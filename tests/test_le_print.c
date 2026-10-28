/*
 * What lell_print() writes, and on which stream: packets whose decode
 * involves byte assembly (a CONNECT_REQ, whose connection access address has
 * its top bit set, and advertising data carrying 16-bit service UUIDs), and
 * packets whose advertising data is malformed, where the complaint belongs on
 * stderr and the dump on stdout.
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "btbb.h"
#include "bluetooth_le_packet.h"

static int failures = 0;

#define CHECK_CONTAINS(haystack, needle)                                   \
	do {                                                               \
		if (strstr((haystack), (needle)) == NULL) {                \
			printf("FAIL %s:%d: output has no \"%s\"\n",       \
			       __FILE__, __LINE__, (needle));              \
			++failures;                                        \
		}                                                          \
	} while (0)

#define CHECK_EQUALS(got, want)                                            \
	do {                                                               \
		if (strcmp((got), (want)) != 0) {                          \
			printf("FAIL %s:%d: output is \"%s\", wanted \"%s\"\n", \
			       __FILE__, __LINE__, (got), (want));         \
			++failures;                                        \
		}                                                          \
	} while (0)

#define CHECK_LACKS(haystack, needle)                                      \
	do {                                                               \
		if (strstr((haystack), (needle)) != NULL) {                \
			printf("FAIL %s:%d: output has \"%s\"\n",          \
			       __FILE__, __LINE__, (needle));              \
			++failures;                                        \
		}                                                          \
	} while (0)

/* A CONNECT_REQ: InitA, AdvA, then the connection parameters. */
static const uint8_t CONNECT_REQ_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,			/* advertising access address */
	0x05,					/* CONNECT_REQ */
	34,					/* 12 + 22 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* InitA */
	0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77,	/* AdvA */
	0x95, 0xf3, 0xc1, 0x8e,			/* connection AA 0x8ec1f395 */
	0x01, 0x02, 0x03,			/* CRCInit */
	0x03,					/* WinSize */
	0x04, 0x00,				/* WinOffset */
	0x18, 0x00,				/* Interval */
	0x00, 0x00,				/* Latency */
	0x48, 0x00,				/* Timeout */
	0xff, 0xff, 0xff, 0xff, 0x1f,		/* ChM */
	0x07,					/* Hop 7, SCA 0 */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

/*
 * An ADV_IND whose advertising data is a list of two 16-bit service UUIDs.
 * The flags element in front of them is what puts the list on an odd offset,
 * so that reading a UUID through a uint16_t pointer is a misaligned load the
 * sanitizer reports.
 */
static const uint8_t ADV_IND_UUID16_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,			/* advertising access address */
	0x00,					/* ADV_IND, TxAdd public */
	15,					/* 6 + 9 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x02, 0x01, 0x1a,			/* flags */
	0x05, 0x03, 0x0d, 0x18, 0x0f, 0x18,	/* UUIDs 0x180d and 0x180f */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

/* Run lell_print() with stdout and stderr redirected into out and err. */
static int capture_lell_print(const lell_packet *pkt,
			      char *out, size_t outlen,
			      char *err, size_t errlen)
{
	FILE *out_capture, *err_capture;
	int saved_out, saved_err;
	size_t read;

	out[0] = '\0';
	err[0] = '\0';

	out_capture = tmpfile();
	err_capture = tmpfile();
	if (out_capture == NULL || err_capture == NULL)
		return -1;

	fflush(stdout);
	fflush(stderr);
	saved_out = dup(fileno(stdout));
	saved_err = dup(fileno(stderr));
	if (saved_out < 0 || saved_err < 0 ||
	    dup2(fileno(out_capture), fileno(stdout)) < 0 ||
	    dup2(fileno(err_capture), fileno(stderr)) < 0) {
		/* put back whatever was redirected before giving up */
		if (saved_out >= 0) {
			dup2(saved_out, fileno(stdout));
			close(saved_out);
		}
		if (saved_err >= 0) {
			dup2(saved_err, fileno(stderr));
			close(saved_err);
		}
		fclose(out_capture);
		fclose(err_capture);
		return -1;
	}

	lell_print(pkt);
	fflush(stdout);
	fflush(stderr);

	dup2(saved_out, fileno(stdout));
	dup2(saved_err, fileno(stderr));
	close(saved_out);
	close(saved_err);

	rewind(out_capture);
	read = fread(out, 1, outlen - 1, out_capture);
	out[read] = '\0';
	fclose(out_capture);

	rewind(err_capture);
	read = fread(err, 1, errlen - 1, err_capture);
	err[read] = '\0';
	fclose(err_capture);
	return 0;
}

