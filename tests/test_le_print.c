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

/*
 * Decode one packet, print it with the streams captured, and free it.
 * Returns 0 on success.
 */
static int decode_and_capture(const uint8_t *packet,
			      char *out, size_t outlen,
			      char *err, size_t errlen)
{
	lell_packet *pkt = NULL;
	int result;

	lell_allocate_and_decode(packet, 2402, 0, &pkt);
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

/* The complaints go to stderr, and the dump on stdout stays a dump. */
static void test_diagnostics_go_to_stderr(void)
{
	char out[8192], err[1024];

	if (decode_and_capture(BAD_TX_POWER_PACKET, out, sizeof(out),
			       err, sizeof(err)) == 0) {
		CHECK_CONTAINS(err, "Wrong length (2, should be 1)");
		CHECK_LACKS(out, "Wrong length");
		/* the dump itself is still there and still parseable */
		CHECK_CONTAINS(out, "Type:  ADV_IND");
		CHECK_CONTAINS(out, "Type 0a (Tx Power Level)");
	}

	if (decode_and_capture(PAST_END_PACKET, out, sizeof(out),
			       err, sizeof(err)) == 0) {
		CHECK_CONTAINS(err,
			"Error: attempt to read past end of buffer (1 + 5 > 4)");
		CHECK_LACKS(out, "Error:");
		CHECK_CONTAINS(out, "Type:  ADV_IND");
	}

	if (decode_and_capture(ZERO_LENGTH_PACKET, out, sizeof(out),
			       err, sizeof(err)) == 0) {
		CHECK_CONTAINS(err, "Early return due to 0 length");
		CHECK_LACKS(out, "Early return");
		CHECK_CONTAINS(out, "Type:  ADV_IND");
	}
}

int main(void)
{
	test_connect_req();
	test_uuid16_advertising_data();
	test_diagnostics_go_to_stderr();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
