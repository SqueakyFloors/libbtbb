/*
 * Capture file writers: check the bytes that reach a file against the pcapng
 * specification and the published LINKTYPE_BLUETOOTH_BREDR_BB and
 * LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR layouts.
 *
 * Every record is read back out of the file by the small reader below rather
 * than compared against the struct that wrote it, so a field in the wrong
 * place or at the wrong offset shows up.
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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "btbb.h"
#include "bluetooth_le_packet.h"
#include "pcapng.h"
#include "pcapng-bt.h"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/* ------------------------------ file reading ----------------------------- */

/* Room for the largest file these tests write: two header blocks, each a
   whole number of memory pages, plus a packet. */
#define MAX_CAPTURE (1024 * 1024)

static uint8_t capture[MAX_CAPTURE];
static size_t capture_len;

/* Read a whole capture file into capture[]; returns 0 on failure. */
static int read_capture(const char *filename)
{
	FILE *f = fopen(filename, "rb");

	capture_len = 0;
	if (f == NULL)
		return 0;
	capture_len = fread(capture, 1, sizeof(capture), f);
	fclose(f);
	return capture_len > 0;
}

static uint16_t rd16(size_t off)
{
	return (uint16_t) (capture[off] | (capture[off + 1] << 8));
}

static uint32_t rd32(size_t off)
{
	return (uint32_t) capture[off] | ((uint32_t) capture[off + 1] << 8) |
	       ((uint32_t) capture[off + 2] << 16) |
	       ((uint32_t) capture[off + 3] << 24);
}

/* Every byte in [off, off+len) is zero. */
static int all_zero(size_t off, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (capture[off + i] != 0)
			return 0;
	}
	return 1;
}

/*
 * Walk the blocks of a pcapng file looking for one of the given type, and
 * return its offset, or capture_len if there is none. A block's header is a
 * 4-byte type then a 4-byte total length, and its last 4 bytes repeat that
 * length, which is what this checks on the way past.
 */
static size_t find_block(uint32_t block_type, size_t from)
{
	size_t off = from;

	while (off + 12 <= capture_len) {
		uint32_t type = rd32(off);
		uint32_t total = rd32(off + 4);

		if ((total < 12) || ((total % 4) != 0) ||
		    (off + total > capture_len)) {
			printf("FAIL block at %zu has length %u\n", off, total);
			++failures;
			return capture_len;
		}
		if (rd32(off + total - 4) != total) {
			printf("FAIL block at %zu does not repeat its "
			       "length\n", off);
			++failures;
			return capture_len;
		}
		if (type == block_type)
			return off;
		off += total;
	}
	return capture_len;
}

/*
 * Walk a block's options looking for one code, and return the offset of its
 * value, or 0 if there is none. options_at is the offset of the first option.
 * Each option is a 2-byte code, a 2-byte length and the value padded up to a
 * 4-byte boundary; the list ends at an all-zero word.
 */
static size_t find_option(size_t block_off, size_t options_at, uint16_t code,
			  uint16_t *value_length)
{
	uint32_t total = rd32(block_off + 4);
	size_t end = block_off + total - 4;
	size_t off = block_off + options_at;

	while (off + 4 <= end) {
		uint16_t opt_code = rd16(off);
		uint16_t opt_length = rd16(off + 2);
		size_t padded = (size_t) (4 * ((opt_length + 3u) / 4u));

		if (opt_code == OPT_ENDOFOPT)
			return 0;
		if (off + 4 + padded > end) {
			printf("FAIL option %04x at %zu runs past its "
			       "block\n", opt_code, off);
			++failures;
			return 0;
		}
		if (opt_code == code) {
			if (value_length != NULL)
				*value_length = opt_length;
			return off + 4;
		}
		off += 4 + padded;
	}
	return 0;
}

/*
 * Walk the records of a pcap file in capture[] and return how many whole ones
 * it holds. A record is a 16-byte header whose third word is the length of
 * the data after it, so a file that ends part way through a record ends on a
 * length reaching past its last byte, which is what a reader chokes on.
 * Returns -1 for such a file.
 */
static int count_pcap_records(void)
{
	size_t off = 24;	/* the file header */
	int records = 0;

	while (off < capture_len) {
		size_t incl_len;

		if (off + 16 > capture_len) {
			printf("FAIL record header at %zu is cut short\n", off);
			return -1;
		}
		incl_len = rd32(off + 8);
		if (off + 16 + incl_len > capture_len) {
			printf("FAIL record at %zu says %zu bytes, file has "
			       "%zu\n", off, incl_len, capture_len - off - 16);
			return -1;
		}
		off += 16 + incl_len;
		records++;
	}
	return records;
}

/* ------------------------------ test packets ----------------------------- */

/*
 * Vol 2, Part G, Section 6.1 of the Bluetooth Core specification: a complete
 * DH1 packet, as the symbols that go on air, least significant bit first and
 * not whitened. The header is the rate 1/3 encoding of header data 0x123 and
 * HEC 0x06, which is LT_ADDR 3, TYPE DH1, for a UAP of 0x47. The payload is a
 * one byte payload header, the five data bytes 01 to 05, and the CRC.
 */
#define SPEC_DH1_UAP 0x47
#define SPEC_DH1_LAP 0x9e8b33

static const char SPEC_DH1_HEADER[] =
	"111111000" "000000111000" "000111000"
	"000111111000000000000000";
static const char SPEC_DH1_PAYLOAD[] =
	"01110100"
	"10000000" "01000000" "11000000" "00100000" "10100000"
	"1110110000110110";

/* The bytes the decoder makes of that payload. */
static const uint8_t SPEC_DH1_BYTES[] = {
	0x2e, 0x01, 0x02, 0x03, 0x04, 0x05, 0x37, 0x6c
};

/* A POLL, which Vol 2, Part B, Section 6.5.4.1 gives no payload at all:
   LT_ADDR 1 in the low three bits and TYPE 1 in the next four. */
#define SPEC_POLL_HEADER_DATA (1 | (1 << 3))

/* Lay out one BR/EDR packet as symbols: the 64-bit sync word of the LAP, the
   4-symbol trailer, the 18 header bits each sent three times, then the
   payload. Returns the number of symbols. */
static int build_bredr_stream(char *stream, uint32_t lap,
			      const char *header_symbols,
			      const char *payload_symbols)
{
	uint64_t syncword = btbb_gen_syncword((int) lap);
	int i, payload_len = 0;

	memset(stream, 0, 1024);
	for (i = 0; i < 64; i++)
		stream[i] = (char) ((syncword >> i) & 1);
	for (i = 0; i < 4; i++)
		stream[64 + i] = (char) (((syncword >> 63) ^ (i + 1)) & 1);
	for (i = 0; header_symbols[i] != '\0'; i++)
		stream[68 + i] = (char) (header_symbols[i] - '0');
	if (payload_symbols != NULL) {
		for (payload_len = 0; payload_symbols[payload_len] != '\0';
		     payload_len++)
			stream[122 + payload_len] =
				(char) (payload_symbols[payload_len] - '0');
	}
	return 122 + payload_len;
}

/* The 18 header bits, each sent three times: the rate 1/3 code of Vol 2,
   Part B, Section 7.4. */
static void encode_header_13(uint16_t header_data, uint8_t hec, char *out)
{
	uint32_t header = (uint32_t) header_data | ((uint32_t) hec << 10);
	int i;

	for (i = 0; i < 18; i++) {
		char bit = (char) ('0' + ((header >> i) & 1));

		out[i * 3] = bit;
		out[i * 3 + 1] = bit;
		out[i * 3 + 2] = bit;
	}
	out[54] = '\0';
}

/* Hand a built stream to the decoder with the clock and UAP it was built
   for, and decode it as the library does for a followed piconet. */
static btbb_packet *decode_bredr(char *stream, int length, uint8_t uap)
{
	btbb_packet *pkt = NULL;

	/* finding the access code is what gives the packet its LAP */
	if (btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) != 0)
		return NULL;
	if (pkt == NULL)
		return NULL;
	btbb_packet_set_data(pkt, stream, length, 0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_uap(pkt, uap);
	/* Two different values, so a record's payload_transport_rate byte
	   pins both of its nibbles. The library never sets these itself. */
	btbb_packet_set_transport(pkt, BTBB_TRANSPORT_ACL);
	btbb_packet_set_modulation(pkt, BTBB_MOD_8DPSK);
	if (btbb_decode_header(pkt))
		(void) btbb_decode_payload(pkt);
	return pkt;
}

/* transport ACL in the high nibble, 8DPSK modulation in the low one */
#define EXPECTED_TRANSPORT_RATE ((BTBB_TRANSPORT_ACL << 4) | BTBB_MOD_8DPSK)

/*
 * The HEC that goes with this header data and UAP, found by trying all 256 of
 * them against the decoder's own check. The HEC generator itself is pinned by
 * the specification's sample data in tests/test_spec_bredr.c, so this only
 * saves writing a second copy of it here.
 */
