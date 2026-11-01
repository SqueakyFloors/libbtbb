/*
 * Fuzz target: clock and UAP recovery, over a run of packets the fuzzer chose
 * delivered to one piconet.
 *
 * The other BR/EDR target decodes one packet on its own. This one is about
 * what a capture tool does with a stream of them: btbb_process_packet() for
 * each, which recovers the UAP from headers, chooses candidate CLK1-27 values
 * from the channel a packet arrived on, narrows them packet by packet, and
 * then follows the piconet. The candidate search and the adaptive hopping
 * (AFH) tables are reached that way, and so is crc_check(), which sits behind
 * the follow.
 *
 * btbb_init_survey() is deliberately not called: it turns the library's
 * survey mode on for the whole process and there is no call that turns it
 * off, so one input choosing it would change how every later input decodes.
 *
 * Input layout:
 *   byte 0      options: bit 0 whitens the packets, bit 1 marks the piconet
 *               adaptive, bit 2 gives it the UAP in byte 4 up front, bit 3
 *               asks for hop reversal as an aliased receiver observes it,
 *               bit 4 gives up the followed clock after the first packet
 *   bytes 1-3   the piconet's LAP, least significant byte first
 *   byte 4      the UAP
 *   bytes 5-14  the AFH channel map, one bit per channel
 *   rest        a run of packet records, each PACKET_RECORD_BYTES long:
 *                 byte 0      the channel the packet arrived on
 *                 bytes 1-4   the local clock, least significant byte first
 *                 bytes 5-44  the symbol stream, 8 symbols to a byte
 *
 * Notes about how recovery is getting on go to standard error, which a run
 * leaves open so that a sanitizer's report is in the log. A long campaign
 * that does not want them can close it too, with -close_fd_mask=3.
 *
 * This file is part of libbtbb
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libbtbb; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "fuzz_input.h"

/* The symbols one packet record carries: 320, which covers the 72-symbol
 * access code and the 54-symbol header three times over, so a header can
 * decode and a short payload can follow it. */
#define PACKET_SYMBOL_BYTES 40

/* channel, clock, symbols */
#define PACKET_RECORD_BYTES (1 + 4 + PACKET_SYMBOL_BYTES)

/* How many records one input may hold. Recovery over a few dozen packets is
 * as much of it as a stream reaches, and a cap keeps one input's time bounded
 * however large libFuzzer's -max_len is. */
#define MAX_PACKET_RECORDS 48

/* How many times the candidate list is narrowed by hand after the run. Each
 * call walks the hops observed so far, so a handful reaches the point where
 * the list stops shrinking. */
#define WINNOW_CALLS 4

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	if (btbb_init(FZ_MAX_AC_ERRORS) < 0)
		abort();
	return 0;
}

