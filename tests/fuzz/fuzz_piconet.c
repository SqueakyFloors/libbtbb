/*
 * Fuzz target: clock and UAP recovery, over a run of packets the fuzzer chose
 * delivered to one piconet.
 *
 * The other BR/EDR target decodes one packet on its own. This one is about
 * what a capture tool does with a stream of them: btbb_process_packet() for
 * each, which recovers the UAP and CLK1-6 from headers, narrows candidate
 * CLK1-27 values against the channels the packets arrived on, and then
 * follows the piconet, decoding each packet at the clock it found. That last
 * step is where crc_check() sits.
 *
 * The library starts the candidate search itself only on a narrow path, so an
 * option bit starts it the way a capture tool does, once the piconet has its
 * UAP. From then on btbb_process_packet() narrows the candidates packet by
 * packet, which is how a run reaches CLK1-27, the follow, and giving a clock
 * up again.
 *
 * btbb_init_survey() is deliberately not called: it turns the library's
 * survey mode on for the whole process and there is no call that turns it
 * off, so one input choosing it would change how every later input decodes.
 *
 * Input layout:
 *   byte 0      options: bit 0 whitens the packets, bit 1 gives the piconet
 *               the channel map in bytes 5-14, bit 2 gives it the UAP in
 *               byte 4 up front, bit 3 gives up the followed clock after the
 *               first packet, bit 4 starts the candidate search once the
 *               piconet has its UAP
 *   bytes 1-3   the piconet's LAP, least significant byte first
 *   byte 4      the UAP
 *   bytes 5-14  the AFH channel map, one bit per channel
 *   rest        a run of packet records, each PACKET_RECORD_BYTES long:
 *                 byte 0      the channel the packet arrived on
 *                 bytes 1-4   the observer's clock, least significant byte
 *                             first, in 312.5 us CLKN ticks
 *                 bytes 5-28  the symbol stream, 8 symbols to a byte
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

/* The symbols one packet record carries: 192, which holds the 72-symbol
 * access code and trailer, the 54-symbol header, and a short payload after
 * it. Keeping a record small is what lets a run hold enough packets for
 * recovery to get somewhere inside libFuzzer's input length limit. */
#define PACKET_SYMBOL_BYTES 24

/* channel, clock, symbols */
#define PACKET_RECORD_BYTES (1 + 4 + PACKET_SYMBOL_BYTES)

/* How many records one input may hold. Recovery over a few dozen packets is
 * as much of it as a stream reaches, and a cap keeps one input's time bounded
 * however large libFuzzer's -max_len is. */
#define MAX_PACKET_RECORDS 48

/* A CLKN is 28 bits (Core Specification Vol 2, Part B, 1.2), so a wider value
 * is not a clock a receiver could report. */
#define CLKN_MASK 0x0fffffff

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
	/* the 79 channels and the 28-bit clock the interface defines, so that
	 * what is fuzzed is what a receiver could report */
	channel = (uint8_t) (fz_u8(&in) % BT_NUM_CHANNELS);
	clkn = fz_u32(&in) & CLKN_MASK;
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
	btbb_packet_unref(pkt);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_input in;
	btbb_piconet *pn;
	const uint8_t *records;
	size_t records_len, record;
	uint8_t options, uap, afh_map[10];
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
	 * controller has it, and one that has to find the map itself from the
	 * channels packets arrive on. */
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

		/* Give up the clock, as a tool does when the follow has gone
		 * quiet, so that discovery runs again over the packets that
		 * follow. */
		if (records_done == 0 && ((options >> 3) & 1))
			(void) btbb_piconet_drop_clock(pn);

		/*
		 * Start reversing hops, which is what a tool does once it has
		 * the piconet's UAP: btbb_process_packet() reaches the
		 * candidate search itself only when it recovers the UAP and
		 * CLK1-6 from the same packet. From here on each packet
		 * narrows the candidates, which is the path to CLK1-27 and
		 * the follow.
		 *
		 * It is behind an option bit because choosing candidates
		 * walks the whole CLK7-27 space once, which costs about as
		 * much as the rest of an input put together, and an input the
		 * fuzzer did not point at the search should not pay for it.
		 */
		if (((options >> 4) & 1) &&
		    btbb_piconet_get_flag(pn, BTBB_UAP_VALID) &&
		    !btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
			if (btbb_init_hop_reversal(0, pn) > 0)
				(void) btbb_winnow(pn);
		}
	}

	/* What a tool prints once recovery has run. It walks the map the run
	 * built up, and goes to standard output as a dump does. */
	btbb_print_afh_map(pn);

	btbb_piconet_unref(pn);
	return 0;
}
