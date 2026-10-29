/*
 * Fuzz target: the BR/EDR header and payload decoders, and the packet dump,
 * over a stream of symbols the fuzzer chose.
 *
 * Input layout:
 *   byte 0      options: bit 0 whitens, bit 1 marks the clock valid,
 *               bits 2-3 the modulation, bits 4-6 the transport
 *   byte 1      the channel
 *   byte 2      the UAP the decoder checks the header against
 *   bytes 3-6   the clock, least significant byte first
 *   byte 7      the eSCO payload length the link agreed, which the decoder
 *               searches for when it is not told one
 *   rest        the symbol stream, 8 symbols to a byte
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

/* The longest payload the decoder holds, in bytes: what
 * btbb_get_payload_packed() may write, so a write past it is the library
 * reporting a payload longer than it decoded into. */
#define LONGEST_PAYLOAD (MAX_PAYLOAD_LENGTH / 8)

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	if (btbb_init(FZ_MAX_AC_ERRORS) < 0)
		abort();
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_input in;
	char symbols[MAX_SYMBOLS];
	char packed[LONGEST_PAYLOAD];
	const uint8_t *rest;
	size_t rest_len;
	btbb_packet *pkt;
	uint8_t options, channel, uap, esco_length;
	uint32_t clkn;
	int symbol_count, payload_length;

	fz_start(&in, data, size);
	options = fz_u8(&in);
	/* the values the interface defines: 79 channels, three modulations
	 * and five transports, so that what is fuzzed is what a caller can
	 * ask for */
	channel = (uint8_t) (fz_u8(&in) % BT_NUM_CHANNELS);
	uap = fz_u8(&in);
	clkn = fz_u32(&in);
	esco_length = fz_u8(&in);

	rest = fz_rest(&in, &rest_len);
	symbol_count = fz_symbols(rest, rest_len, symbols, sizeof(symbols));

	pkt = fz_packet(symbols, symbol_count, 0x9e8b33, uap, channel, clkn,
			options & 1);
	if (pkt == NULL)
		return 0;

	btbb_packet_set_flag(pkt, BTBB_CLK27_VALID, (options >> 1) & 1);
	btbb_packet_set_modulation(pkt, (uint8_t) (((options >> 2) & 3) % 3));
	btbb_packet_set_transport(pkt, (uint8_t) (((options >> 4) & 7) % 5));
	/* 0 leaves the decoder to search for the length, as a tool that was
	 * not told the link's does */
	btbb_packet_set_esco_length(pkt, esco_length);

	(void) btbb_header_present(pkt);
	if (btbb_decode_header(pkt)) {
		(void) btbb_decode_payload(pkt);

		payload_length = btbb_packet_get_payload_length(pkt);
		if (payload_length < 0 || payload_length > LONGEST_PAYLOAD)
			abort();	/* more payload than was decoded */
		if (btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD))
			(void) btbb_get_payload_packed(pkt, packed);
	}

	/* the accessors a capture tool reads after a decode */
	(void) btbb_packet_get_type(pkt);
	(void) btbb_packet_get_lt_addr(pkt);
	(void) btbb_packet_get_header_flags(pkt);
	(void) btbb_packet_get_hec(pkt);
	(void) btbb_packet_get_header_packed(pkt);
	(void) btbb_packet_get_nap(pkt);

	/* The dump goes to standard output, which a run closes with
	 * libFuzzer's -close_fd_mask. */
	btbb_print_packet(pkt);

	/* btbb_decode() is the other way in, and a capture tool calls it on a
	 * packet it has already decoded once. */
	(void) btbb_decode(pkt);

	btbb_packet_unref(pkt);
	return 0;
}