static int hec_for(uint16_t header_data, uint8_t uap)
{
	char header_symbols[55];
	char stream[1024];
	int hec;

	for (hec = 0; hec < 256; hec++) {
		btbb_packet *pkt;
		int decoded;

		encode_header_13(header_data, (uint8_t) hec, header_symbols);
		(void) build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
					  NULL);
		pkt = decode_bredr(stream, 122, uap);
		if (pkt == NULL)
			return -1;
		/* the header's fields are only recorded on the packet when it
		   decodes, which is when the HEC matched the UAP */
		decoded = (btbb_packet_get_lt_addr(pkt) ==
			   (header_data & 0x7)) &&
			  (btbb_packet_get_type(pkt) ==
			   ((header_data >> 3) & 0xf)) &&
			  (btbb_packet_get_hec(pkt) == (uint8_t) hec);
		btbb_packet_unref(pkt);
		if (decoded)
			return hec;
	}
	return -1;
}

/*
 * A CONNECT_IND as it appears in a capture: the advertising access address,
 * the PDU header, the 34 payload bytes InitA through HopSCA, and the CRC.
 * Every field holds a distinct value so a record taken from the wrong offset
 * cannot match by accident.
 */
#define CONNECT_IND_CHANNEL_MHZ 2402	/* channel index 37, an advertising one */

static const uint8_t CONNECT_IND[] = {
	0xd6, 0xbe, 0x89, 0x8e,			/* access address */
	0x45, 0x22,				/* PDU header: CONNECT_IND, 34 */
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16,	/* InitA */
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26,	/* AdvA */
	0x78, 0x56, 0x34, 0x12,			/* AA */
	0xa1, 0xa2, 0xa3,			/* CRCInit */
	0x05,					/* WinSize */
	0x06, 0x00,				/* WinOffset */
	0x18, 0x00,				/* Interval */
	0x02, 0x00,				/* Latency */
	0x48, 0x00,				/* Timeout */
	0xff, 0xff, 0xff, 0xff, 0x1f,		/* ChM */
	0xc7,					/* HopSCA: hop 7, SCA 6 */
	0xaa, 0xbb, 0xcc			/* CRC */
};

/* ------------------------------- the tests ------------------------------- */

/*
 * Leave a known pattern on the stack where a writer's record will sit, so a
 * record the writer does not clear shows up as non-zero padding in the file.
 */
static void dirty_stack(void)
{
	volatile uint8_t scratch[4096];

	memset((void *) scratch, 0xaa, sizeof(scratch));
}

static void unlink_quietly(const char *filename)
{
	(void) unlink(filename);
}

/*
 * A BR/EDR pcapng record, field by field against the published
 * LINKTYPE_BLUETOOTH_BREDR_BB layout and the pcapng enhanced packet block.
 */
static void test_bredr_pcapng_record(void)
{
	const char *filename = "test_capture_bredr.pcapng";
	char header_symbols[55];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_packet *pkt;
	size_t blk, data;
	int length, i;
	uint16_t expected_flags;
	uint32_t high_refuap_lap_uap = 0;

	unlink_quietly(filename);
	strcpy(header_symbols, SPEC_DH1_HEADER);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_packet_get_payload_length(pkt) == 8);

	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	dirty_stack();
	CHECK(btbb_pcapng_append_packet(h, 1000000007ull, -20, -80,
					SPEC_DH1_LAP, SPEC_DH1_UAP, pkt) == 0);
	/* a second record with a reference UAP whose top bit is set */
	CHECK(btbb_pcapng_append_packet(h, 1000000008ull, -20, -80,
					SPEC_DH1_LAP, 0x80, pkt) == 0);
	CHECK(btbb_pcapng_close(h) == 0);
	btbb_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;

	/* The block holds the 22-byte pseudoheader and 8 payload bytes, and
	   rounds up to a 4-byte boundary with the options word and trailing
	   length after it. */
	CHECK(rd32(blk + 4) == 68);
	CHECK(rd32(blk + 8) == 0);			/* interface id */
	CHECK(rd32(blk + 12) == 0);			/* timestamp high */
	CHECK(rd32(blk + 16) == 1000000007u);		/* timestamp low */
	CHECK(rd32(blk + 20) == 30);			/* captured length */
	CHECK(rd32(blk + 24) == 30);			/* original length */

	data = blk + 28;
	CHECK(capture[data + 0] == 0);			/* RF channel */
	CHECK((int8_t) capture[data + 1] == -20);	/* signal power */
	CHECK((int8_t) capture[data + 2] == -80);	/* noise power */
	CHECK(capture[data + 3] == 0);			/* AC offenses */
	CHECK(capture[data + 4] == EXPECTED_TRANSPORT_RATE);
	CHECK(capture[data + 5] == 0);			/* corrected header bits */
	CHECK(rd16(data + 6) == 0);			/* corrected payload bits */
	CHECK(rd32(data + 8) == SPEC_DH1_LAP);
	CHECK(rd32(data + 12) ==
	      (SPEC_DH1_LAP | ((uint32_t) SPEC_DH1_UAP << 24)));
	CHECK(rd32(data + 16) == (0x123u | ((uint32_t) 0x06 << 10)));

	/* The DH1's CRC passes, so the record says the HEC and the CRC were
	   both checked and both held. */
	expected_flags = BREDR_DEWHITENED | BREDR_SIGPOWER_VALID |
		BREDR_NOISEPOWER_VALID | BREDR_REFLAP_VALID |
		BREDR_REFUAP_VALID | BREDR_PAYLOAD_PRESENT |
		BREDR_HEC_CHECKED | BREDR_HEC_VALID |
		BREDR_CRC_CHECKED | BREDR_CRC_VALID;
	CHECK(rd16(data + 20) == expected_flags);

	for (i = 0; i < 8; i++)
		CHECK(capture[data + 22 + i] == SPEC_DH1_BYTES[i]);

	/* 58 bytes of block and payload, rounded up to 60, so two bytes of
	   padding before the options word. */
	CHECK(all_zero(blk + 58, 2));
	CHECK(rd32(blk + 60) == 0);			/* end of options */
	CHECK(rd32(blk + 64) == 68);

	/* the second record, with the high reference UAP */
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, blk + 68);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	CHECK(rd32(blk + 16) == 1000000008u);
	high_refuap_lap_uap = rd32(blk + 28 + 12);
	/* a reference UAP with its top bit set is recorded as given; it is the
	   value whose shift used to run into the sign bit of an int */
	CHECK(high_refuap_lap_uap == (SPEC_DH1_LAP | 0x80000000u));
	/* and it is not the UAP the HEC and CRC were checked against, so this
	   record says nothing about either */
	CHECK((rd16(blk + 28 + 20) & BREDR_REFUAP_VALID) != 0);
	CHECK((rd16(blk + 28 + 20) &
	       (BREDR_HEC_CHECKED | BREDR_HEC_VALID |
		BREDR_CRC_CHECKED | BREDR_CRC_VALID)) == 0);
}

/* A POLL has no payload, so the record carries none and says so. */
static void test_bredr_pcapng_empty_payload(void)
{
	const char *filename = "test_capture_poll.pcapng";
	const char *pcap_name = "test_capture_poll.pcap";
	char header_symbols[55];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_pcap_handle *ph = NULL;
	btbb_packet *pkt;
	size_t blk, data;
	int hec;
	uint16_t expected_flags;

	unlink_quietly(filename);
	hec = hec_for(SPEC_POLL_HEADER_DATA, SPEC_DH1_UAP);
	CHECK(hec >= 0);
	if (hec < 0)
		return;
	encode_header_13(SPEC_POLL_HEADER_DATA, (uint8_t) hec,
			 header_symbols);
	(void) build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols, NULL);
	/* enough symbols for the header, which is all a POLL has */
	pkt = decode_bredr(stream, 122, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_packet_get_type(pkt) == 1);		/* POLL */
	CHECK(btbb_packet_get_payload_length(pkt) == 0);

	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	dirty_stack();
	CHECK(btbb_pcapng_append_packet(h, 5ull, -20, -80, LAP_ANY, UAP_ANY,
					pkt) == 0);
	(void) btbb_pcapng_close(h);

	/* the same packet through the pcap writer, which builds the same
	   record */
	unlink_quietly(pcap_name);
	CHECK(btbb_pcap_create_file(pcap_name, &ph) == 0);
	if (ph != NULL) {
		CHECK(btbb_pcap_append_packet(ph, 5ull, -20, -80, LAP_ANY,
					      UAP_ANY, pkt) == 0);
		CHECK(btbb_pcap_close(ph) == 0);
	}
	btbb_packet_unref(pkt);

	/* No reference LAP or UAP was given, so the record has no UAP to
	   attribute a HEC or CRC result to and says nothing was checked, and
	   a POLL carries no CRC in any case. */
	expected_flags = BREDR_DEWHITENED | BREDR_SIGPOWER_VALID |
		BREDR_NOISEPOWER_VALID;

	if (ph != NULL) {
		CHECK(read_capture(pcap_name));
		unlink_quietly(pcap_name);
		if (capture_len != 0) {
			CHECK(rd32(24 + 8) == 22);
			CHECK(rd16(24 + 16 + 20) == expected_flags);
			CHECK(capture_len == 24 + 16 + 22);
		}
	}

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;

	/* the pseudoheader alone, rounded up from 50 to 52 */
	CHECK(rd32(blk + 4) == 60);
	CHECK(rd32(blk + 20) == 22);
	CHECK(rd32(blk + 24) == 22);

	data = blk + 28;
	CHECK(rd16(data + 20) == expected_flags);
	CHECK((rd16(data + 20) & BREDR_PAYLOAD_PRESENT) == 0);
	CHECK((rd16(data + 20) & (BREDR_HEC_CHECKED | BREDR_CRC_CHECKED)) == 0);

	CHECK(all_zero(blk + 50, 2));
	CHECK(rd32(blk + 52) == 0);
	CHECK(rd32(blk + 56) == 60);
}