/* Print one decoded packet with the streams captured, and free it. */
static int capture_decoded(lell_packet *pkt, char *out, size_t outlen,
			   char *err, size_t errlen)
{
	int result;

	if (pkt == NULL) {
		printf("FAIL: no packet\n");
		++failures;
		return -1;
	}

	result = capture_lell_print(pkt, out, outlen, err, errlen);
	if (result != 0) {
		printf("FAIL: could not capture the output\n");
		++failures;
	}

	lell_packet_unref(pkt);
	return result;
}

/*
 * Decode one packet, print it with the streams captured, and free it.
 * Returns 0 on success.
 */
static int decode_and_capture(const uint8_t *packet,
			      char *out, size_t outlen,
			      char *err, size_t errlen)
{
	lell_packet *pkt = NULL;

	lell_allocate_and_decode(packet, 2402, 0, &pkt);
	return capture_decoded(pkt, out, outlen, err, errlen);
}

/* The same for a capture that holds a known number of bytes, which is what
 * lell_allocate_and_decode_n() is given. */
static int decode_n_and_capture(const uint8_t *packet, size_t len,
				uint16_t phys_channel,
				char *out, size_t outlen,
				char *err, size_t errlen)
{
	lell_packet *pkt = NULL;

	lell_allocate_and_decode_n(packet, len, phys_channel, 0, &pkt);
	return capture_decoded(pkt, out, outlen, err, errlen);
}

