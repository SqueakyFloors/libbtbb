/*
 * What lell_print() writes for packets whose decode involves byte assembly:
 * a CONNECT_REQ, whose connection access address has its top bit set, and
 * advertising data carrying 16-bit service UUIDs.
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
	0xde, 0xad, 0xbe,			/* CRC */
};

/* An ADV_IND whose advertising data is a list of two 16-bit service UUIDs. */
static const uint8_t ADV_IND_UUID16_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,			/* advertising access address */
	0x00,					/* ADV_IND, TxAdd public */
	12,					/* 6 + 6 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA */
	0x05, 0x03, 0x0d, 0x18, 0x0f, 0x18,	/* UUIDs 0x180d and 0x180f */
	0xde, 0xad, 0xbe,			/* CRC */
};

/* Run lell_print() with stdout redirected into buf. */
static int capture_lell_print(const lell_packet *pkt, char *buf, size_t buflen)
{
	FILE *capture;
	int saved_stdout;
	size_t read;

	buf[0] = '\0';

	capture = tmpfile();
	if (capture == NULL)
		return -1;

	fflush(stdout);
	saved_stdout = dup(fileno(stdout));
	if (saved_stdout < 0 || dup2(fileno(capture), fileno(stdout)) < 0) {
		fclose(capture);
		return -1;
	}

	lell_print(pkt);
	fflush(stdout);

	dup2(saved_stdout, fileno(stdout));
	close(saved_stdout);

	rewind(capture);
	read = fread(buf, 1, buflen - 1, capture);
	buf[read] = '\0';
	fclose(capture);
	return 0;
}

static void test_connect_req(void)
{
	lell_packet *pkt = NULL;
	char out[8192];

	lell_allocate_and_decode(CONNECT_REQ_PACKET, 2402, 0, &pkt);
	if (pkt == NULL) {
		printf("FAIL: no packet\n");
		++failures;
		return;
	}

	if (capture_lell_print(pkt, out, sizeof(out)) != 0) {
		printf("FAIL: could not capture stdout\n");
		++failures;
	} else {
		CHECK_CONTAINS(out, "Type:  CONNECT_REQ");
		CHECK_CONTAINS(out, "InitA: 11:22:33:44:55:66 (public)");
		CHECK_CONTAINS(out, "AdvA:  77:88:99:aa:bb:cc (public)");
		/* the byte with the top bit set must survive the shift */
		CHECK_CONTAINS(out, "AA:    8ec1f395");
		CHECK_CONTAINS(out, "CRCInit: 030201");
		CHECK_CONTAINS(out, "Interval: 0018 (24)");
		CHECK_CONTAINS(out, "Hop: 7");
	}

	lell_packet_unref(pkt);
}

static void test_uuid16_advertising_data(void)
{
	lell_packet *pkt = NULL;
	char out[8192];

	lell_allocate_and_decode(ADV_IND_UUID16_PACKET, 2402, 0, &pkt);
	if (pkt == NULL) {
		printf("FAIL: no packet\n");
		++failures;
		return;
	}

	if (capture_lell_print(pkt, out, sizeof(out)) != 0) {
		printf("FAIL: could not capture stdout\n");
		++failures;
	} else {
		CHECK_CONTAINS(out, "Type 03 (16-bit Service UUIDs)");
		/* UUIDs are little endian on air, whatever the host is */
		CHECK_CONTAINS(out, "180d");
		CHECK_CONTAINS(out, "180f");
	}

	lell_packet_unref(pkt);
}

int main(void)
{
	test_connect_req();
	test_uuid16_advertising_data();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