/*
 * A checked HEC and a checked CRC that did not hold: the record must say the
 * check was made and that it failed, which is a different answer from both
 * "it passed" and "it was never checked".
 */
static void test_bredr_failed_checks(void)
{
	const char *filename = "test_capture_bad.pcapng";
	const char *pcap_name = "test_capture_bad.pcap";
	char header_symbols[55];
	char payload[sizeof(SPEC_DH1_PAYLOAD)];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_pcap_handle *ph = NULL;
	btbb_packet *pkt;
	size_t blk, data;
	int length, hec;
	uint16_t expected_flags;

	/* A DH1 with one payload byte altered, so the CRC it carries no
	   longer matches. */
	unlink_quietly(filename);
	strcpy(header_symbols, SPEC_DH1_HEADER);
	strcpy(payload, SPEC_DH1_PAYLOAD);
	CHECK(payload[8] == '1');
	payload[8] = '0';
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    payload);
	pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_packet_get_payload_length(pkt) == 8);

	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	CHECK(btbb_pcapng_append_packet(h, 11ull, -20, -80, SPEC_DH1_LAP,
					SPEC_DH1_UAP, pkt) == 0);
	(void) btbb_pcapng_close(h);

	/* the same record through the pcap writer */
	unlink_quietly(pcap_name);
	CHECK(btbb_pcap_create_file(pcap_name, &ph) == 0);
	if (ph != NULL) {
		CHECK(btbb_pcap_append_packet(ph, 11ull, -20, -80,
					      SPEC_DH1_LAP, SPEC_DH1_UAP,
					      pkt) == 0);
		CHECK(btbb_pcap_close(ph) == 0);
	}
	btbb_packet_unref(pkt);

	expected_flags = BREDR_DEWHITENED | BREDR_SIGPOWER_VALID |
		BREDR_NOISEPOWER_VALID | BREDR_REFLAP_VALID |
		BREDR_REFUAP_VALID | BREDR_PAYLOAD_PRESENT |
		BREDR_HEC_CHECKED | BREDR_HEC_VALID | BREDR_CRC_CHECKED;

	if (ph != NULL) {
		CHECK(read_capture(pcap_name));
		unlink_quietly(pcap_name);
		if (capture_len != 0) {
			CHECK(rd16(24 + 16 + 20) == expected_flags);
			CHECK((rd16(24 + 16 + 20) & BREDR_CRC_VALID) == 0);
		}
	}

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len != 0) {
		blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
		CHECK(blk < capture_len);
		if (blk < capture_len) {
			data = blk + 28;
			CHECK(rd16(data + 20) == expected_flags);
			CHECK((rd16(data + 20) & BREDR_CRC_VALID) == 0);
			CHECK(capture[data + 23] == 0x00);
		}
	}

	/* The same packet with a HEC that belongs to another UAP, so the
	   header does not decode and no payload is reached. */
	h = NULL;
	unlink_quietly(filename);
	hec = hec_for(0x123, SPEC_DH1_UAP);
	CHECK(hec == 0x06);
	encode_header_13(0x123, (uint8_t) ((hec + 1) & 0xff), header_symbols);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_packet_get_payload_length(pkt) == 0);

	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	CHECK(btbb_pcapng_append_packet(h, 12ull, -20, -80, SPEC_DH1_LAP,
					SPEC_DH1_UAP, pkt) == 0);
	(void) btbb_pcapng_close(h);
	btbb_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	data = blk + 28;
	CHECK(rd16(data + 20) ==
	      (BREDR_DEWHITENED | BREDR_SIGPOWER_VALID |
	       BREDR_NOISEPOWER_VALID | BREDR_REFLAP_VALID |
	       BREDR_REFUAP_VALID | BREDR_HEC_CHECKED));
	CHECK((rd16(data + 20) & BREDR_HEC_VALID) == 0);
	CHECK((rd16(data + 20) & BREDR_CRC_CHECKED) == 0);
}

/*
 * An HV3 voice payload has no CRC (Vol 2, Part B, Section 6.5.4.4), but the
 * decoder reaches it by trying the packet as an EV3 first, which does check
 * one. The record must not report that failed trial: a reader would read
 * every SCO payload as corrupt.
 */
static void test_bredr_voice_payload_has_no_crc(void)
{
	/* FHS, HV3, EV4 and EV5. HV3 has no CRC at all; the eSCO types look
	   for their payload length by checking a CRC at every candidate until
	   they run out of symbols, and an FHS tries all 32 whitening inputs
	   before giving up. */
	static const uint8_t TYPES[] = { 2, 7, 12, 13 };
	size_t row;

	for (row = 0; row < sizeof(TYPES) / sizeof(TYPES[0]); row++) {
		const char *filename = "test_capture_voice.pcapng";
		char header_symbols[55];
		char payload[241];
		char stream[1024];
		btbb_pcapng_handle *h = NULL;
		btbb_packet *pkt;
		uint16_t header_data =
			(uint16_t) (1 | (TYPES[row] << 3));
		size_t blk, data;
		int length, hec, i;

		unlink_quietly(filename);
		hec = hec_for(header_data, SPEC_DH1_UAP);
		CHECK(hec >= 0);
		if (hec < 0)
			return;
		encode_header_13(header_data, (uint8_t) hec, header_symbols);
		/* 30 bytes of payload, which is what an HV3 carries, and not
		   enough for either eSCO type to finish its search. An FHS
		   payload goes through rate 2/3 error correction first, and
		   all zeroes is a valid codeword of any linear code, so that
		   is what the FHS row sends: it decodes, and its CRC does not
		   come out. */
		for (i = 0; i < 240; i++)
			payload[i] = (TYPES[row] == 2) ? '0'
				: (char) ('0' + ((0x9c5a3 >> (i % 19)) & 1));
		payload[240] = '\0';
		length = build_bredr_stream(stream, SPEC_DH1_LAP,
					    header_symbols, payload);
		CHECK(length == 362);
		pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_packet_get_type(pkt) == TYPES[row]);

		CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
		if (h == NULL) {
			btbb_packet_unref(pkt);
			return;
		}
		CHECK(btbb_pcapng_append_packet(h, 13ull, -20, -80,
						SPEC_DH1_LAP, SPEC_DH1_UAP,
						pkt) == 0);
		(void) btbb_pcapng_close(h);
		btbb_packet_unref(pkt);

		CHECK(read_capture(filename));
		unlink_quietly(filename);
		if (capture_len == 0)
			return;
		blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
		CHECK(blk < capture_len);
		if (blk >= capture_len)
			return;
		data = blk + 28;
		/* the header decoded, so the HEC result is there; no CRC
		   result describes any of these payloads */
		CHECK((rd16(data + 20) &
		       (BREDR_HEC_CHECKED | BREDR_HEC_VALID)) ==
		      (BREDR_HEC_CHECKED | BREDR_HEC_VALID));
		CHECK((rd16(data + 20) &
		       (BREDR_CRC_CHECKED | BREDR_CRC_VALID)) == 0);
		/* The FHS gave up on every whitening input it tried, so what
		   was left in its payload was the last one, known to be
		   wrong. The record carries no payload at all. */
		if (TYPES[row] == 2) {
			CHECK((rd16(data + 20) &
			       BREDR_PAYLOAD_PRESENT) == 0);
			CHECK(rd32(blk + 20) == 22);
		}
	}
}

/*
 * The other side of that: an eSCO payload whose CRC does come out is still
 * reported. The decoder finds the length by trying each one, so this packet
 * sends the specification's DH1 payload, whose CRC is right at 8 bytes, under
 * an HV3 header and with one byte to spare, which is what EV3() needs to
 * reach the check at that length.
 */
static void test_bredr_esco_crc_reported(void)
{
	const char *filename = "test_capture_ev3.pcapng";
	char header_symbols[55];
	char payload[sizeof(SPEC_DH1_PAYLOAD) + 8];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_packet *pkt;
	uint16_t header_data = (uint16_t) (1 | (7 << 3));
	size_t blk, data;
	int length, hec;

	unlink_quietly(filename);
	hec = hec_for(header_data, SPEC_DH1_UAP);
	CHECK(hec >= 0);
	if (hec < 0)
		return;
	encode_header_13(header_data, (uint8_t) hec, header_symbols);
	strcpy(payload, SPEC_DH1_PAYLOAD);
	strcat(payload, "00000000");
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    payload);
	pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_packet_get_payload_length(pkt) == 8);

	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	CHECK(btbb_pcapng_append_packet(h, 21ull, -20, -80, SPEC_DH1_LAP,
					SPEC_DH1_UAP, pkt) == 0);
	CHECK(btbb_pcapng_close(h) == 0);
	btbb_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	data = blk + 28;
	CHECK((rd16(data + 20) &
	       (BREDR_CRC_CHECKED | BREDR_CRC_VALID)) ==
	      (BREDR_CRC_CHECKED | BREDR_CRC_VALID));
	CHECK((rd16(data + 20) & BREDR_PAYLOAD_PRESENT) != 0);
}

