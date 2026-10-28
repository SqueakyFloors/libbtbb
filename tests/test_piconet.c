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
 * translation units they live in, as tests/test_internal.c does.  That also
 * puts the allocations they make within reach, which the last few cases need.
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
#include <stdlib.h>
#include <string.h>

/*
 * Every malloc() the library source below makes goes through this, so a test
 * can see how large the request was and make one fail.  The system headers
 * are included first, so that the macro rewrites the library's calls and not
 * malloc()'s own declaration.
 */
static void *counted_malloc(size_t size);
#define malloc counted_malloc

#include "bluetooth_packet.c"
#include "bluetooth_piconet.c"

#undef malloc

static size_t largest_request;	/* bytes, since it was last reset */
static size_t last_request;	/* bytes asked for by the most recent call */
static int refuse_allocations;

static void *counted_malloc(size_t size)
{
	if (size > largest_request)
		largest_request = size;
	last_request = size;
	if (refuse_allocations)
		return NULL;
	return malloc(size);
}

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
	/* The list handed back is exactly as long as the count beside it,
	 * whatever the sizes it grew through on the way: the last allocation
	 * made is that size. */
	CHECK_EQ(sizeof(uint32_t) * (size_t) count, last_request);
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
	CHECK(!candidates_superseded(pn));
	CHECK(candidate_listed(pn, true_clock));
	/* clk_offset is in CLKN ticks, so twice the CLK1-27 difference. */
	CHECK_EQ(0, pn->clk_offset);	/* the first packet was the true clock */
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
}

/*
 * Choosing the candidates again is a walk over 2^21 clock values, and during
 * discovery a channel is seen for the first time often, so it has to happen
 * only when the hops really would be predicted differently: when hop
 * selection is adapted to the map at all, and then only if the map itself
 * changed.
 */
static void test_candidates_rechosen_only_for_a_new_sequence(void)
{
	/* Channels 0-17, below the minimum AFH is defined for. */
	static const uint8_t map_18[10] = {0xff, 0xff, 0x03};
	btbb_piconet *pn = discovered(NULL, 0);

	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	/* Basic hopping: the map does not come into hop selection. */
	CHECK_EQ(1, btbb_piconet_set_channel_seen(pn, 40));
	CHECK(!candidates_superseded(pn));
	btbb_piconet_unref(pn);

	pn = discovered(map_18, 1);
	pn->pattern_channels[0] = channel_at(pn, 0);
	pn->packets_observed = 1;
	CHECK_EQ(18, pn->used_channels);
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	/* One more used channel, still below the minimum: the same basic
	 * sequence, so the candidates stand. */
	CHECK_EQ(1, btbb_piconet_set_channel_seen(pn, 20));
	CHECK_EQ(19, pn->used_channels);
	CHECK(!candidates_superseded(pn));
	/* The one that reaches the minimum starts adapting the sequence. */
	CHECK_EQ(1, btbb_piconet_set_channel_seen(pn, 21));
	CHECK_EQ(AFH_MIN_USED_CHANNELS, pn->used_channels);
	CHECK(candidates_superseded(pn));
	btbb_piconet_unref(pn);

	/* An adapted sequence, and a map differing only in the reserved bit
	 * 79, which there is no channel for. */
	pn = discovered(MAP_20, 1);
	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	pn->afh_map[9] |= 0x80;
	precalc(pn);
	CHECK_EQ(AFH_MIN_USED_CHANNELS, pn->used_channels);
	CHECK(!candidates_superseded(pn));
	btbb_piconet_unref(pn);
}

/*
 * How the candidate list used to be chosen: one walk over the clock values to
 * count the matches, then a second to write them into a list of exactly that
 * size.  The library now makes a single walk, growing the list as it goes,
 * and this is here to say that the candidates it ends up with are the same
 * ones, in the same order.
 */
static int reference_candidates(btbb_piconet *pn, char channel,
				int known_clock_bits, uint32_t **out)
{
	uint32_t clk1_27;
	int count = 0;

	*out = NULL;
	for (clk1_27 = (uint32_t) known_clock_bits; clk1_27 < SEQUENCE_LENGTH;
	     clk1_27 += 0x40) {
		if (observed_channel(clk1_27, pn) == channel)
			count++;
	}
	if (count == 0)
		return 0;
	*out = (uint32_t *) malloc(sizeof(uint32_t) * (size_t) count);
	if (*out == NULL)
		return -1;
	count = 0;
	for (clk1_27 = (uint32_t) known_clock_bits; clk1_27 < SEQUENCE_LENGTH;
	     clk1_27 += 0x40) {
		if (observed_channel(clk1_27, pn) == channel)
			(*out)[count++] = clk1_27;
	}
	return count;
}

