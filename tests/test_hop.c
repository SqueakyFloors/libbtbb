/*
 * BR/EDR connection-state hop selection: single_hop(), with and without AFH.
 *
 * The expected frequencies are the vectors from the Ubertooth project's
 * firmware/tests/test_bluetooth_hop.c, so that libbtbb's host-side
 * predictions and the channels the board tunes to are pinned to one source of
 * truth. They were computed outside either code base, by a direct
 * transcription of the Bluetooth Core specification Vol 2 Part B section 2.6:
 * the basic kernel (Figure 2.16, Table 2.2 connection-state inputs), the AFH
 * re-mapping function (2.6.3.1) and the AFH same channel mechanism (2.6.1).
 *
 * AFH rules being checked:
 *  - a basic-sequence channel that the map marks used is kept unchanged;
 *  - an unused one is replaced from the used channels only, ordered even
 *    channels ascending then odd channels ascending;
 *  - with AFH, the peripheral slot (CLK1 = 1) reuses the central slot channel;
 *  - a map with fewer used channels than AFH is defined for is not adapted to.
 */

#include <stdio.h>
#include <string.h>

/* single_hop() and the tables it reads are internal to the library, so this
 * test compiles the two translation units they live in rather than linking
 * the library, which exports only what btbb.h declares. */
#include "bluetooth_packet.c"
#include "bluetooth_piconet.c"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

#define CHECK_EQ(expected, actual)                                         \
	do {                                                               \
		int e_ = (int) (expected), a_ = (int) (actual);            \
		if (e_ != a_) {                                            \
			printf("FAIL %s:%d: %s is %d, expected %d\n",      \
			       __FILE__, __LINE__, #actual, a_, e_);       \
			++failures;                                        \
		}                                                          \
	} while (0)

/* The address the vectors were computed for. */
#define TEST_LAP 0x345678
#define TEST_UAP 0x02

struct hop_vector {
	uint32_t clock;
	uint16_t basic;	/* MHz with AFH disabled */
	uint16_t afh;	/* MHz with AFH_MAP below */
};

/*
 * Used channels: 22-48 and 72-78 (34 channels), i.e. the 2402-2423 MHz and
 * 2451-2473 MHz ranges avoided, as a host does around two busy Wi-Fi channels.
 * Byte 0 holds channels 0-7, least significant bit first.
 */
static const uint8_t AFH_MAP[10] = {
	0x00, 0x00, 0xc0, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x7f
};

static const struct hop_vector HOP_VECTORS[] = {
	{0x00000000, 2462, 2449},
	{0x00000001, 2462, 2449},
	{0x00000002, 2439, 2449},	/* used, but same channel as slot 0 */
	{0x00000003, 2439, 2449},
	{0x00000004, 2466, 2477},
	{0x0000007e, 2451, 2439},
	{0x00123456, 2429, 2444},
	{0x07fffffe, 2472, 2436},
	{0xffffffff, 2444, 2479},
	{0x00002000, 2456, 2424},
	{0x00045678, 2473, 2478},
	{0x00abcdef, 2436, 2435},
	{0x01000004, 2455, 2448},
	{0x03fffffc, 2450, 2450},	/* used basic channel kept */
	{0x089abcde, 2434, 2435},
	{0x00448d30, 2449, 2449},	/* used basic channel kept */
	{0x0045b07c, 2437, 2437},	/* used basic channel kept */
	{0x004c8444, 2447, 2447},	/* used basic channel kept */
};
#define NUM_VECTORS (sizeof(HOP_VECTORS) / sizeof(HOP_VECTORS[0]))

/* The channel single_hop() returns, as the MHz the vectors are written in. */
static int hop_mhz(uint32_t clock, btbb_piconet *pn)
{
	return 2402 + (int) single_hop((int) clock, pn);
}

/* A piconet with the address and the map already applied. */
static void setup(btbb_piconet *pn, const uint8_t *map, int afh)
{
	memset(pn->afh_map, 0, sizeof(pn->afh_map));
	if (map != NULL)
		memcpy(pn->afh_map, map, sizeof(pn->afh_map));
	btbb_init_piconet(pn, TEST_LAP);
	pn->UAP = TEST_UAP;
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, afh);
	precalc(pn);
	address_precalc(((pn->UAP << 24) | pn->LAP) & 0xfffffff, pn);
}

static int map_marks_used(const uint8_t *map, int mhz)
{
	unsigned int chan = (unsigned int) (mhz - 2402);
	return chan < BT_NUM_CHANNELS && ((map[chan / 8] >> (chan % 8)) & 1);
}

static void test_basic_hops_match_reference(void)
{
	btbb_piconet *pn = btbb_piconet_new();
	size_t i;

	setup(pn, NULL, 0);
	for (i = 0; i < NUM_VECTORS; i++)
		CHECK_EQ(HOP_VECTORS[i].basic,
			 hop_mhz(HOP_VECTORS[i].clock, pn));
	btbb_piconet_unref(pn);
}

static void test_afh_mapping_table_is_used_channels_even_then_odd(void)
{
	static const int expected[] = {
		22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48,
		72, 74, 76, 78,
		23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47,
		73, 75, 77
	};
	btbb_piconet *pn = btbb_piconet_new();
	size_t i;

	setup(pn, AFH_MAP, 1);
	CHECK_EQ(sizeof(expected) / sizeof(expected[0]), pn->used_channels);
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
		CHECK_EQ(expected[i], pn->afh_bank[i]);
	/* The basic table is not overwritten by the AFH table. */
	for (i = 0; i < BT_NUM_CHANNELS; i++)
		CHECK_EQ((i * 2) % BT_NUM_CHANNELS, pn->bank[i]);
	btbb_piconet_unref(pn);
}

