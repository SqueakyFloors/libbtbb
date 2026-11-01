/*
 * Fuzz target: the PCAPNG writers, fed packets the fuzzer's bytes decoded
 * into. Each run writes a BR/EDR capture and an LE capture, including the
 * records that are not frames - an address, a clock, and the parameters of a
 * connection request - over one file in the temporary directory.
 *
 * Input layout:
 *   byte 0      options: bit 0 whitens the BR/EDR symbols, bits 2-3 how
 *               many records to write, bit 4 writes the LE capture instead
 *               of the BR/EDR one
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
 *   byte 19     the length of the interface description that follows
 *   then        that many bytes of interface description
 *   rest        the frame: the bytes of an LE frame, and the same bytes
 *               unpacked into BR/EDR symbols
 */

#include "fuzz_input.h"
#include "pcapng-bt.h"

/* The bytes of a connection request's payload, InitA through HopSCA, which
 * lell_pcapng_record_connect_req() reads. The library's own constant, so that
 * the buffer here cannot be shorter than what that function reads. */
#define CONNECT_REQ_PDU_BYTES LE_CONNECT_IND_PDU_LENGTH

/*
 * The longest interface description the writer takes, which is far longer
 * than any a capture tool writes - Ubertooth's pass nine characters - and
 * long enough to reach the room the file sets aside for its options.
 */
#define LONGEST_DESCRIPTION 255

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
	char description[LONGEST_DESCRIPTION + 1];
	uint8_t connect_pdu[CONNECT_REQ_PDU_BYTES];
	const uint8_t *rest;
	size_t rest_len, i;
	btbb_packet *pkt;
	lell_packet *le_pkt;
	btbb_pcapng_handle *bredr_capture = NULL;
	lell_pcapng_handle *le_capture = NULL;
	uint8_t options, channel, radio, uap, refuap, description_length;
	int8_t sigdbm, noisedbm;
	uint32_t refaddress;
	uint64_t ns;
	int symbol_count, records, record;

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

	/* A description the fuzzer chose, which the file carries as an option
	 * of its interface description block. */
	description_length = fz_u8(&in);
	for (i = 0; i < description_length; i++) {
		uint8_t byte = fz_u8(&in);

		/* a zero would end the string early, which only makes the
		 * description shorter than the length chosen above */
		description[i] = (char) (byte == 0 ? ' ' : byte);
	}
	description[description_length] = '\0';

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

	/* One capture a run: creating a file costs more than a decode, so a
	 * run that wrote both would spend its time in the file system. */
	if ((options & 0x10) == 0 &&
	    btbb_pcapng_create_file(fz_capture_file_path(), description,
				    &bredr_capture) == 0 &&
	    bredr_capture != NULL) {
		for (record = 0; record < records; record++) {
			(void) btbb_pcapng_append_packet(bredr_capture, ns,
							 sigdbm, noisedbm,
							 refaddress, refuap,
							 pkt);
			(void) btbb_pcapng_record_bdaddr(bredr_capture, ns,
							 refuap,
							 (uint8_t) record);
			(void) btbb_pcapng_record_btclock(bredr_capture, ns,
							  ns, refaddress,
							  refaddress);
		}
		(void) btbb_pcapng_close(bredr_capture);
	}

	if ((options & 0x10) != 0 && le_pkt != NULL &&
	    lell_pcapng_create_file(fz_capture_file_path(), description,
				    &le_capture) == 0 &&
	    le_capture != NULL) {
		memset(connect_pdu, 0, sizeof(connect_pdu));
		memcpy(connect_pdu, rest,
		       rest_len < sizeof(connect_pdu) ? rest_len
						      : sizeof(connect_pdu));
		for (record = 0; record < records; record++) {
			(void) lell_pcapng_append_packet(le_capture, ns,
							 sigdbm, noisedbm,
							 refaddress, le_pkt);
			(void) lell_pcapng_record_connect_req(le_capture, ns,
							      connect_pdu);
		}
		(void) lell_pcapng_close(le_capture);
	}

	if (le_pkt != NULL)
		lell_packet_unref(le_pkt);
	btbb_packet_unref(pkt);
	return 0;
}
