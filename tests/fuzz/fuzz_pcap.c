/*
 * Fuzz target: the PCAP writers, fed packets the fuzzer's bytes decoded
 * into. Each run writes a BR/EDR capture, an LE capture and an LE capture in
 * the PPI format, over one file in the temporary directory.
 *
 * Input layout:
 *   byte 0      options: bit 0 whitens the BR/EDR symbols, bit 1 chooses
 *               the PPI version, bits 2-3 how many records to write,
 *               bits 4-5 which of the three writers to write with
 *   byte 1      the channel: 0 to 79 for the BR/EDR record, and the LE one
 *               is decoded at the frequency of LE channel 'channel % 40'
 *   byte 2      bits 0-1 the modulation, bits 2-4 the transport, both of
 *               which a BR/EDR record carries
 *   byte 3      the UAP the BR/EDR decoder checks the header against
 *   byte 4      the signal level in dBm
 *   byte 5      the noise level in dBm
 *   bytes 6-9   the address the capture tool was listening for
 *   byte 10     the reference UAP
 *   bytes 11-18 the timestamp in nanoseconds
 *   rest        the frame: the bytes of an LE frame, and the same bytes
 *               unpacked into BR/EDR symbols
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
	uint8_t options, channel, radio, uap, refuap;
	int8_t sigdbm, noisedbm;
	uint32_t refaddress;
	uint64_t ns;
	int symbol_count, records, writer, i;

	fz_start(&in, data, size);
	options = fz_u8(&in);
	/* the values the interface defines: 79 channels, three modulations
	 * and five transports, so that what is fuzzed is what a caller can
	 * ask for */
	channel = (uint8_t) (fz_u8(&in) % BT_NUM_CHANNELS);
	radio = fz_u8(&in);
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
	btbb_packet_set_modulation(pkt, (uint8_t) ((radio & 3) % 3));
	btbb_packet_set_transport(pkt, (uint8_t) (((radio >> 2) & 7) % 5));
	if (btbb_decode_header(pkt))
		(void) btbb_decode_payload(pkt);

	/* 2402 to 2480 MHz: the advertising channels among them, and the
	 * data channels that make lell_packet_is_data() true */
	le_pkt = fz_le_packet(rest, rest_len,
			      (uint16_t) (2402 + 2 * (channel % 40)),
			      (uint32_t) ns);

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
