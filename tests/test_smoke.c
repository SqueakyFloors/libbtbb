/*
 * Smoke test: link the library and decode one known packet of each kind.
 *
 * It works through the public API in btbb.h, except that it reads a couple of
 * packet fields the public API has no getter for straight out of the
 * internal header.
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

/*
 * One ADV_IND, unwhitened, as the decoder receives it: access address,
 * header, 6-byte advertiser address, 9 bytes of advertising data (flags and
 * a complete local name) and a CRC. The buffer is MAX_LE_SYMBOLS long
 * because lell_allocate_and_decode() copies that much.
 */
static const uint8_t ADV_IND_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,			/* access address 0x8e89bed6 */
	0x40,					/* ADV_IND, TxAdd random */
	15,					/* 6 + 9 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA 11:22:33:44:55:66 */
	0x02, 0x01, 0x06,			/* flags */
	0x05, 0x09, 'b', 't', 'b', 'b',		/* complete local name */
	0xde, 0xad, 0xbe,			/* CRC */
};

/*
 * Sync words for three LAPs, computed from the (64,30) block code in
 * python/utils/encode_sw.py, independently of the library's own tables.
 */
static const struct {
	uint32_t lap;
	uint64_t syncword;
} KNOWN_SYNCWORDS[] = {
	{ 0x9e8b33, 0x4e7a2cce331a3ae2ULL },	/* general inquiry access code */
	{ 0xffffff, 0x4ffffffe44ad1ae7ULL },
	{ 0x123456, 0xb048d15a658627c0ULL },
};

static void test_le_advertising_packet(void)
{
	lell_packet *pkt = NULL;

	lell_allocate_and_decode(ADV_IND_PACKET, 2402, 0x12345678, &pkt);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	CHECK(lell_get_access_address(pkt) == LE_ADV_AA);
	CHECK(lell_get_access_address_offenses(pkt) == 0);
	CHECK(lell_packet_is_data(pkt) == 0);
	CHECK(lell_get_channel_index(pkt) == 37);
	CHECK(lell_get_channel_k(pkt) == 0);
	CHECK(strcmp(lell_get_adv_type_str(pkt), "ADV_IND") == 0);
	CHECK(pkt->length == 15);
	CHECK(pkt->adv_tx_add == 1);
	CHECK(pkt->adv_rx_add == 0);
	CHECK(pkt->clk100ns == 0x12345678);

	/* exercise the dump path, including the advertising data parser */
	lell_print(pkt);

	lell_packet_unref(pkt);
}

/* How far into the symbol stream below the sync word starts. */
#define SEARCH_LENGTH 24
#define PACKET_OFFSET 10

/* The sync word a LAP generates, and the LAP a sync word decodes back to. */
static void test_bredr_syncword(void)
{
	size_t i;
	int bit, offset;
	char stream[SEARCH_LENGTH + 64];
	btbb_packet *pkt = NULL;
	uint64_t syncword;

	for (i = 0; i < sizeof(KNOWN_SYNCWORDS) / sizeof(KNOWN_SYNCWORDS[0]); ++i)
		CHECK(btbb_gen_syncword((int) KNOWN_SYNCWORDS[i].lap) ==
		      KNOWN_SYNCWORDS[i].syncword);

	CHECK(btbb_init(2) == 0);

	/* a sync word in air order, with silence either side of it */
	syncword = KNOWN_SYNCWORDS[0].syncword;
	memset(stream, 0, sizeof(stream));
	for (bit = 0; bit < 64; ++bit)
		stream[PACKET_OFFSET + bit] = (char) ((syncword >> bit) & 1);

	offset = btbb_find_ac(stream, SEARCH_LENGTH, LAP_ANY, 0, &pkt);
	CHECK(offset == PACKET_OFFSET);
	CHECK(pkt != NULL);
	if (pkt != NULL) {
		CHECK(btbb_packet_get_lap(pkt) == KNOWN_SYNCWORDS[0].lap);
		CHECK(btbb_packet_get_ac_errors(pkt) == 0);
		btbb_packet_unref(pkt);
	}
}

static void test_version_strings(void)
{
	CHECK(btbb_get_version() != NULL && btbb_get_version()[0] != '\0');
	CHECK(btbb_get_release() != NULL && btbb_get_release()[0] != '\0');
}

int main(void)
{
	test_version_strings();
	test_le_advertising_packet();
	test_bredr_syncword();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