/* One packet from the record at 'record', delivered to the piconet. */
static void process_one(const uint8_t *record, uint32_t lap, uint8_t uap,
			int whitened, btbb_piconet *pn)
{
	char symbols[MAX_SYMBOLS];
	struct fuzz_input in;
	btbb_packet *pkt;
	uint8_t channel;
	uint32_t clkn;
	const uint8_t *bytes;
	size_t bytes_len;
	int symbol_count;

	fz_start(&in, record, PACKET_RECORD_BYTES);
	/* the 79 channels the interface defines, so that what is fuzzed is
	 * what a caller can ask for */
	channel = (uint8_t) (fz_u8(&in) % BT_NUM_CHANNELS);
	clkn = fz_u32(&in);
	bytes = fz_rest(&in, &bytes_len);
	symbol_count = fz_symbols(bytes, bytes_len, symbols, sizeof(symbols));

	pkt = fz_packet(symbols, symbol_count, lap, uap, channel, clkn,
			whitened);
	if (pkt == NULL)
		return;

	/* The dump btbb_process_packet() makes of a followed packet goes to
	 * standard output, which a run closes with libFuzzer's
	 * -close_fd_mask. */
	(void) btbb_process_packet(pkt, pn);

	/* btbb_uap_from_header() is the other way in: a tool that recovers
	 * the UAP without handing the packet to the piconet calls it. */
	(void) btbb_uap_from_header(pkt, pn);

	btbb_packet_unref(pkt);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_input in;
	btbb_piconet *pn;
	const uint8_t *records;
	size_t records_len, record;
	uint8_t options, uap, afh_map[10], channel;
	uint32_t lap;
	int i, whitened, records_done = 0;

	fz_start(&in, data, size);
	options = fz_u8(&in);
	/* 24 bits: the LAP the piconet and every packet's access code carry */
	lap = fz_u16(&in);
	lap |= (uint32_t) fz_u8(&in) << 16;
	uap = fz_u8(&in);
	for (i = 0; i < 10; i++)
		afh_map[i] = fz_u8(&in);
	whitened = options & 1;

	pn = btbb_piconet_new();
	if (pn == NULL)
		return 0;
	btbb_init_piconet(pn, lap);

	/* A piconet whose map is known, as a tool told one by the host's
	 * controller has it, and one that has to find the map itself. */
	if ((options >> 1) & 1) {
		btbb_piconet_set_flag(pn, BTBB_IS_AFH, 1);
		btbb_piconet_set_afh_map(pn, afh_map);
	}
	/* A tool given the UAP skips recovering it, which is a different path
	 * through btbb_process_packet() than recovering it. */
	if ((options >> 2) & 1)
		btbb_piconet_set_uap(pn, uap);

	records = fz_rest(&in, &records_len);
	for (record = 0; record + PACKET_RECORD_BYTES <= records_len &&
	     records_done < MAX_PACKET_RECORDS;
	     record += PACKET_RECORD_BYTES, records_done++) {
		process_one(records + record, lap, uap, whitened, pn);

		/* Give up the clock partway, as a tool does when the follow
		 * has gone quiet, so that discovery runs again over the
		 * packets that follow. */
		if (records_done == 0 && ((options >> 4) & 1))
			(void) btbb_piconet_drop_clock(pn);
	}

	/*
	 * The candidate search and the winnowing, driven directly. A tool
	 * that has its own idea of when to start reversing hops calls these,
	 * and reaching them does not depend on the run above having got far
	 * enough to call them itself.
	 */
	if (btbb_init_hop_reversal((options >> 3) & 1, pn) > 0) {
		for (i = 0; i < WINNOW_CALLS; i++)
			if (btbb_winnow(pn) <= 1)
				break;
	}

	/* The AFH map a detector builds up, and the accessors a tool reads it
	 * back through. The map bytes from the front of the input choose
	 * which channels are marked, so marking and clearing both run, and
	 * each one rebuilds the mapping tables hop selection indexes. */
	for (i = 0; i < BT_NUM_CHANNELS; i++) {
		channel = (uint8_t) i;
		if ((afh_map[i / 8] >> (i % 8)) & 1)
			(void) btbb_piconet_set_channel_seen(pn, channel);
		else
			(void) btbb_piconet_clear_channel_seen(pn, channel);
		(void) btbb_piconet_get_channel_seen(pn, channel);
	}
	/* a channel past the last one, which reads as used */
	(void) btbb_piconet_get_channel_seen(pn, BT_NUM_CHANNELS);

	/* What a tool prints and records once recovery has run. The map goes
	 * to standard output, as a dump does. */
	btbb_print_afh_map(pn);
	(void) btbb_piconet_get_afh_map(pn);
	(void) btbb_piconet_get_bdaddr(pn);
	(void) btbb_piconet_get_nap(pn);
	(void) btbb_piconet_get_uap(pn);
	(void) btbb_piconet_get_lap(pn);
	(void) btbb_piconet_get_clk_offset(pn);

	btbb_piconet_unref(pn);
	return 0;
}
