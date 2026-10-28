/*
 * Clock recovery (hop reversal) for a BR/EDR piconet, with and without AFH:
 * the candidate CLK1-27 list, the winnowing of it against observed hops, and
 * the AFH detector that runs alongside.
 *
 * The hops the library has to reverse are produced here by single_hop(), which
 * tests/test_hop.c and tests/test_spec_hop.c pin against the Bluetooth Core
 * specification's own definition, so a "truth" piconet standing in for the
 * device under observation hops the way a real one does.
 *
 * The functions under test are internal to the library, so this compiles the
 * translation units they live in, as tests/test_internal.c does.
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
		long e_ = (long) (expected), a_ = (long) (actual);         \
		if (e_ != a_) {                                            \
			printf("FAIL %s:%d: %s is %ld, expected %ld\n",    \
			       __FILE__, __LINE__, #actual, a_, e_);       \
			++failures;                                        \
		}                                                          \
	} while (0)

#define TEST_LAP 0x345678
#define TEST_UAP 0x02

/*
 * A piconet in the state UAP and CLK1-6 discovery leaves behind: the address
 * known, the channels seen so far in the map, and the first packet remembered
 * as hop 0.  btbb_init_hop_reversal() takes it from here.
 */
static btbb_piconet *discovered(const uint8_t *map, int afh)
{
	btbb_piconet *pn = btbb_piconet_new();

	btbb_init_piconet(pn, TEST_LAP);
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	if (map != NULL)
		memcpy(pn->afh_map, map, sizeof(pn->afh_map));
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, afh);
	precalc(pn);
	address_precalc(((TEST_UAP << 24) | TEST_LAP) & 0xfffffff, pn);
	return pn;
}

/* The channel a device with this piconet's address and map hops to. */
static uint8_t channel_at(btbb_piconet *pn, uint32_t clk1_27)
{
	return (uint8_t) single_hop((int) (clk1_27 << 1), pn);
}

/* Remember an observed hop and narrow the candidates with it, as
 * btbb_process_packet() and try_hop() do for a received packet. */
static void observe(btbb_piconet *pn, int offset, uint8_t channel)
{
	btbb_piconet_set_channel_seen(pn, channel);
	pn->pattern_indices[pn->packets_observed] = offset;
	pn->pattern_channels[pn->packets_observed] = channel;
	pn->packets_observed++;
	btbb_winnow(pn);
}

/* The first packet, which btbb_init_hop_reversal() picks the candidates from. */
static void first_hop(btbb_piconet *pn, uint8_t channel)
{
	btbb_piconet_set_channel_seen(pn, channel);
	pn->pattern_indices[0] = 0;
	pn->pattern_channels[0] = channel;
	pn->packets_observed = 1;
}

static int candidate_listed(btbb_piconet *pn, uint32_t clk1_27)
{
	int i;

	for (i = 0; i < pn->num_candidates; i++)
		if (pn->clock_candidates[i] == clk1_27)
			return 1;
	return 0;
}

/* 20 used channels, 0-19, the fewest AFH is defined for. */
static const uint8_t MAP_20[10] = {0xff, 0xff, 0x0f};

/*
 * Under AFH the hop sequence only ever visits the used channels, so a single
 * channel accounts for a far larger share of the sequence than an even spread
 * over all 79 would: with 20 used channels, one in 20 of the clock values
 * scanned rather than one in 79.  The candidate list used to be sized for the
 * even spread, and the scan appended to it without a bound, so these
 * candidates were written about 200 kB past the end of a 212 kB allocation.
 */
static void test_afh_candidates_fit_their_list(void)
{
	/* How the list used to be sized, kept here as what has to be exceeded
	 * for this test to be covering anything. */
	const int old_size = (SEQUENCE_LENGTH / BT_NUM_CHANNELS) / 32;
	btbb_piconet *pn = discovered(MAP_20, 1);
	int count;
	int i;

	CHECK_EQ(AFH_MIN_USED_CHANNELS, pn->used_channels);
	first_hop(pn, channel_at(pn, 0));
	count = btbb_init_hop_reversal(0, pn);
	CHECK_EQ(count, pn->num_candidates);
	CHECK(count > old_size);
	/* Every candidate really does hop to the observed channel, so the list
	 * was filled and not merely allocated large enough. */
	for (i = 0; i < pn->num_candidates; i++)
		CHECK_EQ(pn->pattern_channels[0],
			 channel_at(pn, pn->clock_candidates[i]));
	/* And the one the first hop was taken from is among them. */
	CHECK(candidate_listed(pn, 0));
	btbb_piconet_unref(pn);
}

/*
 * The AFH detector compares a hop with the one before it.  On the first hop
 * there is none, and the look-back used to be guarded by a counter that is
 * always above zero, so it read one element before the array: in the piconet
 * that is the last CLK1-6 candidate, which is -1 once that candidate has been
 * ruled out.  A first packet on channel 0 then matched, and the library
 * latched an AFH map it had never observed, which a later restart turns into a
 * different hop sequence that the clock cannot be found in.
 */
