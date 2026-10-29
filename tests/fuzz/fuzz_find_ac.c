/*
 * Fuzz target: the access code search, btbb_find_ac() and the length form
 * btbb_find_ac_n(), over a stream of symbols the fuzzer chose.
 *
 * Input layout:
 *   byte 0      options: bit 0 searches for any LAP instead of the one
 *               below, bits 1-2 are the error budget, 0 to 2
 *   bytes 1-3   the LAP to search for, least significant byte first
 *   bytes 4-5   the search length, least significant byte first
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
	const uint8_t *rest;
	size_t rest_len;
	btbb_packet *pkt = NULL;
	uint8_t options;
	uint32_t lap;
	int max_ac_errors, search_length, symbol_count, offset;

	fz_start(&in, data, size);
	options = fz_u8(&in);
	lap = fz_u8(&in);
	lap |= (uint32_t) fz_u8(&in) << 8;
	lap |= (uint32_t) fz_u8(&in) << 16;
	search_length = fz_u16(&in);
	if (options & 1)
		lap = LAP_ANY;
	max_ac_errors = (options >> 1) & 3;
	if (max_ac_errors > FZ_MAX_AC_ERRORS)
		max_ac_errors = FZ_MAX_AC_ERRORS;

	rest = fz_rest(&in, &rest_len);
	symbol_count = fz_symbols(rest, rest_len, symbols, sizeof(symbols));

	/* The length form is told what the stream holds and reads no
	 * further, so any search length is in bounds for it. */
	offset = btbb_find_ac_n(symbols, (size_t) symbol_count, search_length,
				lap, max_ac_errors, &pkt);
	if (offset >= 0 && offset >= symbol_count)
		abort();	/* a match outside the stream it was given */
	if (pkt != NULL) {
		btbb_packet_unref(pkt);
		pkt = NULL;
	}

	/*
	 * The form without a length trusts its caller for
	 * search_length + 63 more symbols, which is the promise its
	 * callers make, so it is only ever called here with a search
	 * length the stream covers.
	 */
	if (symbol_count > SYNCWORD_SYMBOLS - 1) {
		int room = symbol_count - (SYNCWORD_SYMBOLS - 1);

		if (search_length > room)
			search_length = room;
		offset = btbb_find_ac(symbols, search_length, lap,
				      max_ac_errors, &pkt);
		if (offset >= 0 && offset >= search_length)
			abort();	/* a match past the search */
		if (pkt != NULL)
			btbb_packet_unref(pkt);
	}

	return 0;
}
