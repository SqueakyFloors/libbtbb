/*
 * Smoke test: link the library, decode one known packet of each kind, and
 * check the channel each frequency of the LE band decodes as.
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
 * header, 6-byte advertiser address, 31 bytes of advertising data (flags and
 * a complete local name) and a CRC placeholder, which nothing checks. The
 * buffer is MAX_LE_SYMBOLS long because lell_allocate_and_decode() copies
 * that much.
 *
 * The payload is deliberately longer than 31 bytes in total, so that a
 * decoder reading the advertising length as five bits rather than six gets it
 * wrong.
 */
static const uint8_t ADV_IND_PACKET[MAX_LE_SYMBOLS] = {
	0xd6, 0xbe, 0x89, 0x8e,			/* access address 0x8e89bed6 */
	0x40,					/* ADV_IND, TxAdd random */
	37,					/* 6 + 31 bytes */
	0x66, 0x55, 0x44, 0x33, 0x22, 0x11,	/* AdvA 11:22:33:44:55:66 */
	0x02, 0x01, 0x06,			/* flags */
	0x1b, 0x09,				/* complete local name */
	'a', ' ', 'n', 'a', 'm', 'e', ' ', 'l',
	'o', 'n', 'g', ' ', 'e', 'n', 'o', 'u',
	'g', 'h', ' ', 't', 'o', ' ', 'p', 'a',
	's', 's', ' ', '3', '1',
	0xde, 0xad, 0xbe,			/* CRC, unchecked */
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
	CHECK(pkt->length == 37);
	CHECK(pkt->adv_tx_add == 1);
	CHECK(pkt->adv_rx_add == 0);
	CHECK(pkt->clk100ns == 0x12345678);

	/* exercise the dump path, including the advertising data parser */
	lell_print(pkt);

	lell_packet_unref(pkt);
}

/*
 * The channel a frequency in MHz decodes as. 2402 MHz is advertising channel
 * 37, 2426 MHz is 38 and 2480 MHz is 39, with the data channels 0 to 36 in
 * the 2 MHz steps between them; k counts those steps up from 2402 MHz, and a
 * PCAP reader turns it back into a frequency as 2402 + 2k.
 *
 * Every frequency the band covers is decoded here, with the two below the
 * lowest LE channel and two above the highest. An index outside 0 to 39 is
 * no channel at all, and because it is kept in a byte, a frequency outside
 * the band computes one that wraps: far enough below, into the 0 to 36 that
 * lell_packet_is_data() reads as a data channel.
 */
static void test_le_channel_index(void)
{
	static const struct {
		uint16_t mhz;
		unsigned idx;
		unsigned k;
	} CHANNELS[] = {
		{ 2400, 37, 0 },	/* below the band: the lowest channel */
		{ 2401, 37, 0 },
		{ 2402, 37, 0 },
		{ 2404,  0, 1 },
		{ 2424, 10, 11 },
		{ 2426, 38, 12 },
		{ 2428, 11, 13 },
		{ 2478, 36, 38 },
		{ 2480, 39, 39 },
		{ 2482, 39, 39 },	/* above it: the highest channel */
		{ 3000, 39, 39 },
	};
	char seen[40];
	size_t i;
	uint16_t mhz;

	for (i = 0; i < sizeof(CHANNELS) / sizeof(CHANNELS[0]); ++i) {
		lell_packet *pkt = NULL;

		lell_allocate_and_decode(ADV_IND_PACKET, CHANNELS[i].mhz, 0,
					 &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		if (lell_get_channel_index(pkt) != CHANNELS[i].idx ||
		    lell_get_channel_k(pkt) != CHANNELS[i].k) {
			printf("FAIL %s:%d: %u MHz is channel %u k %u, "
			       "expected channel %u k %u\n",
			       __FILE__, __LINE__, CHANNELS[i].mhz,
			       lell_get_channel_index(pkt),
			       lell_get_channel_k(pkt),
			       CHANNELS[i].idx, CHANNELS[i].k);
			++failures;
		}
		lell_packet_unref(pkt);
	}

	/*
	 * The 40 even frequencies of the band are the 40 channels, each once,
	 * and every frequency in it, odd ones included, decodes as some
	 * channel.
	 */
	memset(seen, 0, sizeof(seen));
	for (mhz = 2400; mhz <= 2480; ++mhz) {
		lell_packet *pkt = NULL;
		unsigned idx;

		lell_allocate_and_decode(ADV_IND_PACKET, mhz, 0, &pkt);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		idx = lell_get_channel_index(pkt);
		if (idx > 39) {
			printf("FAIL %s:%d: %u MHz is channel index %u\n",
			       __FILE__, __LINE__, mhz, idx);
			++failures;
		} else if (mhz >= 2402 && mhz % 2 == 0) {
			++seen[idx];
		}
		lell_packet_unref(pkt);
	}
	for (i = 0; i < sizeof(seen); ++i)
		CHECK(seen[i] == 1);
}

#define SEARCH_LENGTH 24
/* how far into the symbol stream below the sync word starts */
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
		pkt = NULL;
	}

	/* one wrong bit is corrected, and counted */
	stream[PACKET_OFFSET + 20] ^= 1;
	offset = btbb_find_ac(stream, SEARCH_LENGTH, LAP_ANY, 2, &pkt);
	CHECK(offset == PACKET_OFFSET);
	CHECK(pkt != NULL);
	if (pkt != NULL) {
		CHECK(btbb_packet_get_lap(pkt) == KNOWN_SYNCWORDS[0].lap);
		CHECK(btbb_packet_get_ac_errors(pkt) == 1);
		btbb_packet_unref(pkt);
	}
}

/* The eSCO length a caller can give the decoder, through the library: what
   it does with it is tests/test_esco.c's subject, which compiles the decoder
   itself, so this is here to catch the pair not being exported. */
static void test_esco_length_accessors(void)
{
	btbb_packet *pkt = btbb_packet_new();

	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	/* nothing said about the link yet */
	CHECK(btbb_packet_get_esco_length(pkt) == 0);
	btbb_packet_set_esco_length(pkt, 30);
	CHECK(btbb_packet_get_esco_length(pkt) == 30);
	btbb_packet_set_esco_length(pkt, 0);
	CHECK(btbb_packet_get_esco_length(pkt) == 0);

	btbb_packet_unref(pkt);
}

/* The company identifier lookup, called through the library rather than by
 * compiling its table in, which is what a program linking libbtbb does. */
static void test_company_names(void)
{
	const char *name = btbb_company_name(1447);

	CHECK(name != NULL && strcmp(name, "Sonos Inc") == 0);
	CHECK(btbb_company_name(0xFFFE) == NULL);
}

static void test_version_strings(void)
{
	CHECK(btbb_get_version() != NULL && btbb_get_version()[0] != '\0');
	CHECK(btbb_get_release() != NULL && btbb_get_release()[0] != '\0');
}

int main(void)
{
	test_version_strings();
	test_company_names();
	test_le_advertising_packet();
	test_le_channel_index();
	test_bredr_syncword();
	test_esco_length_accessors();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