static void test_afh_hops_match_reference(void)
{
	btbb_piconet *pn = btbb_piconet_new();
	size_t i;

	setup(pn, AFH_MAP, 1);
	for (i = 0; i < NUM_VECTORS; i++)
		CHECK_EQ(HOP_VECTORS[i].afh,
			 hop_mhz(HOP_VECTORS[i].clock, pn));
	btbb_piconet_unref(pn);
}

static void test_afh_hops_stay_on_used_channels(void)
{
	btbb_piconet *pn = btbb_piconet_new();
	uint32_t clock;
	int off_map = 0, slot_mismatch = 0, kept = 0, mismatched_kept = 0;

	setup(pn, AFH_MAP, 1);
	/* Every central slot in 2^20 clock values, plus its peripheral slot. */
	for (clock = 0; clock < (1u << 20); clock += 4) {
		int afh = hop_mhz(clock, pn);
		int peripheral = hop_mhz(clock | 2, pn);
		int basic;

		if (!map_marks_used(AFH_MAP, afh))
			off_map++;
		if (peripheral != afh)
			slot_mismatch++;
		/* Compare with the basic sequence for the same slot. */
		btbb_piconet_set_flag(pn, BTBB_IS_AFH, 0);
		basic = hop_mhz(clock, pn);
		btbb_piconet_set_flag(pn, BTBB_IS_AFH, 1);
		if (map_marks_used(AFH_MAP, basic)) {
			kept++;
			if (basic != afh)
				mismatched_kept++;
		}
	}
	CHECK_EQ(0, off_map);
	CHECK_EQ(0, slot_mismatch);
	CHECK(kept > 0);
	CHECK_EQ(0, mismatched_kept);
	btbb_piconet_unref(pn);
}

static void test_too_few_used_channels_falls_back_to_basic(void)
{
	/* Channels 0-18: one short of the spec minimum of 20. */
	static const uint8_t small_map[10] = {0xff, 0xff, 0x07};
	/* And one more, which is adapted to. */
	static const uint8_t min_map[10] = {0xff, 0xff, 0x0f};
	btbb_piconet *pn = btbb_piconet_new();
	size_t i;
	int adapted = 0;

	setup(pn, small_map, 1);
	CHECK_EQ(AFH_MIN_USED_CHANNELS - 1, pn->used_channels);
	for (i = 0; i < NUM_VECTORS; i++)
		CHECK_EQ(HOP_VECTORS[i].basic,
			 hop_mhz(HOP_VECTORS[i].clock, pn));

	setup(pn, min_map, 1);
	CHECK_EQ(AFH_MIN_USED_CHANNELS, pn->used_channels);
	for (i = 0; i < NUM_VECTORS; i++)
		if (hop_mhz(HOP_VECTORS[i].clock, pn) != HOP_VECTORS[i].basic)
			adapted++;
	CHECK(adapted > 0);
	btbb_piconet_unref(pn);
}

static void test_empty_map_falls_back_to_basic(void)
{
	btbb_piconet *pn = btbb_piconet_new();
	size_t i;

	/* The AFH flag can be set before any channel has been seen. */
	setup(pn, NULL, 1);
	CHECK_EQ(0, pn->used_channels);
	for (i = 0; i < NUM_VECTORS; i++)
		CHECK_EQ(HOP_VECTORS[i].basic,
			 hop_mhz(HOP_VECTORS[i].clock, pn));
	btbb_piconet_unref(pn);
}

static void test_reserved_map_bit_is_not_counted(void)
{
	uint8_t map[10];
	btbb_piconet *pn = btbb_piconet_new();

	memcpy(map, AFH_MAP, sizeof(map));
	map[9] |= 0x80;	/* bit 79 is reserved; there is no channel 79 */
	setup(pn, map, 1);
	CHECK_EQ(34, pn->used_channels);
	btbb_piconet_unref(pn);
}

/*
 * The public entry point keeps the map, the table and the count in step, and
 * with the address known it leaves the piconet ready to predict hops on the
 * new map: the hops below are asked for without any further setting up.
 */
static void test_set_afh_map_rebuilds_the_table(void)
{
	btbb_piconet *pn = btbb_piconet_new();
	uint8_t map[10];
	size_t i;

	btbb_init_piconet(pn, TEST_LAP);
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, 1);
	memcpy(map, AFH_MAP, sizeof(map));
	btbb_piconet_set_afh_map(pn, map);
	CHECK_EQ(34, pn->used_channels);
	for (i = 0; i < NUM_VECTORS; i++)
		CHECK_EQ(HOP_VECTORS[i].afh,
			 hop_mhz(HOP_VECTORS[i].clock, pn));

	/* Marking one more channel seen keeps them in step too. */
	CHECK_EQ(1, btbb_piconet_set_channel_seen(pn, 0));
	CHECK_EQ(35, pn->used_channels);
	CHECK_EQ(0, pn->afh_bank[0]);
	CHECK_EQ(0, btbb_piconet_set_channel_seen(pn, 0));
	CHECK_EQ(1, btbb_piconet_clear_channel_seen(pn, 0));
	CHECK_EQ(34, pn->used_channels);
	CHECK_EQ(22, pn->afh_bank[0]);
	btbb_piconet_unref(pn);
}

int main(void)
{
	test_basic_hops_match_reference();
	test_afh_mapping_table_is_used_channels_even_then_odd();
	test_afh_hops_match_reference();
	test_afh_hops_stay_on_used_channels();
	test_too_few_used_channels_falls_back_to_basic();
	test_empty_map_falls_back_to_basic();
	test_reserved_map_bit_is_not_counted();
	test_set_afh_map_rebuilds_the_table();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