static void test_first_hop_alone_does_not_look_like_afh(void)
{
	btbb_piconet *pn = discovered(NULL, 0);

	/* What CLK1-6 discovery leaves behind once candidate 63 is out. */
	pn->clock6_candidates[63] = -1;
	first_hop(pn, 0);
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(btbb_winnow(pn) > 1);	/* the look-back is reached */
	CHECK(!btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	btbb_piconet_unref(pn);
}

/* Two hops in a row on one channel still look like AFH, from the second hop
 * on, which is where the comparison has something to compare with. */
static void test_two_hops_on_one_channel_look_like_afh(void)
{
	btbb_piconet *pn = discovered(MAP_20, 1);
	uint8_t channel = channel_at(pn, 0);

	first_hop(pn, channel);
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(btbb_winnow(pn) > 1);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	/* The next slot, on the same channel: the AFH same channel mechanism. */
	CHECK_EQ(channel, channel_at(pn, 1));
	observe(pn, 1, channel);
	CHECK(btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	btbb_piconet_unref(pn);
}

/*
 * While the clock is being recovered, every packet may add a channel to the
 * AFH map, and the hops predicted from it change with it.  The predictions
 * used to come from a sequence generated once, when hop reversal started, so
 * from then on the library winnowed against a map it no longer held - and the
 * true clock was dropped along with the rest.
 */
static void test_candidates_follow_the_map_as_it_is_learned(void)
{
	/* What the device really uses: channels 0-23. */
	static const uint8_t map_truth[10] = {0xff, 0xff, 0xff};
	/* What has been seen of it so far: channel 7 not yet. */
	static const uint8_t map_so_far[10] = {0x7f, 0xff, 0xff};
	const uint8_t unseen_channel = 7;
	/* Standing in for the device under observation, hopping its real map. */
	btbb_piconet *device = discovered(map_truth, 1);
	btbb_piconet *pn = discovered(map_so_far, 1);
	uint32_t true_clock = 0;
	uint32_t clock, offset;

	CHECK_EQ(24, device->used_channels);
	CHECK_EQ(23, pn->used_channels);

	/*
	 * A clock where the map as it stands predicts the first hop wrongly -
	 * one channel missing changes the re-mapping of every slot that lands
	 * on an unused channel, not just that one - and where the second hop
	 * observed is the one that completes the map.
	 */
	for (clock = 0x1000000; clock < 0x1000000 + 0x10000; clock++) {
		if (channel_at(device, clock) != channel_at(pn, clock) &&
		    channel_at(device, clock + 4) == unseen_channel) {
			true_clock = clock;
			break;
		}
	}
	CHECK(true_clock != 0);
	pn->first_pkt_time = true_clock;
	pn->clk6_offset = 0;	/* so the known clock bits are true_clock's */

	first_hop(pn, channel_at(device, true_clock));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	/* Not a candidate yet: the map it was chosen under is incomplete. */
	CHECK(!candidate_listed(pn, true_clock));

	/* The device's hops, four slots apart so each is a new slot pair. */
	for (offset = 4; offset < 4 * 40; offset += 4) {
		observe(pn, (int) offset,
			channel_at(device, true_clock + offset));
		if (!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT))
			break;	/* every candidate was dropped */
		if (offset == 4) {
			/* The hop on channel 7 completed the map, and the
			 * candidates were chosen again from it. */
			CHECK_EQ(24, pn->used_channels);
			CHECK(candidate_listed(pn, true_clock));
		}
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;
	}

	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(1, pn->num_candidates);
	CHECK(candidate_listed(pn, true_clock));
	/* clk_offset is in CLKN ticks, so twice the CLK1-27 difference. */
	CHECK_EQ(0, pn->clk_offset);	/* the first packet was the true clock */
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
}

/*
 * The two clock offsets are in different units: clk6_offset in 625 us CLK1-6
 * slots, which is what the candidates' known low bits are counted in, and the
 * public clk_offset in 312.5 us CLKN ticks.  One field carrying both would
 * make the candidates start 6 bits into the wrong clock.
 */
static void test_clk6_offset_fixes_the_known_clock_bits(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	int i;

	pn->first_pkt_time = 0x1000;
	pn->clk6_offset = 0x25;
	/* A CLKN-tick offset, which must not be taken for CLK1-6 slots. */
	pn->clk_offset = 0x4a;
	first_hop(pn, channel_at(pn, (0x1000 + 0x25) & 0x3f));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	for (i = 0; i < pn->num_candidates; i++)
		CHECK_EQ((pn->first_pkt_time + pn->clk6_offset) & 0x3f,
			 pn->clock_candidates[i] & 0x3f);
	btbb_piconet_unref(pn);
}

/*
 * More hops than the remembered pattern has room for restarts discovery, in
 * try_hop() as in btbb_uap_from_header(), rather than writing past the end of
 * the pattern arrays.
 */
static void test_remembered_pattern_is_bounded(void)
{
	char symbols[400];
	btbb_packet *pkt = btbb_packet_new();
	btbb_piconet *pn = discovered(NULL, 0);

	memset(symbols, 0, sizeof(symbols));
	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);

	/* The pattern arrays are full. */
	pn->packets_observed = MAX_PATTERN_LENGTH;
	pn->pattern_channels[0] = 7;	/* a marker just past pattern_indices */
	btbb_packet_set_data(pkt, symbols, (int) sizeof(symbols), 11,
			     (pn->first_pkt_time + 123) << 1);
	try_hop(pkt, pn);

	CHECK_EQ(7, pn->pattern_channels[0]);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK_EQ(0, pn->packets_observed);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

int main(void)
{
	test_afh_candidates_fit_their_list();
	test_first_hop_alone_does_not_look_like_afh();
	test_two_hops_on_one_channel_look_like_afh();
	test_candidates_follow_the_map_as_it_is_learned();
	test_clk6_offset_fixes_the_known_clock_bits();
	test_remembered_pattern_is_bounded();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
