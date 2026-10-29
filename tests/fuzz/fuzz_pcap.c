/*
 * Fuzz target: the PCAP writers, fed packets the fuzzer's bytes decoded
 * into. Each run writes a BR/EDR capture, an LE capture and an LE capture in
 * the PPI format, over one file in the temporary directory.
 *
 * Input layout:
 *   byte 0      options: bit 0 whitens the BR/EDR symbols, bit 1 chooses
 *               the PPI version, bits 2-3 how many records to write,
 *               bits 4-5 which of the three writers to write with
 *   byte 1      the channel
 *   byte 2      the UAP the BR/EDR decoder checks the header against
 *   byte 3      the signal level in dBm
 *   byte 4      the noise level in dBm
 *   bytes 5-8   the address the capture tool was listening for
 *   byte 9      the reference UAP
 *   bytes 10-17 the timestamp in nanoseconds
 *   rest        the frame: the bytes of an LE frame, and the same bytes
 *               unpacked into BR/EDR symbols
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

#include <unistd.h>

#include "fuzz_input.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	if (btbb_init(FZ_MAX_AC_ERRORS) < 0)
		abort();
	fz_capture_file_init();
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_input in;
	char symbols[MAX_SYMBOLS];
	const uint8_t *rest;
	size_t rest_len;
	btbb_packet *pkt;
	lell_packet *le_pkt;
	btbb_pcap_handle *bredr_capture = NULL;
	lell_pcap_handle *le_capture = NULL;
	uint8_t options, channel, uap, refuap;
	int8_t sigdbm, noisedbm;
	uint32_t refaddress;
	uint64_t ns;
	int symbol_count, records, writer, i;

	fz_start(&in, data, size);
	options = fz_u8(&in);
	channel = fz_u8(&in);
	uap = fz_u8(&in);
	sigdbm = (int8_t) fz_u8(&in);
	noisedbm = (int8_t) fz_u8(&in);
	refaddress = fz_u32(&in);
	refuap = fz_u8(&in);
	ns = fz_u64(&in);
	rest = fz_rest(&in, &rest_len);

	/* one to four records, so that a second record after a first is
	 * covered without the file growing over a long run */
	records = 1 + ((options >> 2) & 3);

	symbol_count = fz_symbols(rest, rest_len, symbols, sizeof(symbols));
	pkt = fz_packet(symbols, symbol_count, 0x9e8b33, uap, channel,
			(uint32_t) ns, options & 1);
	if (pkt == NULL)
		return 0;
	if (btbb_decode_header(pkt))
		(void) btbb_decode_payload(pkt);

	le_pkt = fz_le_packet(rest, rest_len, 2402, (uint32_t) ns);

	/* One writer a run: creating a file costs more than a decode, so a run
	 * that wrote all three would spend its time in the file system. */
	writer = ((options >> 4) & 3) % 3;

	if (writer == 0 &&
	    btbb_pcap_create_file(fz_capture_file_path(), &bredr_capture) == 0 &&
	    bredr_capture != NULL) {
		for (i = 0; i < records; i++)
			(void) btbb_pcap_append_packet(bredr_capture, ns,
						       sigdbm, noisedbm,
						       refaddress, refuap,
						       pkt);
		(void) btbb_pcap_close(bredr_capture);
	}

	if (writer == 1 && le_pkt != NULL &&
	    lell_pcap_create_file(fz_capture_file_path(), &le_capture) == 0 &&
	    le_capture != NULL) {
		for (i = 0; i < records; i++)
			(void) lell_pcap_append_packet(le_capture, ns, sigdbm,
						       noisedbm, refaddress,
						       le_pkt);
		(void) lell_pcap_close(le_capture);
	}

	le_capture = NULL;
	if (writer == 2 && le_pkt != NULL &&
	    lell_pcap_ppi_create_file(fz_capture_file_path(),
				      (options >> 1) & 1, &le_capture) == 0 &&
	    le_capture != NULL) {
		for (i = 0; i < records; i++)
			(void) lell_pcap_append_ppi_packet(le_capture, ns,
							   (uint8_t) (ns >> 8),
							   sigdbm, noisedbm,
							   sigdbm,
							   (uint8_t) records,
							   le_pkt);
		(void) lell_pcap_close(le_capture);
	}

	if (le_pkt != NULL)
		lell_packet_unref(le_pkt);
	btbb_packet_unref(pkt);
	return 0;
}
