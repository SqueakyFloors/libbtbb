/*
 * Low Energy link layer conformance: the packet fields and rules of
 * Bluetooth Core Specification v5.4, Vol 6, Part B, driven through libbtbb's
 * LE decoder.
 *
 * libbtbb decodes LE packets that have already been dewhitened and whose CRC
 * has already been checked, so the CRC-24 of Section 3.1.1, the per-channel
 * whitening of Section 3.2 and the channel selection algorithms of Section
 * 4.5.8 are not in this library and have no vectors here. What is here is
 * what libbtbb does decide: the physical channel index of Section 1.4.1, the
 * advertising and data PDU headers of Sections 2.3 and 2.4 including Data
 * Length Extension lengths, and the access address rules of Section 2.1.2.
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

#include "btbb.h"
#include "bluetooth_le_packet.h"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/* ------------------------------------------------------------------ *
 * Section 1.4.1: physical channel indices
 * ------------------------------------------------------------------ */

/*
 * Table 1.3: 2402 MHz is index 37, 2426 MHz is 38 and 2480 MHz is 39, the
 * three primary advertising channels; the rest run 0 to 36 in two of
 * ascending frequency, skipping the advertising ones.
 */
static void test_channel_indices(void)
{
	static const struct {
		uint16_t mhz;
		uint8_t index;
	} CHANNELS[] = {
		{ 2402, 37 }, { 2404,  0 }, { 2406,  1 }, { 2408,  2 },
		{ 2410,  3 }, { 2412,  4 }, { 2414,  5 }, { 2416,  6 },
		{ 2418,  7 }, { 2420,  8 }, { 2422,  9 }, { 2424, 10 },
		{ 2426, 38 }, { 2428, 11 }, { 2430, 12 }, { 2432, 13 },
		{ 2434, 14 }, { 2436, 15 }, { 2438, 16 }, { 2440, 17 },
		{ 2442, 18 }, { 2444, 19 }, { 2446, 20 }, { 2448, 21 },
		{ 2450, 22 }, { 2452, 23 }, { 2454, 24 }, { 2456, 25 },
		{ 2458, 26 }, { 2460, 27 }, { 2462, 28 }, { 2464, 29 },
		{ 2466, 30 }, { 2468, 31 }, { 2470, 32 }, { 2472, 33 },
		{ 2474, 34 }, { 2476, 35 }, { 2478, 36 }, { 2480, 39 }
	};
	size_t row;

	for (row = 0; row < sizeof(CHANNELS) / sizeof(CHANNELS[0]); row++) {
		uint8_t stream[MAX_LE_SYMBOLS];
		lell_packet *pkt = NULL;

		memset(stream, 0, sizeof(stream));
		lell_allocate_and_decode_n(stream, sizeof(stream),
					   CHANNELS[row].mhz, 0, &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(lell_get_channel_index(pkt) == CHANNELS[row].index);
		/* the three advertising channels are the only non-data ones */
		CHECK(lell_packet_is_data(pkt) ==
		      (CHANNELS[row].index < 37 ? 1u : 0u));
		lell_packet_unref(pkt);
	}
}

/* ------------------------------------------------------------------ *
 * Section 2.3: advertising physical channel PDUs
 * ------------------------------------------------------------------ */

/* The longest LE frame on air: the access address, the two header octets, the
 * 255 payload octets a one-octet Length field can claim, and the CRC. Written
 * out rather than taken from MAX_LE_SYMBOLS, so that a library whose buffer is
 * shorter fails the checks below instead of overflowing the test's own
 * frame. */
#define LONGEST_LE_FRAME 264

/* Lay out a packet the way the decoder receives it, already dewhitened:
 * access address, two header octets, payload, CRC. `stream_size` is how many
 * bytes `stream` holds. */
static int build_le_packet(uint8_t *stream, size_t stream_size, uint32_t aa,
			   uint8_t header0, uint8_t length,
			   const uint8_t *payload)
{
	int i;

	memset(stream, 0, stream_size);
	stream[0] = (uint8_t) (aa & 0xff);
	stream[1] = (uint8_t) ((aa >> 8) & 0xff);
	stream[2] = (uint8_t) ((aa >> 16) & 0xff);
	stream[3] = (uint8_t) ((aa >> 24) & 0xff);
	stream[4] = header0;
	stream[5] = length;
	if (payload != NULL)
		memcpy(stream + 6, payload, length);
	for (i = 0; i < 3; i++)
		stream[6 + length + i] = (uint8_t) (0xc0 + i);
	return 6 + length + 3;
}

/* The longest payload a one-octet Length field can claim. */
#define MAX_BUILT_PAYLOAD 255

/*
 * Section 2.3: the header's first octet is the 4-bit PDU Type, one reserved
 * bit, ChSel, TxAdd and RxAdd; the second octet is the Length, and Section
 * 2.1.2 fixes the access address of every advertising channel packet at
 * 0x8E89BED6.
 */
static void test_advertising_pdu_header(void)
{
	static const struct {
		uint8_t type;
		int tx_add;
		int rx_add;
	} CASES[] = {
		{ ADV_IND,         0, 0 }, { ADV_IND,         1, 0 },
		{ ADV_DIRECT_IND,  1, 1 }, { ADV_NONCONN_IND, 0, 0 },
		{ SCAN_REQ,        1, 0 }, { SCAN_RSP,        0, 1 },
		{ CONNECT_REQ,     1, 1 }, { ADV_SCAN_IND,    0, 0 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		uint8_t stream[MAX_LE_SYMBOLS];
		uint8_t payload[37];
		lell_packet *pkt = NULL;
		uint8_t header0 = (uint8_t) (CASES[row].type |
					     (CASES[row].tx_add ? 0x40 : 0) |
					     (CASES[row].rx_add ? 0x80 : 0));
		int length, i;

		for (i = 0; i < 37; i++)
			payload[i] = (uint8_t) i;
		length = build_le_packet(stream, sizeof(stream), LE_ADV_AA,
					 header0, 37, payload);

		lell_allocate_and_decode_n(stream, (size_t) length, 2402, 0,
					   &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(lell_get_access_address(pkt) == LE_ADV_AA);
		CHECK(lell_get_access_address_offenses(pkt) == 0);
		CHECK(pkt->flags.as_bits.access_address_ok);
		CHECK(pkt->adv_type == CASES[row].type);
		CHECK(pkt->adv_tx_add == CASES[row].tx_add);
		CHECK(pkt->adv_rx_add == CASES[row].rx_add);
		CHECK(lell_get_payload_length(pkt) == 37);
		CHECK(lell_get_adv_type_str(pkt) != NULL);
		lell_packet_unref(pkt);
	}

	/* any other access address on an advertising channel is wrong */
	{
		uint8_t stream[MAX_LE_SYMBOLS];
		lell_packet *pkt = NULL;
		int length = build_le_packet(stream, sizeof(stream),
					     0x8e89bed7, ADV_IND, 6, NULL);

		lell_allocate_and_decode_n(stream, (size_t) length, 2426, 0,
					   &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(!pkt->flags.as_bits.access_address_ok);
		/* one bit away from the advertising address, which Section
		 * 2.1.2 also forbids a data channel from using */
		CHECK(lell_get_access_address_offenses(pkt) == 1);
		lell_packet_unref(pkt);
	}
}

/*
 * Section 2.3: "The valid range of the Length field shall be 1 to 255
 * octets", so the whole second header octet is the length. Legacy advertising
 * PDUs stay at or below 37, but an extended advertising PDU on a primary
 * channel goes past 63 - its extended header alone reaches 63 octets - which
 * six bits of the field reported modulo 64, so 64 came back as 0.
 */
static void test_advertising_pdu_length_is_eight_bits(void)
{
	static const uint8_t LENGTHS[] = { 1, 37, 63, 64, 100, 200, 251 };
	size_t row;

	for (row = 0; row < sizeof(LENGTHS) / sizeof(LENGTHS[0]); row++) {
		uint8_t stream[MAX_LE_SYMBOLS];
		uint8_t payload[MAX_BUILT_PAYLOAD];
		lell_packet *pkt = NULL;
		int length, i;

		for (i = 0; i < LENGTHS[row]; i++)
			payload[i] = (uint8_t) (i + 1);
		/* ADV_EXT_IND is PDU type 0b0111 */
		length = build_le_packet(stream, sizeof(stream), LE_ADV_AA,
					 0x07, LENGTHS[row], payload);

		lell_allocate_and_decode_n(stream, (size_t) length, 2402, 0,
					   &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(lell_get_payload_length(pkt) == LENGTHS[row]);
		/* every payload byte is where the printer and the capture
		 * writers look for it */
		for (i = 0; i < LENGTHS[row]; i++)
			CHECK(pkt->symbols[6 + i] == (uint8_t) (i + 1));
		lell_packet_unref(pkt);
	}

	/* a claim of 255 where only 37 octets arrived still reports the 37 */
	{
		uint8_t stream[LONGEST_LE_FRAME];
		lell_packet *pkt = NULL;

		build_le_packet(stream, sizeof(stream), LE_ADV_AA, 0x07, 255,
				NULL);
		lell_allocate_and_decode_n(stream, 9 + 37, 2402, 0, &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(lell_get_payload_length(pkt) == 37);
		lell_packet_unref(pkt);
	}
}

/*
 * Section 2.3: "The valid range of the Length field shall be 1 to 255
 * octets", and extended advertising uses the top of it: AUX_ADV_IND and
 * AUX_CHAIN_IND carry the payload an ADV_EXT_IND only points at.
 *
 * Those are secondary advertising channel PDUs, so they arrive on the same
 * channels as connection traffic - indices 0 to 36, where the library's
 * lell_packet_is_data() reads a packet as a data PDU. A 255-octet PDU there
 * must still come back whole: capping it at the 251 a data PDU may carry
 * would lose its last four octets and report three of them as its CRC, which
 * is what a 260-byte symbol buffer used to do.
 *
 * Every payload octet has to come back, and the three bytes after them have
 * to be the CRC.
 */
static void test_longest_pdu_on_every_channel(void)
{
	/* 2402 MHz is primary advertising channel 37; 2404 and 2440 MHz are
	 * secondary advertising channel indices 0 and 17, which are also the
	 * data channels a followed connection hops over. */
	static const uint16_t CHANNELS[] = { 2402, 2404, 2440 };
	size_t row;

	for (row = 0; row < sizeof(CHANNELS) / sizeof(CHANNELS[0]); row++) {
		uint8_t stream[LONGEST_LE_FRAME];
		uint8_t payload[MAX_BUILT_PAYLOAD];
		lell_packet *pkt = NULL;
		int length, i;

		for (i = 0; i < 255; i++)
			payload[i] = (uint8_t) (0x40 + i);
		/* ADV_EXT_IND and AUX_ADV_IND share PDU type 0b0111; which
		 * one it is follows from the channel it arrived on. */
		length = build_le_packet(stream, sizeof(stream), LE_ADV_AA,
					 0x07, 255, payload);
		CHECK(length == LONGEST_LE_FRAME);

		lell_allocate_and_decode_n(stream, (size_t) length,
					   CHANNELS[row], 0, &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(lell_get_payload_length(pkt) == 255);
		for (i = 0; i < 255; i++)
			CHECK(pkt->symbols[6 + i] == (uint8_t) (0x40 + i));
		/* build_le_packet writes the CRC as c0 c1 c2 */
		for (i = 0; i < 3; i++)
			CHECK(pkt->symbols[6 + 255 + i] ==
			      (uint8_t) (0xc0 + i));
		lell_packet_unref(pkt);
	}
}

/* ------------------------------------------------------------------ *
 * Section 2.4: data physical channel PDUs
 * ------------------------------------------------------------------ */

/*
 * Section 2.4: the header's first octet is LLID, NESN, SN, MD and two
 * reserved bits, and the second is the Length, which Data Length Extension
 * widened to the full octet; the payload may be up to 251 octets.
 */
static void test_data_pdu_header_and_length(void)
{
	static const uint8_t LENGTHS[] = { 0, 1, 27, 28, 63, 64, 128, 251 };
	size_t row;
	int llid, nesn, sn, md;

	for (row = 0; row < sizeof(LENGTHS) / sizeof(LENGTHS[0]); row++) {
		uint8_t stream[MAX_LE_SYMBOLS];
		uint8_t payload[251];
		lell_packet *pkt = NULL;
		int length, i;

		for (i = 0; i < LENGTHS[row]; i++)
			payload[i] = (uint8_t) (0x80 + i);
		length = build_le_packet(stream, sizeof(stream), 0x50655dab,
					 0x01, LENGTHS[row], payload);

		lell_allocate_and_decode_n(stream, (size_t) length, 2440, 0,
					   &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(lell_packet_is_data(pkt));
		CHECK(lell_get_payload_length(pkt) == LENGTHS[row]);
		for (i = 0; i < LENGTHS[row]; i++)
			CHECK(pkt->symbols[6 + i] == (uint8_t) (0x80 + i));
		lell_packet_unref(pkt);
	}

	/* the four header flags, each read from its own bit */
	for (llid = 0; llid < 4; llid++)
		for (nesn = 0; nesn < 2; nesn++)
			for (sn = 0; sn < 2; sn++)
				for (md = 0; md < 2; md++) {
					uint8_t stream[MAX_LE_SYMBOLS];
					lell_packet *pkt = NULL;
					uint8_t header0 = (uint8_t)
						(llid | (nesn << 2) |
						 (sn << 3) | (md << 4));
					int length = build_le_packet(
						stream, sizeof(stream),
						0x50655dab, header0, 4, NULL);

					lell_allocate_and_decode_n(
						stream, (size_t) length, 2440,
						0, &pkt);
					CHECK(pkt != NULL);
					if (pkt == NULL)
						return;
					CHECK((pkt->symbols[4] & 0x3) == llid);
					CHECK(((pkt->symbols[4] >> 2) & 1) ==
					      nesn);
					CHECK(((pkt->symbols[4] >> 3) & 1) ==
					      sn);
					CHECK(((pkt->symbols[4] >> 4) & 1) ==
					      md);
					lell_packet_unref(pkt);
				}
}

/*
 * A length longer than what was captured is not a length the decoder can
 * honor.
 *
 * A claim of 255 on one of these channels is not capped at the 251 octets
 * Section 2.4 allows a data PDU, even though 255 is more than a data PDU may
 * carry: a secondary advertising channel PDU arrives on the same channels and
 * does reach 255, and nothing in the packet says which it is. Reporting what
 * arrived keeps the long PDU whole, where capping would lose its last four
 * octets.
 */
static void test_data_pdu_length_bounds(void)
{
	uint8_t stream[LONGEST_LE_FRAME];
	lell_packet *pkt = NULL;

	memset(stream, 0, sizeof(stream));
	stream[0] = 0xab;
	stream[1] = 0x5d;
	stream[2] = 0x65;
	stream[3] = 0x50;
	stream[4] = 0x01;
	stream[5] = 255;

	lell_allocate_and_decode_n(stream, sizeof(stream), 2440, 0, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(lell_get_payload_length(pkt) == 255);
	lell_packet_unref(pkt);

	/* only 20 bytes captured, so only 11 of them can be payload */
	pkt = NULL;
	lell_allocate_and_decode_n(stream, 20, 2440, 0, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(lell_get_payload_length(pkt) == 11);
	lell_packet_unref(pkt);

	/* and a capture too short to hold even a header is no payload at all */
	pkt = NULL;
	lell_allocate_and_decode_n(stream, 4, 2440, 0, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(lell_get_payload_length(pkt) == 0);
	CHECK(lell_get_access_address(pkt) == 0x50655dab);
	lell_packet_unref(pkt);
}

/* ------------------------------------------------------------------ *
 * Section 2.1.2: access address rules
 * ------------------------------------------------------------------ */

/*
 * Section 2.1.2 lists what a data channel access address may not be. The
 * library counts how many of those rules an address breaks, which is how it
 * tells a real packet from noise, so a compliant address has to score zero
 * and each of these has to score at least one.
 */
static void test_data_access_address_rules(void)
{
	static const struct {
		uint32_t aa;
		const char *why;
	} OFFENDERS[] = {
		{ 0x8e89bed6, "the advertising channel address" },
		{ 0x8e89bed7, "one bit from the advertising address" },
		{ 0xabababab, "all four octets equal" },
		{ 0x5500007f, "more than six consecutive zeros" },
		{ 0x55ffff80, "more than six consecutive ones" },
		{ 0x55555555, "more than 24 transitions" },
		{ 0x03555555, "fewer than two transitions in the top six "
			      "bits" }
	};
	static const uint32_t COMPLIANT[] = {
		0x50655dab, 0x4c6b1e39, 0x71764129, 0x9a3c6b52
	};
	size_t row;

	for (row = 0; row < sizeof(OFFENDERS) / sizeof(OFFENDERS[0]); row++) {
		uint8_t stream[MAX_LE_SYMBOLS];
		lell_packet *pkt = NULL;
		int length = build_le_packet(stream, sizeof(stream),
					     OFFENDERS[row].aa, 0x01, 4,
					     NULL);

		lell_allocate_and_decode_n(stream, (size_t) length, 2440, 0,
					   &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		if (lell_get_access_address_offenses(pkt) == 0) {
			printf("FAIL %s:%d: %08x is accepted, but it is %s\n",
			       __FILE__, __LINE__, OFFENDERS[row].aa,
			       OFFENDERS[row].why);
			++failures;
		}
		CHECK(!pkt->flags.as_bits.access_address_ok);
		lell_packet_unref(pkt);
	}

	for (row = 0; row < sizeof(COMPLIANT) / sizeof(COMPLIANT[0]); row++) {
		uint8_t stream[MAX_LE_SYMBOLS];
		lell_packet *pkt = NULL;
		int length = build_le_packet(stream, sizeof(stream),
					     COMPLIANT[row], 0x01, 4,
					     NULL);

		lell_allocate_and_decode_n(stream, (size_t) length, 2440, 0,
					   &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		if (lell_get_access_address_offenses(pkt) != 0) {
			printf("FAIL %s:%d: %08x breaks %u of the Section "
			       "2.1.2 rules, but it breaks none\n", __FILE__,
			       __LINE__, COMPLIANT[row],
			       lell_get_access_address_offenses(pkt));
			++failures;
		}
		CHECK(pkt->flags.as_bits.access_address_ok);
		lell_packet_unref(pkt);
	}
}

int main(void)
{
	test_channel_indices();
	test_advertising_pdu_header();
	test_advertising_pdu_length_is_eight_bits();
	test_longest_pdu_on_every_channel();
	test_data_pdu_header_and_length();
	test_data_pdu_length_bounds();
	test_data_access_address_rules();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
