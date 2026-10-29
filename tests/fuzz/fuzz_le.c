/*
 * Fuzz target: the LE packet decoder and the packet dump, over the bytes of
 * a frame the fuzzer chose.
 *
 * Input layout:
 *   bytes 0-1   the frequency in MHz, least significant byte first
 *   bytes 2-5   the capture clock, least significant byte first
 *   rest        the frame as it comes off the air, access address first
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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_input in;
	uint8_t fixed[LELL_LEGACY_STREAM_LEN];
	const uint8_t *rest;
	size_t rest_len;
	lell_packet *pkt;
	uint16_t phys_channel;
	uint32_t clk100ns;
	unsigned length;

	fz_start(&in, data, size);
	phys_channel = fz_u16(&in);
	clk100ns = fz_u32(&in);
	rest = fz_rest(&in, &rest_len);

	pkt = fz_le_packet(rest, rest_len, phys_channel, clk100ns);
	if (pkt == NULL)
		return 0;

	length = lell_get_payload_length(pkt);
	if (length > LE_MAX_PDU_LENGTH)
		abort();	/* more payload than the buffer holds */
	if (rest_len >= LE_FRAMING_BYTES &&
	    length > rest_len - LE_FRAMING_BYTES)
		abort();	/* more payload than the capture carried */
	if (lell_get_channel_k(pkt) > 39)
		abort();	/* no such radio channel */
	if (lell_get_channel_index(pkt) > 39)
		abort();	/* no such channel index */

	(void) lell_get_access_address(pkt);
	(void) lell_get_access_address_offenses(pkt);
	(void) lell_packet_is_data(pkt);
	(void) lell_get_adv_type_str(pkt);

	/* The dump goes to standard output and its complaints about the frame
	 * to standard error, which a run closes with libFuzzer's
	 * -close_fd_mask. */
	lell_print(pkt);

	lell_packet_unref(pkt);

	/*
	 * The form without a length reads a fixed 64 bytes whatever its
	 * caller holds, so it is called here on a buffer of that size.
	 */
	memset(fixed, 0, sizeof(fixed));
	memcpy(fixed, rest, rest_len < sizeof(fixed) ? rest_len : sizeof(fixed));
	pkt = NULL;
	lell_allocate_and_decode(fixed, phys_channel, clk100ns, &pkt);
	if (pkt != NULL) {
		lell_print(pkt);
		lell_packet_unref(pkt);
	}

	return 0;
}