/*
 * While the library is still guessing a piconet's upper address part it
 * decodes the same packet at up to 64 candidate clocks and keeps the one
 * whose CRC comes out. Those failures say the guess was wrong, not that the
 * air traffic was corrupt, so a record written during that search must not
 * claim a CRC was checked. This is what `ubertooth-rx -q` does on every
 * packet until it finds the address part.
 */
static void test_bredr_uap_search(void)
{
	const char *filename = "test_capture_search.pcapng";
	char header_symbols[55];
	char payload[sizeof(SPEC_DH1_PAYLOAD)];
	char stream[1024];
	int pass;

	strcpy(header_symbols, SPEC_DH1_HEADER);

	/*
	 * Twice: once with the specification's DH1, whose CRC comes out and
	 * so settles the address part, and once with a payload byte altered,
	 * where every candidate is rejected and the search goes on.
	 */
	for (pass = 0; pass < 2; pass++) {
		btbb_pcapng_handle *h = NULL;
		btbb_piconet *pn;
		btbb_packet *pkt = NULL;
		size_t blk, data;
		int length, found;

		unlink_quietly(filename);
		strcpy(payload, SPEC_DH1_PAYLOAD);
		if (pass == 1) {
			CHECK(payload[8] == '1');
			payload[8] = '0';
		}
		length = build_bredr_stream(stream, SPEC_DH1_LAP,
					    header_symbols, payload);

		/* the LAP is known, the address part is not: the state
		   btbb_process_packet() is in before it starts following */
		pn = btbb_piconet_new();
		CHECK(pn != NULL);
		if (pn == NULL)
			return;
		btbb_init_piconet(pn, SPEC_DH1_LAP);
		CHECK(btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) == 0);
		CHECK(pkt != NULL);
		if (pkt == NULL) {
			btbb_piconet_unref(pn);
			return;
		}
		btbb_packet_set_data(pkt, stream, length, 0, 0);
		btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
		CHECK(btbb_process_packet(pkt, pn) == 0);
		found = btbb_piconet_get_flag(pn, BTBB_UAP_VALID);
		CHECK(found == (pass == 0));
		/* the search decoded no payload of its own, whatever it left
		   in the packet from a candidate it tried */
		CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 0);
		/* the CRC answer stays on the packet only when it is the one
		   that settled the address part */
		CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED) == found);
		CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == found);
		/* the header is only decoded once the clock is known, so the
		   HEC is not checked either way */
		CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED) == 0);

		CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
		if (h == NULL) {
			btbb_packet_unref(pkt);
			btbb_piconet_unref(pn);
			return;
		}
		/* the reference UAP a capture tool passes is the piconet's,
		   which is only known once the search has settled it */
		CHECK(btbb_pcapng_append_packet(h, 17ull, -20, -80,
						SPEC_DH1_LAP,
						found ? btbb_piconet_get_uap(pn)
						      : UAP_ANY,
						pkt) == 0);
		(void) btbb_pcapng_close(h);
		btbb_packet_unref(pkt);
		btbb_piconet_unref(pn);

		CHECK(read_capture(filename));
		unlink_quietly(filename);
		if (capture_len == 0)
			return;
		blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
		CHECK(blk < capture_len);
		if (blk >= capture_len)
			return;
		data = blk + 28;
		CHECK((rd16(data + 20) &
		       (BREDR_HEC_CHECKED | BREDR_HEC_VALID)) == 0);
		if (found) {
			CHECK((rd16(data + 20) &
			       (BREDR_CRC_CHECKED | BREDR_CRC_VALID)) ==
			      (BREDR_CRC_CHECKED | BREDR_CRC_VALID));
		}
		else {
			CHECK((rd16(data + 20) &
			       (BREDR_CRC_CHECKED | BREDR_CRC_VALID)) == 0);
		}
		/* no payload of the search's own, so none in the record */
		CHECK((rd16(data + 20) & BREDR_PAYLOAD_PRESENT) == 0);
		CHECK(rd32(blk + 20) == 22);
	}
}

/*
 * btbb_packet_set_data() replaces a packet's symbols and resets none of its
 * decoded state, so a caller that hands one packet a second frame must not
 * get the first frame's results or payload recorded for it.
 */
static void test_bredr_reused_packet(void)
{
	const char *filename = "test_capture_reuse.pcapng";
	const char *pcap_name = "test_capture_reuse.pcap";
	char header_symbols[55];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_pcap_handle *ph = NULL;
	btbb_packet *pkt = NULL;
	size_t blk, data;
	int length;

	unlink_quietly(filename);
	strcpy(header_symbols, SPEC_DH1_HEADER);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	CHECK(btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) == 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_data(pkt, stream, length, 0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_uap(pkt, SPEC_DH1_UAP);
	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT));

	/* the same packet, handed a capture too short to hold a header */
	btbb_packet_set_data(pkt, stream, 121, 0, 0);
	CHECK(btbb_decode_header(pkt) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == 0);

	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	CHECK(btbb_pcapng_append_packet(h, 19ull, -20, -80, SPEC_DH1_LAP,
					SPEC_DH1_UAP, pkt) == 0);
	(void) btbb_pcapng_close(h);

	unlink_quietly(pcap_name);
	CHECK(btbb_pcap_create_file(pcap_name, &ph) == 0);
	if (ph != NULL) {
		CHECK(btbb_pcap_append_packet(ph, 19ull, -20, -80,
					      SPEC_DH1_LAP, SPEC_DH1_UAP,
					      pkt) == 0);
		CHECK(btbb_pcap_close(ph) == 0);
	}
	btbb_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	data = blk + 28;
	CHECK((rd16(data + 20) &
	       (BREDR_HEC_CHECKED | BREDR_HEC_VALID |
		BREDR_CRC_CHECKED | BREDR_CRC_VALID)) == 0);
	/* and the earlier packet's payload is not in it either */
	CHECK((rd16(data + 20) & BREDR_PAYLOAD_PRESENT) == 0);
	CHECK(rd32(blk + 20) == 22);
	CHECK(rd32(blk + 4) == 60);

	if (ph != NULL) {
		CHECK(read_capture(pcap_name));
		unlink_quietly(pcap_name);
		if (capture_len != 0) {
			CHECK(rd32(24 + 8) == 22);
			CHECK((rd16(24 + 16 + 20) &
			       (BREDR_HEC_CHECKED | BREDR_HEC_VALID |
				BREDR_CRC_CHECKED | BREDR_CRC_VALID |
				BREDR_PAYLOAD_PRESENT)) == 0);
			CHECK(capture_len == 24 + 16 + 22);
		}
	}
}

/*
 * A payload the decoder could not read is not reported as one. The flag the
 * record writers and btbb_print_packet() read used to be set on every decode,
 * so a packet whose payload did not decode was reported with whatever the
 * last packet decoded into the same object had left behind.
 */
static void test_bredr_undecoded_payload_is_not_reported(void)
{
	char header_symbols[55];
	char stream[1024];
	btbb_packet *pkt = NULL;
	int length, i;
	int payload_is_clear = 1;

	strcpy(header_symbols, SPEC_DH1_HEADER);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	CHECK(btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) == 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_uap(pkt, SPEC_DH1_UAP);

	/* the whole packet first, so the object holds a decoded payload */
	btbb_packet_set_data(pkt, stream, length, 0, 0);
	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 1);
	CHECK(btbb_packet_get_payload_length(pkt) == 8);

	/* the same object handed a capture that ends two bytes into the
	   payload: the header still decodes, the payload cannot */
	btbb_packet_set_data(pkt, stream, 122 + 16, 0, 0);
	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_decode_payload(pkt) == 1);
	CHECK(btbb_packet_get_payload_length(pkt) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 0);

	/* and the bytes of the first decode are no longer in the field */
	for (i = 0; i < 8 * 8; i++) {
		if (pkt->payload[i] != 0)
			payload_is_clear = 0;
	}
	CHECK(payload_is_clear);

	btbb_packet_unref(pkt);
}

/*
 * A packet object reused for a second frame, where the second frame goes down
 * the search for the piconet's address part. That search reads the header
 * through try_clock(), which records nothing on the packet, so none of the
 * first frame's decoded header - the bits, the HEC answer, the count of
 * corrected header bits - may reach the second frame's record.
 */