/*
 * The single walk finds exactly the candidates the two walks did, for the
 * basic hopping sequence and for an adaptive one, where a single channel
 * accounts for one in 20 of the clock values rather than one in 79 and the
 * list grows several times over.
 */
static void test_one_walk_finds_the_same_candidates(void)
{
	static const int WITH_AFH[] = { 0, 1 };
	size_t row;

	for (row = 0; row < sizeof(WITH_AFH) / sizeof(WITH_AFH[0]); row++) {
		int afh = WITH_AFH[row];
		btbb_piconet *pn = discovered(afh ? MAP_20 : NULL, afh);
		uint32_t *expected = NULL;
		int expected_count;
		int i;

		first_hop(pn, channel_at(pn, 0));
		expected_count = reference_candidates(pn,
						      (char) pn->pattern_channels[0],
						      0, &expected);
		CHECK(expected_count > 0);
		CHECK_EQ(expected_count, btbb_init_hop_reversal(0, pn));
		CHECK_EQ(expected_count, pn->num_candidates);
		if (expected == NULL || pn->clock_candidates == NULL) {
			free(expected);
			btbb_piconet_unref(pn);
			continue;
		}
		for (i = 0; i < expected_count; i++) {
			if (pn->clock_candidates[i] != expected[i]) {
				printf("FAIL candidate %d is 0x%07x, "
				       "expected 0x%07x\n", i,
				       pn->clock_candidates[i], expected[i]);
				++failures;
				break;
			}
		}
		free(expected);
		btbb_piconet_unref(pn);
	}
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
	/* A marker where the write past pattern_indices would land on a
	 * little-endian host; the two flag checks below are the assertion
	 * that holds whatever the byte order. */
	pn->pattern_channels[0] = 7;
	btbb_packet_set_data(pkt, symbols, (int) sizeof(symbols), 11,
			     (pn->first_pkt_time + 123) << 1);
	try_hop(pkt, pn);

	CHECK_EQ(7, pn->pattern_channels[0]);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK_EQ(0, pn->packets_observed);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * Setting an AFH map used to generate the whole hopping sequence when the
 * address was known: a 128 MB allocation and a wait of seconds, which
 * ubertooth-follow paid every time it read an adapter's map, for a sequence
 * nothing ever read.  Hops are predicted one at a time from the map instead,
 * so setting it allocates nothing at all.
 */
static void test_set_afh_map_allocates_nothing(void)
{
	btbb_piconet *pn = discovered(NULL, 1);
	uint8_t map[10];

	memcpy(map, MAP_20, sizeof(map));
	largest_request = 0;
	btbb_piconet_set_afh_map(pn, map);
	CHECK_EQ(0, largest_request);
	/* and the piconet is ready to predict hops on the new map */
	CHECK_EQ(AFH_MIN_USED_CHANNELS, pn->used_channels);
	CHECK_EQ(channel_at(pn, 0), channel_at(pn, 1));
	btbb_piconet_unref(pn);
}

/*
 * The UAP's top bit lands on bit 31 when it is shifted into the address, so
 * it has to be widened first: a signed int cannot hold it.
 */
static void test_a_uap_with_its_top_bit_set(void)
{
	btbb_piconet *pn = discovered(NULL, 1);
	btbb_piconet *same = discovered(NULL, 1);
	uint8_t map[10];

	memcpy(map, MAP_20, sizeof(map));
	btbb_piconet_set_uap(pn, 0xff);
	btbb_piconet_set_afh_map(pn, map);

	/* the same address, from a value that was widened before it was
	 * shifted */
	address_precalc((int) (((((uint32_t) 0xff) << 24) | TEST_LAP) &
			       0xfffffff), same);
	CHECK_EQ(same->a1, pn->a1);
	CHECK_EQ(same->b, pn->b);
	CHECK_EQ(same->c1, pn->c1);
	CHECK_EQ(same->d1, pn->d1);
	CHECK_EQ(same->e, pn->e);
	btbb_piconet_unref(pn);
	btbb_piconet_unref(same);
}

/*
 * The candidate list is the largest allocation clock recovery makes.  A host
 * that cannot spare it is told so, rather than having the failure written
 * through: hop reversal reports no candidates and leaves the piconet with
 * none, and winnowing against no candidates starts discovery over.
 */
static void test_an_allocation_failure_leaves_no_candidates(void)
{
	btbb_piconet *pn = discovered(MAP_20, 1);

	first_hop(pn, channel_at(pn, 0));
	refuse_allocations = 1;
	CHECK_EQ(0, btbb_init_hop_reversal(0, pn));
	refuse_allocations = 0;
	CHECK_EQ(0, pn->num_candidates);
	CHECK(pn->clock_candidates == NULL);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(0, btbb_winnow(pn));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	btbb_piconet_unref(pn);
}

/*
 * With one candidate left, the clock is accepted only once that many further
 * hops have agreed with it.  Here the piconet predicts its own hops, so the
 * candidate that survives is the right one and the only question is when it
 * is accepted.
 *
 * The hops are spread over the clock rather than taken from consecutive
 * slots, as the packets a receiver happens to catch are: two clock values
 * that differ only in the high bits hop alike for a long run of slots, so
 * consecutive slots hardly narrow the list at all.
 */
static void test_the_clock_is_confirmed_before_it_is_accepted(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	uint32_t offset;
	int hops_with_one_candidate = 0;

	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	for (offset = 100; offset < 100 * 40; offset += 100) {
		observe(pn, (int) offset, channel_at(pn, offset));
		if (pn->num_candidates == 1)
			hops_with_one_candidate++;
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;
	}
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	/* the hop that chose it, and then the ones that confirmed it */
	CHECK_EQ(CLK27_CONFIRM_HOPS + 1, hops_with_one_candidate);
	CHECK_EQ(0, pn->clock_candidates[0]);
	CHECK_EQ(0, pn->clk_offset);
	btbb_piconet_unref(pn);
}

/*
 * A clock that is not the piconet's can match every hop observed so far while
 * the AFH map is still a channel short, and be the last candidate standing.
 * Accepting it there starts following the piconet on the wrong offset, after
 * which nothing winnows again: every packet fails to decode, forever.
 *
 * The device here hops on channels 0-23 and the observer has yet to see
 * channel 7.  At this clock the candidate that stands alone partway through
 * is a wrong one, and the hop that follows is the one that completes the map,
 * which chooses the candidates again from the map as it then stands.  The
 * clock accepted in the end is the piconet's.
 */
static void test_a_wrong_clock_is_not_accepted_on_a_short_map(void)
{
	/* What the device really uses: channels 0-23. */
	static const uint8_t map_truth[10] = {0xff, 0xff, 0xff};
	/* What has been seen of it so far: channel 7 not yet. */
	static const uint8_t map_so_far[10] = {0x7f, 0xff, 0xff};
	/* Found by running this scenario over a range of clock values. */
	const uint32_t true_clock = 0x1000000;
	btbb_piconet *device = discovered(map_truth, 1);
	btbb_piconet *pn = discovered(map_so_far, 1);
	uint32_t offset;
	int wrong_candidate_stood_alone = 0;
	int accepted_a_wrong_clock = 0;

	pn->first_pkt_time = true_clock;
	pn->clk6_offset = 0;	/* so the known clock bits are true_clock's */
	first_hop(pn, channel_at(device, true_clock));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);

	/* The device's hops, four slots apart so each is a new slot pair. */
	for (offset = 4; offset <= 4 * 40; offset += 4) {
		observe(pn, (int) offset,
			channel_at(device, true_clock + offset));
		if (!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT))
			break;	/* every candidate was dropped */
		if (pn->num_candidates == 1 &&
		    pn->clock_candidates[0] != true_clock)
			wrong_candidate_stood_alone = 1;
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
			if (pn->clock_candidates[0] != true_clock)
				accepted_a_wrong_clock = 1;
			break;
		}
	}

	/* the scenario really does leave a wrong candidate on its own */
	CHECK(wrong_candidate_stood_alone);
	CHECK_EQ(0, accepted_a_wrong_clock);
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(1, pn->num_candidates);
	CHECK_EQ(true_clock, pn->clock_candidates[0]);
	CHECK_EQ(24, pn->used_channels);	/* the map completed on the way */
	CHECK_EQ(0, pn->clk_offset);	/* the first packet was the true clock */
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
}

int main(void)
{
	test_afh_candidates_fit_their_list();
	test_first_hop_alone_does_not_look_like_afh();
	test_two_hops_on_one_channel_look_like_afh();
	test_candidates_follow_the_map_as_it_is_learned();
	test_candidates_rechosen_only_for_a_new_sequence();
	test_one_walk_finds_the_same_candidates();
	test_clk6_offset_fixes_the_known_clock_bits();
	test_remembered_pattern_is_bounded();
	test_set_afh_map_allocates_nothing();
	test_a_uap_with_its_top_bit_set();
	test_an_allocation_failure_leaves_no_candidates();
	test_the_clock_is_confirmed_before_it_is_accepted();
	test_a_wrong_clock_is_not_accepted_on_a_short_map();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