static void test_connect_req(void)
{
	char out[8192], err[1024];

	if (decode_and_capture(CONNECT_REQ_PACKET, out, sizeof(out),
			       err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "Type:  CONNECT_REQ");
	CHECK_CONTAINS(out, "InitA: 11:22:33:44:55:66 (public)");
	CHECK_CONTAINS(out, "AdvA:  77:88:99:aa:bb:cc (public)");
	/* the byte with the top bit set must survive the shift */
	CHECK_CONTAINS(out, "AA:    8ec1f395");
	CHECK_CONTAINS(out, "CRCInit: 030201");
	CHECK_CONTAINS(out, "Interval: 0018 (24)");
	CHECK_CONTAINS(out, "Hop: 7");
	if (err[0] != '\0') {
		printf("FAIL: unexpected stderr output: %s", err);
		++failures;
	}
}

static void test_uuid16_advertising_data(void)
{
	char out[8192], err[1024];

	if (decode_and_capture(ADV_IND_UUID16_PACKET, out, sizeof(out),
			       err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "Type 03 (16-bit Service UUIDs)");
	/* UUIDs are little endian on air, whatever the host is */
	CHECK_CONTAINS(out, "\n           180d\n");
	CHECK_CONTAINS(out, "\n           180f\n");
	if (err[0] != '\0') {
		printf("FAIL: unexpected stderr output: %s", err);
		++failures;
	}
}

/*
 * Each of these has advertising data the parser complains about: a Tx Power
 * Level of the wrong length, a field that runs past the end of the data, and
 * a field of zero length.
 */
static const uint8_t BAD_TX_POWER_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	10,					/* 6 + 4 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x03, 0x0a, 0x01, 0x02,			/* Tx Power Level, 2 bytes */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

static const uint8_t PAST_END_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	10,					/* 6 + 4 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x05, 0x09, 0x41, 0x42,			/* a 5-byte field in 4 bytes */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

static const uint8_t ZERO_LENGTH_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	10,					/* 6 + 4 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x00, 0x00, 0x00, 0x00,			/* a zero-length field */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

/* One packet for each of the other three "Wrong length" messages. */
static const uint8_t BAD_INTERVAL_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	10,					/* 6 + 4 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x03, 0x12, 0x06, 0x00,			/* interval range, 2 bytes */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

static const uint8_t BAD_SERVICE_DATA_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	9,					/* 6 + 3 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x02, 0x16, 0x0d,			/* service data, 1 byte */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

static const uint8_t BAD_MANUFACTURER_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	9,					/* 6 + 3 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x02, 0xff, 0x4c,			/* manufacturer data, 1 byte */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

static const uint8_t BAD_UUID128_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,
	0x00,					/* ADV_IND */
	10,					/* 6 + 4 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x03, 0x07, 0x01, 0x02,			/* 128-bit UUIDs, 2 bytes */
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
};

/* The complaints go to stderr, and the dump on stdout stays a dump. */
static void test_diagnostics_go_to_stderr(void)
{
	char out[8192], err[1024];

	static const struct {
		const uint8_t *packet;
		const char *message;
		const char *dump_line;
	} CASES[] = {
		{ BAD_TX_POWER_PACKET, "Wrong length (2, should be 1)\n",
		  "Type 0a (Tx Power Level)" },
		{ BAD_INTERVAL_PACKET, "Wrong length (2, should be 4)\n",
		  "Type 12 (Slave Connection Interval Range)" },
		{ BAD_SERVICE_DATA_PACKET, "Wrong length (1, should be >= 2)\n",
		  "Type 16 (Service Data)" },
		{ BAD_MANUFACTURER_PACKET, "Wrong length (1, should be >= 2)\n",
		  "Type ff (Manufacturer Specific Data)" },
		{ BAD_UUID128_PACKET,
		  "Wrong length (2, must be divisible by 16)\n",
		  "Type 07 (128-bit Service UUIDs)" },
		{ PAST_END_PACKET,
		  "Error: attempt to read past end of buffer (1 + 5 > 4)\n",
		  "Type:  ADV_IND" },
		{ ZERO_LENGTH_PACKET, "Early return due to 0 length\n",
		  "Type:  ADV_IND" },
	};
	size_t i;

	for (i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
		if (decode_and_capture(CASES[i].packet, out, sizeof(out),
				       err, sizeof(err)) != 0)
			continue;

		/* the message, and nothing else, on stderr */
		CHECK_EQUALS(err, CASES[i].message);
		CHECK_LACKS(out, "Wrong length");
		CHECK_LACKS(out, "Error:");
		CHECK_LACKS(out, "Early return");
		/* the dump itself is still there, and still a dump */
		CHECK_CONTAINS(out, "Type:  ADV_IND");
		CHECK_CONTAINS(out, CASES[i].dump_line);
	}
}

/*
 * The indentation of a value belongs to the dump, so a value that can be
 * read still carries it.
 */
static void test_well_formed_values_keep_their_indent(void)
{
	static const uint8_t GOOD_VALUES_PACKET[MAX_LE_SYMBOLS] = {
		0xd6, 0xbe, 0x89, 0x8e,
		0x00,				/* ADV_IND */
		26,				/* 6 + 20 bytes */
		0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
		0x02, 0x0a, 0x06,		/* Tx Power Level 6 dBm */
		0x05, 0x12, 0x06, 0x00, 0x0c, 0x00,	/* interval range */
		0x04, 0x16, 0x0d, 0x18, 0x5a,	/* service data */
		0x05, 0xff, 0x4c, 0x00, 0x01, 0x02,	/* manufacturer data */
		0xde, 0xad, 0xbe,		/* CRC, unchecked */
	};
	char out[8192], err[1024];

	if (decode_and_capture(GOOD_VALUES_PACKET, out, sizeof(out),
			       err, sizeof(err)) != 0)
		return;

	CHECK_EQUALS(err, "");
	CHECK_CONTAINS(out, "\n           6 dBm\n");
	CHECK_CONTAINS(out, "\n           (7.50, 15.00) ms\n");
	CHECK_CONTAINS(out, "\n           UUID: 180d, Additional: 5a\n");
	CHECK_CONTAINS(out, "\n           Company: Apple, Inc.\n");
	CHECK_CONTAINS(out, "\n           Data: 01 02\n");
}

/*
 * An advertising PDU may claim a longer payload than the capture holds: here
 * 63 bytes, where a 64-byte capture can hold 55 after the access address,
 * the header and the CRC.  The claim used to be taken at face value, and the
 * three bytes printed as the CRC came from past the end of the symbol buffer
 * - the packet structure's own next fields, which is how the access address
 * and channel index of the packet being printed came out as its CRC.
 *
 * The bytes the capture holds are printed, and no others. An ADV_DIRECT_IND
 * is used because its two addresses are read from fixed offsets rather than
 * walked through as advertising data.
 */
static void test_an_over_claimed_advertising_length(void)
{
	uint8_t packet[LELL_LEGACY_STREAM_LEN];
	char out[8192], err[1024];
	int i;

	packet[0] = 0xd6;
	packet[1] = 0xbe;
	packet[2] = 0x89;
	packet[3] = 0x8e;
	packet[4] = 0x01;		/* ADV_DIRECT_IND */
	packet[5] = 0x3f;		/* claims 63 payload bytes */
	/* a byte's own index, so that each one printed can be placed */
	for (i = 6; i < LELL_LEGACY_STREAM_LEN; i++)
		packet[i] = (uint8_t) i;

	if (decode_and_capture(packet, out, sizeof(out),
			       err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "Advertising / AA 8e89bed6 (valid)/ 55 bytes");
	CHECK_CONTAINS(out, "AdvA:  0b:0a:09:08:07:06 (public)");
	CHECK_CONTAINS(out, "InitA: 11:10:0f:0e:0d:0c (public)");
	/* the last three bytes of the capture, 0x3d to 0x3f */
	CHECK_CONTAINS(out, "\n    CRC:   3d 3e 3f\n");
	/* the access address, which is what used to be printed from past the
	 * end of the buffer */
	CHECK_LACKS(out, "d6 be 89 8e");
	CHECK_EQUALS(err, "");
}

/*
 * An extended advertising PDU on a primary channel goes past 63 payload
 * octets, where a legacy one stops at 37. Only six bits of the length field
 * were read, so a 64-octet PDU came out as 0 bytes and none of its payload
 * was dumped.
 */
static void test_an_extended_advertising_length(void)
{
	uint8_t packet[6 + 64 + 3];
	char out[8192], err[1024];
	int i;

	packet[0] = 0xd6;
	packet[1] = 0xbe;
	packet[2] = 0x89;
	packet[3] = 0x8e;
	packet[4] = 0x07;		/* ADV_EXT_IND */
	packet[5] = 64;
	/* a byte's own index, so that each one printed can be placed */
	for (i = 6; i < (int) sizeof(packet); i++)
		packet[i] = (uint8_t) i;

	if (decode_n_and_capture(packet, sizeof(packet), 2402,
				 out, sizeof(out), err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "Advertising / AA 8e89bed6 (valid)/ 64 bytes");
	/* the first payload bytes, the last of them, then the CRC */
	CHECK_CONTAINS(out, "\n    Data:  06 07 08");
	CHECK_CONTAINS(out, " 42 43 44 45\n");
	CHECK_CONTAINS(out, "\n    CRC:   46 47 48\n");
	CHECK_EQUALS(err, "");
}

/*
 * A PDU whose payload is too short for its own type: the fields after it
 * were printed from whatever the buffer held there, so a CONNECT_REQ
 * carrying ten bytes still reported a connection access address, a hop
 * increment and a clock accuracy, none of which arrived.
 */
static void test_a_payload_too_short_for_its_type(void)
{
	static const uint8_t SHORT_CONNECT_REQ[LELL_LEGACY_STREAM_LEN] = {
		0xd6, 0xbe, 0x89, 0x8e,
		0x05,				/* CONNECT_REQ */
		10,				/* 24 bytes short of one */
		0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
		0x01, 0x02, 0x03, 0x04,
		0xde, 0xad, 0xbe,		/* CRC, unchecked */
	};
	char out[8192], err[1024];

	if (decode_and_capture(SHORT_CONNECT_REQ, out, sizeof(out),
			       err, sizeof(err)) != 0)
		return;

	CHECK_EQUALS(err, "Error: CONNECT_REQ needs 34 payload bytes, 10 received\n");
	CHECK_CONTAINS(out, "Type:  CONNECT_REQ");
	CHECK_LACKS(out, "InitA:");
	CHECK_LACKS(out, "AA:    ");
	CHECK_LACKS(out, "Hop:");
	CHECK_LACKS(out, "SCA:");
	/* the bytes that did arrive are still dumped */
	CHECK_CONTAINS(out, "\n    Data:  66 55 44 33 22 11 01 02 03 04\n");
}

/*
 * A capture that stops partway through a packet: the length field is the
 * sender's, so it describes bytes the receiver never had.  The decoder
 * reports only what it was given, and the print follows it.
 */
static void test_a_truncated_capture(void)
{
	static const uint8_t TRUNCATED_ADV_IND[12] = {
		0xd6, 0xbe, 0x89, 0x8e,
		0x00,				/* ADV_IND */
		37,				/* of which 3 bytes arrived */
		0x66, 0x55, 0x44,
		0xde, 0xad, 0xbe,		/* CRC, unchecked */
	};
	char out[8192], err[1024];

	if (decode_n_and_capture(TRUNCATED_ADV_IND, sizeof(TRUNCATED_ADV_IND),
				 2402, out, sizeof(out), err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "Advertising / AA 8e89bed6 (valid)/  3 bytes");
	CHECK_EQUALS(err, "Error: ADV_IND needs 6 payload bytes, 3 received\n");
	CHECK_LACKS(out, "AdvA:");
	CHECK_CONTAINS(out, "\n    Data:  66 55 44\n");
	CHECK_CONTAINS(out, "\n    CRC:   de ad be\n");
}

/* An LL Control PDU carries its opcode in the first payload byte, so one
 * that arrived with no payload has no opcode to report. */
static void test_an_empty_ll_control_pdu(void)
{
	static const uint8_t EMPTY_CONTROL_PDU[9] = {
		0x11, 0x22, 0x33, 0x44,		/* connection access address */
		0x03,				/* LLID 3, LL Control */
		0,				/* no payload */
		0xde, 0xad, 0xbe,		/* CRC, unchecked */
	};
	char out[8192], err[1024];

	/* 2404 MHz is data channel 0 */
	if (decode_n_and_capture(EMPTY_CONTROL_PDU, sizeof(EMPTY_CONTROL_PDU),
				 2404, out, sizeof(out), err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "LLID: 3 / LL Control PDU");
	CHECK_EQUALS(err, "Error: an LL Control PDU needs 1 payload byte, 0 received\n");
	CHECK_LACKS(out, "Opcode");
	CHECK_CONTAINS(out, "\n    CRC:   de ad be\n");
}

/* A flags element with no flags in it: its one byte is not there to read. */
static void test_an_empty_flags_element(void)
{
	static const uint8_t EMPTY_FLAGS_PACKET[MAX_LE_SYMBOLS] = {
		0xd6, 0xbe, 0x89, 0x8e,
		0x00,				/* ADV_IND */
		8,				/* 6 + 2 bytes */
		0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
		0x01, 0x01,			/* flags, with no flags byte */
		0xde, 0xad, 0xbe,		/* CRC, unchecked */
	};
	char out[8192], err[1024];

	if (decode_and_capture(EMPTY_FLAGS_PACKET, out, sizeof(out),
			       err, sizeof(err)) != 0)
		return;

	CHECK_CONTAINS(out, "Type 01 (Flags)");
	CHECK_EQUALS(err, "Wrong length (0, should be >= 1)\n");
}

int main(void)
{
	test_connect_req();
	test_uuid16_advertising_data();
	test_diagnostics_go_to_stderr();
	test_well_formed_values_keep_their_indent();
	test_an_over_claimed_advertising_length();
	test_an_extended_advertising_length();
	test_a_payload_too_short_for_its_type();
	test_a_truncated_capture();
	test_an_empty_ll_control_pdu();
	test_an_empty_flags_element();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