static void test_bredr_reused_packet_in_the_uap_search(void)
{
	const char *filename = "test_capture_reuse_search.pcapng";
	char header_symbols[55];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_piconet *pn;
	btbb_packet *pkt = NULL;
	size_t blk, data;
	int length;

	/* Frame A: the specification's DH1 with three wrong header symbols,
	   which the rate 1/3 code puts right, decoded in full. */
	strcpy(header_symbols, SPEC_DH1_HEADER);
	header_symbols[0] = (char) ('0' + '1' - header_symbols[0]);
	header_symbols[4] = (char) ('0' + '1' - header_symbols[4]);
	header_symbols[8] = (char) ('0' + '1' - header_symbols[8]);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	CHECK(btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) == 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_data(pkt, stream, length, 0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_uap(pkt, SPEC_DH1_UAP);
	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(pkt->corrected_header_bits == 3);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID) == 1);

	/* Frame B in the same object, with a clean header, through the search
	   for the address part: the state btbb_process_packet() is in before
	   it starts following. */
	strcpy(header_symbols, SPEC_DH1_HEADER);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	btbb_packet_set_data(pkt, stream, length, 0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	pn = btbb_piconet_new();
	CHECK(pn != NULL);
	if (pn == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	btbb_init_piconet(pn, SPEC_DH1_LAP);
	CHECK(btbb_process_packet(pkt, pn) == 0);
	CHECK(btbb_piconet_get_flag(pn, BTBB_UAP_VALID) == 1);

	/* none of frame A's header survives on frame B */
	CHECK(pkt->corrected_header_bits == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID) == 0);
	CHECK(btbb_packet_get_header_packed(pkt) == 0);

	unlink_quietly(filename);
	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		btbb_piconet_unref(pn);
		return;
	}
	CHECK(btbb_pcapng_append_packet(h, 29ull, -20, -80, SPEC_DH1_LAP,
					btbb_piconet_get_uap(pn), pkt) == 0);
	(void) btbb_pcapng_close(h);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	data = blk + 28;
	CHECK(capture[data + 5] == 0);			/* corrected header bits */
	CHECK(rd16(data + 6) == 0);			/* corrected payload bits */
	CHECK(rd32(data + 16) == 0);			/* bt header */
	CHECK((rd16(data + 20) &
	       (BREDR_HEC_CHECKED | BREDR_HEC_VALID)) == 0);
}

/*
 * A payload decoded before the search for the address part starts does not
 * survive a search that found nothing. The search decodes the same symbols
 * again at up to 64 candidate clocks and throws each result away, so what is
 * left in the payload field is the last clock tried.
 *
 * A search that succeeds is the other way round: it stops on the clock whose
 * CRC came out, so the payload left behind is the packet's own and stays.
 */
static void test_bredr_payload_does_not_survive_the_uap_search(void)
{
	const char *filename = "test_capture_search_payload.pcapng";
	char header_symbols[55];
	char payload[sizeof(SPEC_DH1_PAYLOAD)];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	btbb_piconet *pn;
	btbb_packet *pkt = NULL;
	size_t blk, data;
	int length;

	/* a DH1 with one payload byte altered, so no clock's CRC comes out
	   and the search runs every candidate */
	strcpy(header_symbols, SPEC_DH1_HEADER);
	strcpy(payload, SPEC_DH1_PAYLOAD);
	CHECK(payload[8] == '1');
	payload[8] = '0';
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    payload);
	CHECK(btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) == 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_data(pkt, stream, length, 0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_uap(pkt, SPEC_DH1_UAP);
	CHECK(btbb_decode_header(pkt) == 1);
	/* 2 is a payload decoded whose CRC did not come out */
	CHECK(btbb_decode_payload(pkt) == 2);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 1);
	CHECK(btbb_packet_get_payload_length(pkt) == 8);

	/* the search over the same symbols, as btbb_process_packet() runs it
	   before a piconet's address part is known */
	pn = btbb_piconet_new();
	CHECK(pn != NULL);
	if (pn == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	btbb_init_piconet(pn, SPEC_DH1_LAP);
	(void) btbb_uap_from_header(pkt, pn);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 0);
	CHECK(btbb_packet_get_payload_length(pkt) == 0);

	unlink_quietly(filename);
	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		btbb_piconet_unref(pn);
		return;
	}
	CHECK(btbb_pcapng_append_packet(h, 31ull, -20, -80, SPEC_DH1_LAP,
					SPEC_DH1_UAP, pkt) == 0);
	(void) btbb_pcapng_close(h);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	data = blk + 28;
	CHECK((rd16(data + 20) & BREDR_PAYLOAD_PRESENT) == 0);
	CHECK(rd16(data + 6) == 0);		/* corrected payload bits */
	CHECK(rd32(blk + 20) == 22);		/* the pseudoheader alone */
}

/*
 * The corrected-bits fields of a BR/EDR record. The format gives neither a
 * validity flag, so a reader takes both at face value, and both writers used
 * to fill them with 0 whatever the forward error correction had done.
 *
 * An HV1 is the case where both fields are interesting at once: its header
 * and its payload are both carried by the rate 1/3 code, which corrects any
 * single wrong symbol of a triple.
 */
static void test_bredr_corrected_bits(void)
{
	const char *filename = "test_capture_corrected.pcapng";
	const char *pcap_name = "test_capture_corrected.pcap";
	/* LT_ADDR 1, TYPE 5 (HV1) */
	const uint16_t header_data = (uint16_t) (1 | (5 << 3));
	/* the payload bytes an HV1 carries: 10, with no CRC */
	static const uint8_t HV1_BYTES[10] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0xf6, 0xa7, 0x18, 0x29, 0xbb
	};
	char header_symbols[55];
	char payload[241];
	char stream[1024];
	btbb_packet *pkt;
	size_t blk, data;
	int hec, length, byte, bit, i, pass;

	hec = hec_for(header_data, SPEC_DH1_UAP);
	CHECK(hec >= 0);
	if (hec < 0)
		return;

	/* each payload bit sent three times, least significant bit first */
	for (byte = 0; byte < 10; byte++) {
		for (bit = 0; bit < 8; bit++) {
			char value = (char)
				('0' + ((HV1_BYTES[byte] >> bit) & 1));
			int at = (byte * 8 + bit) * 3;

			payload[at] = value;
			payload[at + 1] = value;
			payload[at + 2] = value;
		}
	}
	payload[240] = '\0';

	/*
	 * Twice: the packet as sent, and the same packet with one symbol of
	 * the header and one of the payload wrong. The majority vote puts
	 * both back, so the record carries the same payload either way and
	 * only the counts differ.
	 */
	for (pass = 0; pass < 2; pass++) {
		btbb_pcapng_handle *h = NULL;
		btbb_pcap_handle *ph = NULL;
		const uint8_t expected = pass == 0 ? 0 : 1;

		encode_header_13(header_data, (uint8_t) hec, header_symbols);
		if (pass == 1) {
			header_symbols[1] = (char)
				('0' + '1' - header_symbols[1]);
			payload[4] = (char) ('0' + '1' - payload[4]);
		}
		length = build_bredr_stream(stream, SPEC_DH1_LAP,
					    header_symbols, payload);
		CHECK(length == 122 + 240);
		pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_packet_get_type(pkt) == 5);		/* HV1 */
		CHECK(btbb_packet_get_payload_length(pkt) == 10);
		CHECK(pkt->corrected_header_bits == expected);
		CHECK(pkt->corrected_payload_bits == expected);

		unlink_quietly(filename);
		CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
		if (h != NULL) {
			CHECK(btbb_pcapng_append_packet(h, 23ull, -20, -80,
							SPEC_DH1_LAP,
							SPEC_DH1_UAP,
							pkt) == 0);
			(void) btbb_pcapng_close(h);
		}
		unlink_quietly(pcap_name);
		CHECK(btbb_pcap_create_file(pcap_name, &ph) == 0);
		if (ph != NULL) {
			CHECK(btbb_pcap_append_packet(ph, 23ull, -20, -80,
						      SPEC_DH1_LAP,
						      SPEC_DH1_UAP,
						      pkt) == 0);
			CHECK(btbb_pcap_close(ph) == 0);
		}
		btbb_packet_unref(pkt);

		/* the pcapng record */
		CHECK(read_capture(filename));
		unlink_quietly(filename);
		if (capture_len == 0)
			return;
		blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
		CHECK(blk < capture_len);
		if (blk >= capture_len)
			return;
		data = blk + 28;
		CHECK(capture[data + 5] == expected);
		CHECK(rd16(data + 6) == expected);
		CHECK((rd16(data + 20) & BREDR_PAYLOAD_PRESENT) != 0);
		for (i = 0; i < 10; i++)
			CHECK(capture[data + 22 + i] == HV1_BYTES[i]);

		/* and the plain pcap record, which has the same layout after
		   its 16-byte record header */
		if (ph != NULL) {
			CHECK(read_capture(pcap_name));
			unlink_quietly(pcap_name);
			if (capture_len == 0)
				return;
			data = 24 + 16;
			CHECK(capture[data + 5] == expected);
			CHECK(rd16(data + 6) == expected);
			for (i = 0; i < 10; i++)
				CHECK(capture[data + 22 + i] ==
				      HV1_BYTES[i]);
		}
	}
}

/*
 * The BD_ADDR and clock options a BR/EDR capture records, and what happens
 * when a capture records more options than the space set aside for them.
 */
static void test_bredr_interface_options(void)
{
	const char *filename = "test_capture_options.pcapng";
	btbb_pcapng_handle *h = NULL;
	size_t idb, value, off, end;
	uint16_t value_length = 0;
	int i, refused = 0;

	unlink_quietly(filename);
	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL)
		return;
	CHECK(btbb_pcapng_record_bdaddr(h, 0x112233445566ull, 0xff, 1) == 0);
	CHECK(btbb_pcapng_record_btclock(h, 0x112233445566ull, 13ull,
					 0x0abbccdd, 0x0fffffff) == 0);

	/* Fill the space set aside for interface options. The extra ones must
	   be refused, not written past the end of the header block. */
	for (i = 0; i < 100000; i++) {
		if (btbb_pcapng_record_bdaddr(h, (uint64_t) i, 0xff, 0) != 0) {
			refused = 1;
			break;
		}
	}
	CHECK(refused);
	(void) btbb_pcapng_close(h);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	idb = find_block(BLOCK_TYPE_INTERFACE, 0);
	CHECK(idb < capture_len);
	if (idb >= capture_len)
		return;
	CHECK(rd16(idb + 8) == DLT_BLUETOOTH_BREDR_BB);
	CHECK(rd32(idb + 12) == sizeof(pcap_bluetooth_bredr_bb_header));

	/* a BD_ADDR option: six address bytes, the UAP mask and the NAP flag */
	value = find_option(idb, 16, PCAPNG_BREDR_OPTION_BD_ADDR,
			    &value_length);
	CHECK(value != 0);
	if (value != 0) {
		CHECK(value_length == sizeof(bredr_bd_addr_info));
		CHECK(value_length == 8);
		CHECK(capture[value + 0] == 0x66);
		CHECK(capture[value + 5] == 0x11);
		CHECK(capture[value + 6] == 0xff);
		CHECK(capture[value + 7] == 1);
	}

	/* a clock option: a timestamp, the address, the clock and its mask */
	value = find_option(idb, 16, PCAPNG_BREDR_OPTION_MASTER_CLOCK_INFO,
			    &value_length);
	CHECK(value != 0);
	if (value != 0) {
		CHECK(value_length == sizeof(bredr_clk_info));
		CHECK(value_length == 20);
		CHECK(rd32(value + 0) == 13);
		CHECK(rd32(value + 8) == 0x33445566u);
		CHECK(rd32(value + 12) == 0x0abbccddu);
		CHECK(rd32(value + 16) == 0x0fffffffu);
	}

	/* the options still cover the block exactly */
	off = idb + 16;
	end = idb + rd32(idb + 4) - 4;
	while (off + 4 <= end) {
		uint16_t len = rd16(off + 2);

		if (rd16(off) == OPT_ENDOFOPT)
			break;
		off += 4 + 4 * ((len + 3u) / 4u);
		CHECK(off <= end);
		if (off > end)
			break;
	}
	CHECK(off + 4 >= end);
}

/* The same record in a plain pcap file. */
static void test_bredr_pcap_record(void)
{
	const char *filename = "test_capture_bredr.pcap";
	char header_symbols[55];
	char stream[1024];
	btbb_pcap_handle *h = NULL;
	btbb_packet *pkt;
	size_t rec, data;
	int length, i;
	uint16_t expected_flags;

	unlink_quietly(filename);
	strcpy(header_symbols, SPEC_DH1_HEADER);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	CHECK(btbb_pcap_create_file(filename, &h) == 0);
	if (h == NULL) {
		btbb_packet_unref(pkt);
		return;
	}
	CHECK(btbb_pcap_append_packet(h, 1000000007ull, -20, -80,
				      SPEC_DH1_LAP, SPEC_DH1_UAP, pkt) == 0);
	CHECK(btbb_pcap_close(h) == 0);
	btbb_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	/* 24-byte file header: the nanosecond-resolution magic, so the second
	   timestamp word counts nanoseconds, then version 2.4 and the link
	   type. */
	CHECK(rd32(0) == 0xa1b23c4du);
	CHECK(rd16(4) == 2);
	CHECK(rd16(6) == 4);
	/* the snapshot length is the longest record the writer can emit, so a
	   reader sizing a buffer from it has room for every record */
	CHECK(rd32(16) == sizeof(pcap_bluetooth_bredr_bb_header));
	CHECK(rd32(16) == 422);
	CHECK(rd32(20) == DLT_BLUETOOTH_BREDR_BB);

	rec = 24;
	CHECK(rd32(rec + 0) == 1);			/* seconds */
	CHECK(rd32(rec + 4) == 7);			/* fractional */
	CHECK(rd32(rec + 8) == 30);			/* included length */
	CHECK(rd32(rec + 12) == 30);			/* original length */

	data = rec + 16;
	CHECK(capture[data + 0] == 0);
	CHECK((int8_t) capture[data + 1] == -20);
	CHECK((int8_t) capture[data + 2] == -80);
	CHECK(rd32(data + 8) == SPEC_DH1_LAP);
	CHECK(rd32(data + 12) ==
	      (SPEC_DH1_LAP | ((uint32_t) SPEC_DH1_UAP << 24)));

	expected_flags = BREDR_DEWHITENED | BREDR_SIGPOWER_VALID |
		BREDR_NOISEPOWER_VALID | BREDR_REFLAP_VALID |
		BREDR_REFUAP_VALID | BREDR_PAYLOAD_PRESENT |
		BREDR_HEC_CHECKED | BREDR_HEC_VALID |
		BREDR_CRC_CHECKED | BREDR_CRC_VALID;
	CHECK(rd16(data + 20) == expected_flags);

	for (i = 0; i < 8; i++)
		CHECK(capture[data + 22 + i] == SPEC_DH1_BYTES[i]);

	/* the record is the only thing in the file */
	CHECK(capture_len == 24 + 16 + 30);
}

/* An LE packet in a plain pcap file, and the PPI form of the same file. */
static void test_le_pcap_records(void)
{
	const char *filename = "test_capture_le.pcap";
	const char *ppi_name = "test_capture_le_ppi.pcap";
	lell_pcap_handle *h = NULL;
	lell_packet *pkt = NULL;
	int i;

	lell_allocate_and_decode_n(CONNECT_IND, sizeof(CONNECT_IND),
				   CONNECT_IND_CHANNEL_MHZ, 42, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	unlink_quietly(filename);
	CHECK(lell_pcap_create_file(filename, &h) == 0);
	if (h != NULL) {
		CHECK(lell_pcap_append_packet(h, 2000000003ull, -20, -80,
					      LE_ADV_AA, pkt) == 0);
		CHECK(lell_pcap_close(h) == 0);
		CHECK(read_capture(filename));
		unlink_quietly(filename);
		if (capture_len != 0) {
			/* the snapshot length is the longest record this
			   writer produces: the pseudoheader and a full frame */
			CHECK(rd32(16) ==
			      sizeof(pcap_bluetooth_le_ll_header) +
			      LE_MAX_PAYLOAD);
			CHECK(rd32(16) == 270);
			CHECK(rd32(20) == DLT_BLUETOOTH_LE_LL_WITH_PHDR);
			CHECK(rd32(24) == 2);			/* seconds */
			CHECK(rd32(28) == 3);			/* nanoseconds */
			CHECK(rd32(32) == 10 + sizeof(CONNECT_IND));
			CHECK(rd32(36) == 10 + sizeof(CONNECT_IND));
			CHECK(capture[40] == 0);		/* channel k */
			CHECK((int8_t) capture[41] == -20);
			CHECK((int8_t) capture[42] == -80);
			CHECK(capture[43] == 0);		/* AA offenses */
			CHECK(rd32(44) == LE_ADV_AA);
			CHECK(rd16(48) ==
			      (LE_DEWHITENED | LE_AA_OFFENSES_VALID |
			       LE_SIGPOWER_VALID | LE_NOISEPOWER_VALID |
			       LE_REF_AA_VALID));
			for (i = 0; i < (int) sizeof(CONNECT_IND); i++)
				CHECK(capture[50 + i] == CONNECT_IND[i]);
			CHECK(capture_len == 24 + 16 + 10 +
			      sizeof(CONNECT_IND));
		}
	}

	/* A packet on a data channel. The writers clear the reference access
	   address flag for one of those, which is the half of the LE flag
	   word the advertising cases above cannot show. */
	{
		const char *data_name = "test_capture_le_data.pcap";
		static const uint8_t DATA_PDU[] = {
			0x78, 0x56, 0x34, 0x12,		/* access address */
			0x02, 0x04,			/* LLID 2, length 4 */
			0x01, 0x02, 0x03, 0x04,		/* payload */
			0xaa, 0xbb, 0xcc		/* CRC */
		};
		lell_pcap_handle *dh = NULL;
		lell_packet *dpkt = NULL;

		/* 2404 MHz is channel index 0, a data channel */
		lell_allocate_and_decode_n(DATA_PDU, sizeof(DATA_PDU), 2404,
					   7, &dpkt);
		CHECK(dpkt != NULL);
		if (dpkt != NULL) {
			CHECK(lell_packet_is_data(dpkt));
			unlink_quietly(data_name);
			CHECK(lell_pcap_create_file(data_name, &dh) == 0);
			if (dh != NULL) {
				CHECK(lell_pcap_append_packet(dh, 5ull, -20,
							      -80, 0x12345678,
							      dpkt) == 0);
				CHECK(lell_pcap_close(dh) == 0);
				CHECK(read_capture(data_name));
				unlink_quietly(data_name);
				if (capture_len != 0) {
					CHECK((rd16(48) &
					       LE_REF_AA_VALID) == 0);
					CHECK(rd32(44) == 0x12345678u);
				}
			}
			lell_packet_unref(dpkt);
		}
	}

	/* the PPI form carries a larger per-packet header, so its snapshot
	   length is larger too */
	h = NULL;
	unlink_quietly(ppi_name);
	CHECK(lell_pcap_ppi_create_file(ppi_name, 2, &h) == 0);
	if (h != NULL) {
		CHECK(lell_pcap_append_ppi_packet(h, 2000000003ull, 0, -80,
						  -20, -40, 3, pkt) == 0);
		CHECK(lell_pcap_close(h) == 0);
		CHECK(read_capture(ppi_name));
		unlink_quietly(ppi_name);
		if (capture_len != 0) {
			CHECK(rd32(16) == 24 + LE_MAX_PAYLOAD);
			CHECK(rd32(16) == 284);
			CHECK(rd32(20) == DLT_PPI);
			CHECK(rd32(32) == 24 + sizeof(CONNECT_IND));
			CHECK(capture_len == 24 + 16 + 24 +
			      sizeof(CONNECT_IND));
		}
	}

	lell_packet_unref(pkt);
}

/*
 * The CONNECT_IND option, field by field. Every connection parameter must
 * come from the PDU's payload, which starts after the access address and the
 * PDU header.
 */
static void test_le_connect_ind_option(void)
{
	const char *filename = "test_capture_le.pcapng";
	lell_pcapng_handle *h = NULL;
	lell_packet *pkt = NULL;
	size_t idb, blk, value, data;
	uint16_t value_length = 0;
	int i;

	unlink_quietly(filename);
	lell_allocate_and_decode_n(CONNECT_IND, sizeof(CONNECT_IND),
				   CONNECT_IND_CHANNEL_MHZ, 42, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(lell_get_payload_length(pkt) == LE_CONNECT_IND_PDU_LENGTH);

	CHECK(lell_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		lell_packet_unref(pkt);
		return;
	}
	dirty_stack();
	CHECK(lell_pcapng_append_packet(h, 1000000007ull, -20, -80, LE_ADV_AA,
					pkt) == 0);
	(void) lell_pcapng_close(h);
	lell_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	/* The connection parameters are recorded as an interface option. */
	idb = find_block(BLOCK_TYPE_INTERFACE, 0);
	CHECK(idb < capture_len);
	if (idb >= capture_len)
		return;
	CHECK(rd16(idb + 8) == DLT_BLUETOOTH_LE_LL_WITH_PHDR);
	CHECK(rd32(idb + 12) ==
	      sizeof(pcap_bluetooth_le_ll_header) + LE_MAX_PAYLOAD);

	/* an interface description block's options start after its 16-byte
	   fixed part */
	value = find_option(idb, 16, PCAPNG_LE_LL_CONNECTION_INFO,
			    &value_length);
	CHECK(value != 0);
	if (value == 0)
		return;
	/* an option's length is the length of its value, so the timestamp and
	   the 34 payload bytes, and not the 4-byte option header */
	CHECK(value_length == sizeof(le_ll_connection_info));
	CHECK(value_length == 8 + LE_CONNECT_IND_PDU_LENGTH);

	/* the timestamp, then the 34 payload bytes of the PDU */
	CHECK(rd32(value) == 1000000007u);
	CHECK(rd32(value + 4) == 0);
	for (i = 0; i < LE_CONNECT_IND_PDU_LENGTH; i++)
		CHECK(capture[value + 8 + i] == CONNECT_IND[6 + i]);
	/* a 42-byte value is rounded up to 44, so two bytes of option padding
	   follow it, and they have to be zero like any other padding */
	CHECK(all_zero(value + 42, 2));

	/* the fields a reader needs to follow the connection, at the offsets
	   the option gives them */
	CHECK(rd32(value + 8 + 12) == 0x12345678u);	/* AA */
	CHECK(capture[value + 8 + 19] == 0x05);		/* WinSize */
	CHECK(rd16(value + 8 + 22) == 0x0018);		/* Interval */
	CHECK(rd16(value + 8 + 26) == 0x0048);		/* Timeout */
	CHECK(capture[value + 8 + 33] == 0xc7);		/* HopSCA */
	CHECK((capture[value + 8 + 33] & 0x1f) == 7);	/* hop increment */

	/* the packet itself, in an enhanced packet block */
	blk = find_block(BLOCK_TYPE_ENHANCED_PACKET, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;

	/* 10 bytes of pseudoheader and the 43 captured bytes, rounded up
	   from 81 to 84 */
	CHECK(rd32(blk + 4) == 92);
	CHECK(rd32(blk + 20) == 53);
	CHECK(rd32(blk + 24) == 53);

	data = blk + 28;
	CHECK(capture[data + 0] == 0);			/* channel k */
	CHECK((int8_t) capture[data + 1] == -20);
	CHECK((int8_t) capture[data + 2] == -80);
	CHECK(capture[data + 3] == 0);			/* AA offenses */
	CHECK(rd32(data + 4) == LE_ADV_AA);
	CHECK(rd16(data + 8) ==
	      (LE_DEWHITENED | LE_AA_OFFENSES_VALID | LE_SIGPOWER_VALID |
	       LE_NOISEPOWER_VALID | LE_REF_AA_VALID));
	for (i = 0; i < (int) sizeof(CONNECT_IND); i++)
		CHECK(capture[data + 10 + i] == CONNECT_IND[i]);

	/* three bytes of padding before the options word */
	CHECK(all_zero(blk + 81, 3));
	CHECK(rd32(blk + 84) == 0);
	CHECK(rd32(blk + 88) == 92);
}

/* A CONNECT_IND cut short does not have 34 bytes to record, so none is. */
static void test_le_short_connect_ind(void)
{
	const char *filename = "test_capture_le_short.pcapng";
	uint8_t truncated[sizeof(CONNECT_IND)];
	lell_pcapng_handle *h = NULL;
	lell_packet *pkt = NULL;
	size_t idb;

	unlink_quietly(filename);
	memcpy(truncated, CONNECT_IND, sizeof(truncated));
	/* 20 payload bytes captured, where the PDU header still says 34 */
	lell_allocate_and_decode_n(truncated, 6 + 20 + 3,
				   CONNECT_IND_CHANNEL_MHZ, 42, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(lell_get_payload_length(pkt) == 20);

	CHECK(lell_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL) {
		lell_packet_unref(pkt);
		return;
	}
	CHECK(lell_pcapng_append_packet(h, 7ull, -20, -80, LE_ADV_AA,
					pkt) == 0);
	(void) lell_pcapng_close(h);
	lell_packet_unref(pkt);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	idb = find_block(BLOCK_TYPE_INTERFACE, 0);
	CHECK(idb < capture_len);
	if (idb >= capture_len)
		return;
	CHECK(find_option(idb, 16, PCAPNG_LE_LL_CONNECTION_INFO, NULL) == 0);
}

/*
 * The padding options that fill the spare room of the two header blocks must
 * cover it exactly, so a reader walking the options arrives at the end of
 * each block. A padding length is a 16-bit field, so a page larger than
 * 64 kB takes more than one option.
 */
static void test_header_padding_options(void)
{
	const char *filename = "test_capture_padding.pcapng";
	btbb_pcapng_handle *h = NULL;
	size_t blocks[2];
	int b;

	unlink_quietly(filename);
	CHECK(btbb_pcapng_create_file(filename, "test", &h) == 0);
	if (h == NULL)
		return;
	(void) btbb_pcapng_close(h);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	blocks[0] = find_block(BLOCK_TYPE_SECTION_HEADER, 0);
	blocks[1] = find_block(BLOCK_TYPE_INTERFACE, 0);
	CHECK(blocks[0] < capture_len);
	CHECK(blocks[1] < capture_len);
	if ((blocks[0] >= capture_len) || (blocks[1] >= capture_len))
		return;

	for (b = 0; b < 2; b++) {
		size_t blk = blocks[b];
		/* a section header's fixed part is 24 bytes, an interface
		   description block's is 16 */
		size_t off = blk + (b == 0 ? 24 : 16);
		size_t end = blk + rd32(blk + 4) - 4;
		int saw_padding = 0;

		while (off + 4 <= end) {
			uint16_t code = rd16(off);
			uint16_t len = rd16(off + 2);

			if (code == OPT_ENDOFOPT)
				break;
			if (code == 0xffff) {
				saw_padding = 1;
				CHECK((len % 4) == 0);
			}
			off += 4 + 4 * ((len + 3u) / 4u);
			CHECK(off <= end);
			if (off > end)
				break;
		}
		CHECK(saw_padding);
		/* the options either end exactly at the block's trailing
		   length or at an end-of-options word just before it */
		CHECK(off + 4 >= end);
	}
}

/*
 * More spare room than one 16-bit option length can cover, which is what a
 * 64 kB page gives. It has to be filled with more than one padding option,
 * or the length written is the real one truncated to 16 bits and the block is
 * malformed.
 */
static void test_padding_beyond_one_option(void)
{
	const char *filename = "test_capture_bigpad.pcapng";
	const size_t space = 80 * 1024;
	PCAPNG_HANDLE handle;
	size_t blk, off, end;
	int options = 0;

	unlink_quietly(filename);
	CHECK(pcapng_create(&handle, filename, NULL, space,
			    DLT_BLUETOOTH_BREDR_BB, BREDR_MAX_PAYLOAD, NULL,
			    64) == PCAPNG_OK);
	(void) pcapng_close(&handle);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;

	blk = find_block(BLOCK_TYPE_SECTION_HEADER, 0);
	CHECK(blk < capture_len);
	if (blk >= capture_len)
		return;
	CHECK(rd32(blk + 4) > space);

	off = blk + 24;
	end = blk + rd32(blk + 4) - 4;
	while (off + 4 <= end) {
		uint16_t code = rd16(off);
		uint16_t len = rd16(off + 2);

		if (code == OPT_ENDOFOPT)
			break;
		CHECK(code == 0xffff);
		CHECK(len <= 0xfffc);
		++options;
		off += 4 + 4 * ((len + 3u) / 4u);
		CHECK(off <= end);
		if (off > end)
			break;
	}
	/* 80 kB of spare room is more than one option can cover */
	CHECK(options >= 2);
	CHECK(off + 4 >= end);
}

/* A section option beyond the space set aside for it is refused, as an
   interface option is. */
static void test_section_option_limit(void)
{
	const char *filename = "test_capture_sectopt.pcapng";
	struct {
		option_header header;
		char text[64];
	} comment;
	PCAPNG_HANDLE handle;
	int i, refused = 0;

	memset(&comment, 0, sizeof(comment));
	comment.header.option_code = OPT_COMMENT;
	comment.header.option_length = sizeof(comment.text);

	unlink_quietly(filename);
	CHECK(pcapng_create(&handle, filename, NULL, 64,
			    DLT_BLUETOOTH_BREDR_BB, BREDR_MAX_PAYLOAD, NULL,
			    64) == PCAPNG_OK);
	for (i = 0; i < 100000; i++) {
		if (pcapng_append_section_option(&handle,
						 (const option_header *)
						 &comment) != PCAPNG_OK) {
			refused = 1;
			break;
		}
	}
	CHECK(refused);
	(void) pcapng_close(&handle);

	CHECK(read_capture(filename));
	unlink_quietly(filename);
	if (capture_len == 0)
		return;
	/* the block still walks to its end, so a reader can parse it */
	{
		size_t blk = find_block(BLOCK_TYPE_SECTION_HEADER, 0);
		size_t off, end;

		CHECK(blk < capture_len);
		if (blk >= capture_len)
			return;
		off = blk + 24;
		end = blk + rd32(blk + 4) - 4;
		while (off + 4 <= end) {
			uint16_t len = rd16(off + 2);

			if (rd16(off) == OPT_ENDOFOPT)
				break;
			off += 4 + 4 * ((len + 3u) / 4u);
			CHECK(off <= end);
			if (off > end)
				break;
		}
		CHECK(off + 4 >= end);
	}
}

/*
 * A write that cannot be completed must be reported, and only the bytes that
 * reached the file may be counted into the section length.
 */
static void test_write_errors(void)
{
	const char *closed = "test_capture_closed.pcapng";
	const char *full = "test_capture_full.pcapng";
	char header_symbols[55];
	char stream[1024];
	btbb_pcapng_handle *h = NULL;
	PCAPNG_HANDLE *raw;
	btbb_packet *pkt;
	struct rlimit saved, limited;
	struct stat before, after;
	uint64_t section_length_before;
	int length;

	strcpy(header_symbols, SPEC_DH1_HEADER);
	length = build_bredr_stream(stream, SPEC_DH1_LAP, header_symbols,
				    SPEC_DH1_PAYLOAD);
	pkt = decode_bredr(stream, length, SPEC_DH1_UAP);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	/* a descriptor that is no longer open */
	unlink_quietly(closed);
	CHECK(btbb_pcapng_create_file(closed, "test", &h) == 0);
	if (h != NULL) {
		raw = (PCAPNG_HANDLE *) h;
		section_length_before = raw->section_header->section_length;
		CHECK(close(raw->fd) == 0);
		CHECK(btbb_pcapng_append_packet(h, 1ull, -20, -80,
						SPEC_DH1_LAP, SPEC_DH1_UAP,
						pkt) ==
		      -PCAPNG_FILE_WRITE_ERROR);
		CHECK(raw->section_header->section_length ==
		      section_length_before);
		raw->fd = -1;
		(void) btbb_pcapng_close(h);
		unlink_quietly(closed);
	}

	/* a file that has reached the largest size this process may write */
	h = NULL;
	unlink_quietly(full);
	CHECK(btbb_pcapng_create_file(full, "test", &h) == 0);
	if ((h != NULL) && (getrlimit(RLIMIT_FSIZE, &saved) == 0)) {
		raw = (PCAPNG_HANDLE *) h;
		section_length_before = raw->section_header->section_length;
		CHECK(fstat(raw->fd, &before) == 0);

		limited = saved;
		/* room for part of the 68-byte block, but not all of it */
		limited.rlim_cur = (rlim_t) before.st_size + 40;
		if (setrlimit(RLIMIT_FSIZE, &limited) == 0) {
			/* the kernel raises SIGXFSZ as well as failing the
			   write, and the default action would end the test */
			void (*old_xfsz)(int) = signal(SIGXFSZ, SIG_IGN);

			CHECK(btbb_pcapng_append_packet(h, 1ull, -20, -80,
							SPEC_DH1_LAP,
							SPEC_DH1_UAP, pkt) ==
			      -PCAPNG_FILE_WRITE_ERROR);
			/* the part of the block that did reach the file is
			   taken back out, and nothing is counted */
			CHECK(fstat(raw->fd, &after) == 0);
			CHECK(after.st_size == before.st_size);
			CHECK(raw->section_header->section_length ==
			      section_length_before);
			CHECK(setrlimit(RLIMIT_FSIZE, &saved) == 0);
			(void) signal(SIGXFSZ, old_xfsz);
		}
		(void) btbb_pcapng_close(h);
		unlink_quietly(full);
	}

	/* The plain pcap writer reports it too, and leaves the file ending on
	   a whole record, as the pcapng writer does with a block. */
	if (getrlimit(RLIMIT_FSIZE, &saved) == 0) {
		const char *small = "test_capture_small.pcap";

		limited = saved;
		/* room for the 24-byte file header and one 46-byte record,
		   and part of a second: the write that fails is one stdio
		   has already put a prefix of on disk */
		limited.rlim_cur = 100;
		if (setrlimit(RLIMIT_FSIZE, &limited) == 0) {
			void (*old_xfsz)(int) = signal(SIGXFSZ, SIG_IGN);
			btbb_pcap_handle *ph = NULL;
			int i, written = 0, failed = 0;

			unlink_quietly(small);
			CHECK(btbb_pcap_create_file(small, &ph) == 0);
			for (i = 0; (ph != NULL) && (i < 100); i++) {
				if (btbb_pcap_append_packet(ph, 1ull, -20, -80,
							    SPEC_DH1_LAP,
							    SPEC_DH1_UAP,
							    pkt) != 0) {
					failed = 1;
					break;
				}
				written++;
			}
			CHECK(failed);
			CHECK(written > 0);
			if (ph != NULL) {
				(void) btbb_pcap_close(ph);
			}
			/* Nothing of the record that failed is in the file:
			   the length is the 24-byte file header and the
			   46-byte records that were written whole. The size
			   is checked as well as the record walk, so a
			   platform that refused the over-limit write whole
			   rather than part way through could not pass this
			   without the rollback. */
			CHECK(stat(small, &after) == 0);
			CHECK(after.st_size == (off_t) (24 + written * 46));
			CHECK(read_capture(small));
			CHECK(count_pcap_records() == written);
			unlink_quietly(small);
			CHECK(setrlimit(RLIMIT_FSIZE, &saved) == 0);
			(void) signal(SIGXFSZ, old_xfsz);
		}
	}

	btbb_packet_unref(pkt);

	/* the LE writer reports the same way */
	{
		const char *le_name = "test_capture_closed_le.pcapng";
		lell_pcapng_handle *lh = NULL;
		lell_packet *lpkt = NULL;

		lell_allocate_and_decode_n(CONNECT_IND, sizeof(CONNECT_IND),
					   CONNECT_IND_CHANNEL_MHZ, 42, &lpkt);
		CHECK(lpkt != NULL);
		if (lpkt == NULL)
			return;
		unlink_quietly(le_name);
		CHECK(lell_pcapng_create_file(le_name, "test", &lh) == 0);
		if (lh != NULL) {
			raw = (PCAPNG_HANDLE *) lh;
			section_length_before =
				raw->section_header->section_length;
			CHECK(close(raw->fd) == 0);
			CHECK(lell_pcapng_append_packet(lh, 1ull, -20, -80,
							LE_ADV_AA, lpkt) ==
			      -PCAPNG_FILE_WRITE_ERROR);
			CHECK(raw->section_header->section_length ==
			      section_length_before);
			raw->fd = -1;
			(void) lell_pcapng_close(lh);
			unlink_quietly(le_name);
		}
		lell_packet_unref(lpkt);
	}
}

int main(void)
{
	if (btbb_init(2) < 0) {
		printf("FAIL btbb_init\n");
		return 1;
	}

	test_bredr_pcapng_record();
	test_bredr_pcapng_empty_payload();
	test_bredr_failed_checks();
	test_bredr_voice_payload_has_no_crc();
	test_bredr_esco_crc_reported();
	test_bredr_uap_search();
	test_bredr_reused_packet();
	test_bredr_undecoded_payload_is_not_reported();
	test_bredr_reused_packet_in_the_uap_search();
	test_bredr_payload_does_not_survive_the_uap_search();
	test_bredr_corrected_bits();
	test_bredr_interface_options();
	test_bredr_pcap_record();
	test_le_pcap_records();
	test_le_connect_ind_option();
	test_le_short_connect_ind();
	test_header_padding_options();
	test_padding_beyond_one_option();
	test_section_option_limit();
	test_write_errors();

	if (failures) {
		printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("all capture file checks passed\n");
	return 0;
}
