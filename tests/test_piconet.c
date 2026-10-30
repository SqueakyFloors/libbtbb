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

/*
 * calloc() is rewritten separately, with a refusal flag of its own and no
 * counting, so that a test can fail an allocation made with calloc() -
 * btbb_piconet_new()'s, and btbb_packet_new()'s in the other translation unit
 * below - without disturbing the malloc() counts the tests below assert on.
 */
static void *refusable_calloc(size_t count, size_t size);
#define calloc refusable_calloc

/*
 * free() is counted, so that a test can say an allocation was given back on a
 * path where nothing else can see it. A block a failed function leaves behind
 * is not always a leak a sanitizer reports: the pointer can still be sitting
 * in a stack slot, which LeakSanitizer scans as a root.
 */
static void counted_free(void *block);
#define free counted_free

#include "bluetooth_packet.c"
#include "bluetooth_piconet.c"

#undef malloc
#undef calloc
#undef free

#include "stream_capture.h"

static size_t largest_request;	/* bytes, since it was last reset */
static int allocation_calls;	/* calls, since it was last reset */
static int refuse_allocations;
static int refuse_from;		/* refuse this call onward, 0 to refuse none */
static int refuse_callocs;
static int free_calls;		/* calls, since the program started */

/*
 * The size each live allocation was asked for, so a test can ask how long the
 * block a function handed back really is rather than guess from the order the
 * allocations were made in.
 */
#define REMEMBERED_ALLOCATIONS 64
static struct {
	void *address;
	size_t size;
} remembered[REMEMBERED_ALLOCATIONS];
static int remembered_next;

static size_t size_allocated(const void *address)
{
	int i, at;

	if (address == NULL)
		return 0;
	/* newest first, so a freed block whose address was handed out again
	 * answers with the size it was asked for the second time */
	for (i = 1; i <= REMEMBERED_ALLOCATIONS; i++) {
		at = (remembered_next - i + REMEMBERED_ALLOCATIONS) %
			REMEMBERED_ALLOCATIONS;
		if (remembered[at].address == address)
			return remembered[at].size;
	}
	return 0;
}

static void *counted_malloc(size_t size)
{
	void *block;

	if (size > largest_request)
		largest_request = size;
	allocation_calls++;
	if (refuse_allocations ||
	    ((refuse_from != 0) && (allocation_calls >= refuse_from)))
		return NULL;
	block = malloc(size);
	if (block != NULL) {
		remembered[remembered_next].address = block;
		remembered[remembered_next].size = size;
		remembered_next = (remembered_next + 1) %
			REMEMBERED_ALLOCATIONS;
	}
	return block;
}

static void *refusable_calloc(size_t count, size_t size)
{
	if (refuse_callocs)
		return NULL;
	return calloc(count, size);
}

static void counted_free(void *block)
{
	free_calls++;
	free(block);
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

/* Start capturing both streams, counting a failure to do so as one. */
static int capture_or_fail(void)
{
	if (capture_start() == 0)
		return 0;
	printf("FAIL: could not capture the output\n");
	++failures;
	return -1;
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
	 * whatever the sizes it grew through on the way. */
	CHECK_EQ(sizeof(uint32_t) * (size_t) count,
		 size_allocated(pn->clock_candidates));
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
 * Which two slots the repeated channel has to be in.  Within a slot pair the
 * basic sequence cannot repeat a channel, so a repeat there is adapted hopping
 * and nothing else.  Across a pair boundary - a peripheral slot and the next
 * pair's central slot, also one slot apart - it can, and for some addresses
 * often, so a repeat there says nothing.
 */
static void test_only_a_repeat_inside_a_slot_pair_means_adapted_hopping(void)
{
	/* An address where the basic sequence does repeat across a boundary,
	 * and the first clock at which it does. */
	const uint32_t cross_lap = 0x345678;
	const uint8_t cross_uap = 0x02;
	const uint32_t cross_clk = 1049087;
	btbb_piconet *pn = btbb_piconet_new();
	uint32_t pair;
	int repeats_inside = 0;

	btbb_init_piconet(pn, cross_lap);
	btbb_piconet_set_uap(pn, cross_uap);
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, 0);
	precalc(pn);
	address_precalc((int) ((((uint32_t) cross_uap << 24) | cross_lap) &
			       0xfffffff), pn);

	/* The peripheral slot of this pair is elsewhere... */
	CHECK(channel_at(pn, cross_clk - 1) != channel_at(pn, cross_clk));
	/* ...and the next pair's central slot repeats it. */
	CHECK_EQ(channel_at(pn, cross_clk), channel_at(pn, cross_clk + 1));
	/* So the clock is in a peripheral slot, which is what tells the two
	 * cases apart. */
	pn->clk6_offset = 0;
	pn->first_pkt_time = cross_clk;
	CHECK(!central_slot(pn, 0));

	/* Inside a pair it never happens, over a sample of this address. */
	for (pair = 0; pair < (1u << 14); pair++)
		if (channel_at(pn, 2 * pair) == channel_at(pn, 2 * pair + 1))
			repeats_inside++;
	CHECK_EQ(0, repeats_inside);
	btbb_piconet_unref(pn);
}

/*
 * A repeat across a pair boundary is not evidence, so it must not flip the
 * piconet to adapted hopping: doing that to a piconet hopping basically
 * leaves it predicting a sequence the piconet does not follow, for the rest
 * of the run, since neither flag is ever cleared.
 */
static void test_a_repeat_across_a_pair_boundary_is_not_adapted_hopping(void)
{
	static const uint8_t full_map[10] = {0xff, 0xff, 0xff, 0xff, 0xff,
					     0xff, 0xff, 0xff, 0xff, 0x7f};
	const uint32_t cross_lap = 0x345678;
	const uint8_t cross_uap = 0x02;
	const uint32_t cross_clk = 1049087;	/* a peripheral slot */
	btbb_piconet *pn = btbb_piconet_new();
	uint8_t channel;

	btbb_init_piconet(pn, cross_lap);
	btbb_piconet_set_uap(pn, cross_uap);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	memcpy(pn->afh_map, full_map, sizeof(pn->afh_map));
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, 0);
	precalc(pn);
	address_precalc((int) ((((uint32_t) cross_uap << 24) | cross_lap) &
			       0xfffffff), pn);
	pn->first_pkt_time = cross_clk;
	pn->clk6_offset = 0;

	channel = channel_at(pn, cross_clk);
	first_hop(pn, channel);
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(candidate_listed(pn, cross_clk));

	/* one slot later, same channel, but the next pair's central slot */
	observe(pn, 1, channel);

	CHECK(!btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_IS_AFH));
	/* The search carries on against the basic sequence it was right to
	 * use, with the piconet's clock still among the candidates. */
	CHECK(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK(candidate_listed(pn, cross_clk));
	btbb_piconet_unref(pn);
}

/*
 * A device using adapted hopping, watched by a piconet that has not adapted
 * yet.  Its candidates come from the basic sequence, so the device's clock is
 * not among them, and the hop that proves the sequence is adapted - a
 * peripheral slot repeating its central slot's channel - is also the hop that
 * rules every candidate out.  The evidence has to be read before the
 * comparison, or it goes with the list: the search starts over on the basic
 * sequence and can do that for the whole of a capture without ever adapting.
 */
static void test_an_adapted_sequence_is_recognized_before_its_hops_are_dropped(void)
{
	/* Channels 0-39 used, as a host writes a map around a busy band. */
	static const uint8_t map_truth[10] = {0xff, 0xff, 0xff, 0xff, 0xff};
	btbb_piconet *device = discovered(map_truth, 1);
	btbb_piconet *pn = discovered(map_truth, 0);
	uint32_t true_clock = 0;
	uint32_t clock, offset;
	uint8_t channel;

	CHECK_EQ(40, device->used_channels);
	/* The same channels seen, but hop selection not adapted to them. */
	CHECK_EQ(40, pn->used_channels);

	/* A central slot whose peripheral slot repeats its channel, which the
	 * observer's basic sequence predicts somewhere else. */
	for (clock = 0x1000000; clock < 0x1000000 + 0x1000; clock += 2)
		if (channel_at(device, clock) == channel_at(device, clock + 1) &&
		    channel_at(pn, clock) != channel_at(pn, clock + 1)) {
			true_clock = clock;
			break;
		}
	CHECK(true_clock != 0);
	pn->first_pkt_time = true_clock;
	pn->clk6_offset = 0;	/* so the known clock bits are true_clock's */

	channel = channel_at(device, true_clock);
	first_hop(pn, channel);
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(!candidate_listed(pn, true_clock));

	/* the peripheral slot, on the same channel */
	observe(pn, 1, channel);

	CHECK(btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	CHECK(btbb_piconet_get_flag(pn, BTBB_IS_AFH));
	/* The search is still running, on candidates chosen from the adapted
	 * sequence, and the device's clock is one of them. */
	CHECK(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK(pn->num_candidates > 1);
	CHECK(candidate_listed(pn, true_clock));

	/* and the rest of the device's hops settle on it */
	for (offset = 2; offset < 2 * 60; offset += 2) {
		observe(pn, (int) offset,
			channel_at(device, true_clock + offset));
		if (!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT))
			break;	/* every candidate was dropped */
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;
	}
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(1, pn->num_candidates);
	CHECK(candidate_listed(pn, true_clock));
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
}

/*
 * The same recognition, with the pair arriving well into the remembered hops
 * rather than right after the first one.  Choosing the candidates again starts
 * the walk of those hops over, and this says every one of them is compared
 * against the new list exactly once: a walk that skipped the first hop, or
 * compared one twice, would leave the clock out or the walk short.
 *
 * The device uses all 79 channels, which an adapted piconet may: a central
 * slot then hops exactly where the basic sequence says, so the earlier hops
 * do not contradict the candidates and the list is still alive when the pair
 * arrives.  The hops are a whole CLK1-6 period apart, which narrows the list
 * gently enough to leave it alive for six hops.
 */
static void test_choosing_the_candidates_again_recompares_every_hop(void)
{
	static const uint8_t full_map[10] = {0xff, 0xff, 0xff, 0xff, 0xff,
					     0xff, 0xff, 0xff, 0xff, 0x7f};
	btbb_piconet *device = discovered(full_map, 1);
	btbb_piconet *pn = discovered(full_map, 0);
	const uint32_t true_clock = 0x1000000;	/* even: a central slot */
	uint32_t offset;
	int hops = 0;

	CHECK_EQ(BT_NUM_CHANNELS, device->used_channels);
	pn->first_pkt_time = true_clock;
	pn->clk6_offset = 0;

	first_hop(pn, channel_at(device, true_clock));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	hops = 1;
	/* Chosen from the basic sequence, and the clock is among them: a
	 * central slot of an all-channels adapted sequence is where the basic
	 * sequence puts it. */
	CHECK(candidate_listed(pn, true_clock));

	for (offset = 64; offset <= 256; offset += 64) {
		CHECK_EQ(channel_at(device, true_clock + offset),
			 channel_at(pn, true_clock + offset));
		observe(pn, (int) offset, channel_at(device, true_clock + offset));
		hops++;
		CHECK(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
		CHECK(!btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	}
	CHECK_EQ(5, hops);
	CHECK(candidate_listed(pn, true_clock));

	/* The peripheral slot answering the hop at offset 256, on its channel:
	 * the sixth remembered hop, so the walk has five to start over on. */
	CHECK(central_slot(pn, 256));
	observe(pn, 257, channel_at(device, true_clock + 256));
	hops++;

	CHECK(btbb_piconet_get_flag(pn, BTBB_IS_AFH));
	CHECK(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK(candidate_listed(pn, true_clock));
	/* Every remembered hop was compared against the new list, and none of
	 * them twice. */
	CHECK_EQ(hops, pn->packets_observed);
	CHECK_EQ(pn->packets_observed, pn->winnowed);
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
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
 * An allocation the library cannot make is reported, rather than written
 * through: every one of these used to be a crash on a host out of memory.
 *
 * The access code error correction table is the one with a choice in it, and
 * it is thrown away whole. Half a table has no sign on it, and the sync word
 * search built on it would quietly stop putting right the error patterns the
 * missing entries describe, so btbb_init() says it could not do what it was
 * asked and leaves no table behind.
 */
static void test_a_failed_allocation_is_reported(void)
{
	btbb_packet *pkt = btbb_packet_new();
	char block[15];
	char out[4096], err[4096];
	int init_result;

	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	memset(block, 0, sizeof(block));

	/* no table yet, so btbb_init() below builds one rather than finding
	 * one already there */
	CHECK(syndrome_map == NULL);

	/* The streams are captured around btbb_init() alone, which is the
	 * only one of these that says anything, and nothing is checked while
	 * they are: a capture held across a check swallows its report, and
	 * across a crash loses the lot. */
	if (capture_or_fail() != 0) {
		btbb_packet_unref(pkt);
		return;
	}
	/* Part of the table is built before the refusal, so the entries it
	 * did make are the ones thrown away. Refusing the first allocation
	 * would leave nothing to throw away and the discard untested. */
	allocation_calls = 0;
	refuse_from = 100;
	init_result = btbb_init(2);
	refuse_from = 0;
	capture_stop(out, sizeof(out), err, sizeof(err));

	/* It got that far, so there was a part-built table to throw away.
	 * That the entries themselves are freed is held down by the leak
	 * check of the sanitizer build rather than from here: a test cannot
	 * see a block go. */
	CHECK(allocation_calls >= 100);
	CHECK_EQ(-1, init_result);
	CHECK(syndrome_map == NULL);
	CHECK(strstr(err, "no memory for the access code") != NULL);
	CHECK_EQ(0, strlen(out));

	refuse_allocations = 1;
	refuse_callocs = 1;

	/* a piconet */
	CHECK(btbb_piconet_new() == NULL);

	/* the rate 2/3 FEC decoder, whose callers already report no payload
	 * for a block they could not put right */
	CHECK(unfec23(block, 10, NULL) == NULL);

	/* the buffer the tun format is built in */
	CHECK(tun_format(pkt) == NULL);

	refuse_allocations = 0;
	refuse_callocs = 0;

	/* and the table is built once there is memory for it */
	CHECK_EQ(0, btbb_init(2));
	CHECK(syndrome_map != NULL);

	btbb_packet_unref(pkt);
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
 * The candidate list grows several times over for an adaptive piconet, so an
 * allocation can fail with a list already in hand.  That list is let go of,
 * and the piconet is left with no candidates, as it is when the first
 * allocation fails.
 */
static void test_an_allocation_failure_partway_through_the_list(void)
{
	btbb_piconet *pn = discovered(MAP_20, 1);

	first_hop(pn, channel_at(pn, 0));
	allocation_calls = 0;
	/* the list has been allocated and grown at least once by then */
	refuse_from = 3;
	CHECK_EQ(0, btbb_init_hop_reversal(0, pn));
	refuse_from = 0;
	CHECK(allocation_calls >= 3);
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

/*
 * How far apart the device's clock and the observer's read in the test below,
 * in 625 us slots. Large enough that every clock the library reads would move
 * if it were read as 312.5 us CLKN ticks instead.
 */
#define CLOCKS_APART 0x0123456

/*
 * Clock recovery works in one unit throughout: CLK1-27, the 625 us slot
 * count, which is what a packet carries and what first_pkt_time, the hop
 * differences and the candidates are all in. Only the clock offset handed
 * back to the caller is in 312.5 us CLKN ticks, because that is what a
 * receiver hops on.
 *
 * Everywhere else the two clocks in play are the same value, so a unit read
 * wrongly cancels out: the piconet standing in for the device shares the
 * library's conventions, and where first_pkt_time equals the clock being
 * recovered their difference is zero whichever unit it is read in. Here the
 * device's clock and the observer's are a long way apart, so the difference
 * is real: reading it in the wrong unit moves every predicted hop and the
 * recovered clock would not be the device's.
 */
static void test_the_clocks_of_the_device_and_the_observer_differ(void)
{
	/* Standing in for the device: the same address and map, its own clock. */
	btbb_piconet *device = discovered(NULL, 0);
	btbb_piconet *pn = discovered(NULL, 0);
	const uint32_t observer_first = 0x0300000;
	const uint32_t device_first = observer_first + CLOCKS_APART;
	uint32_t offset = 0;

	pn->first_pkt_time = observer_first;
	/* What CLK1-6 discovery found: the two clocks' difference in slots. */
	pn->clk6_offset = (int) (CLOCKS_APART & 0x3f);

	first_hop(pn, channel_at(device, device_first));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	/* Every candidate's CLK1-6 is the device's, not the observer's. */
	CHECK_EQ(device_first & 0x3f, pn->clock_candidates[0] & 0x3f);
	CHECK(candidate_listed(pn, device_first));

	/*
	 * The device's hops, at uneven intervals as the packets a receiver
	 * happens to catch are: two clocks differing only in their high bits
	 * hop alike for a long run of consecutive slots.
	 */
	while (pn->packets_observed < MAX_PATTERN_LENGTH) {
		offset += 100 + 37 * (uint32_t) pn->packets_observed;
		observe(pn, (int) offset, channel_at(device, device_first + offset));
		if (!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT))
			break;	/* every candidate was dropped */
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;
	}

	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(1, pn->num_candidates);
	CHECK_EQ(device_first, pn->clock_candidates[0]);
	/* The offset the caller hops on is in CLKN ticks, so twice the slots
	 * between the two clocks. */
	CHECK_EQ(2 * CLOCKS_APART, btbb_piconet_get_clk_offset(pn));
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
}

/*
 * What a wrong candidate costs, and what it does not. Dewhitening and the
 * header check use CLK1-6 only, and every candidate carries the CLK1-6 that
 * CRC-confirmed discovery found, so the low 7 bits of the CLKN offset are the
 * same for all of them. Following a wrong candidate therefore still decodes
 * the packets it does receive; what it gets wrong is which channel to listen
 * on, so it receives almost none.
 */
static void test_every_candidate_agrees_on_clk1_6(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	const uint32_t first_pkt_time = 0x0300000;
	const uint32_t true_clock = first_pkt_time + CLOCKS_APART;
	int true_offset = (int) ((true_clock << 1) - (first_pkt_time << 1));
	int i;

	pn->first_pkt_time = first_pkt_time;
	pn->clk6_offset = (int) (CLOCKS_APART & 0x3f);
	first_hop(pn, channel_at(pn, true_clock));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(candidate_listed(pn, true_clock));
	for (i = 0; i < pn->num_candidates; i++) {
		int offset = (int) ((pn->clock_candidates[i] << 1) -
				    (first_pkt_time << 1));

		CHECK_EQ(true_offset & 0x7f, offset & 0x7f);
	}
	btbb_piconet_unref(pn);
}

/* ------------------ the clock offset the search recovers ----------------- */

/*
 * The Bluetooth Core specification's own DH1, Vol 2, Part G, Section 6.1:
 * header data 0x123 with the HEC 0x06 that goes with a UAP of 0x47, which is
 * LT_ADDR 3 and TYPE DH1, and a payload of five data bytes whose CRC comes
 * out under that same UAP.  The payload is the symbols that go on air, least
 * significant bit first, before whitening.
 */
#define SPEC_DH1_LAP 0x9e8b33
#define SPEC_DH1_UAP 0x47
#define SPEC_DH1_HEADER_DATA 0x123
#define SPEC_DH1_HEC 0x06
static const char SPEC_DH1_PAYLOAD[] =
	"01110100"
	"10000000" "01000000" "11000000" "00100000" "10100000"
	"1110110000110110";

/*
 * Lay down the start of a BR/EDR packet as it goes on air: the 64-bit sync
 * word of the LAP, the access code trailer, and the 18 header bits each sent
 * three times, which is the rate 1/3 code of Vol 2, Part B, Section 7.4.
 *
 * On air the header is whitened before it is encoded, with the sequence the
 * receive clock picks out, and the decoder undoes it the other way round.
 * Whitening here is what lets UAP discovery tell one candidate clock from
 * another: each wrong clock reads a different header, and so a different UAP,
 * from the same symbols.  A whiten_clock below zero leaves the header
 * unwhitened, so it reads back as it was built whatever clock the decoder
 * tries.  Returns the symbols used, which is where a payload starts.
 */
static int lay_out_header(char *stream, uint32_t lap, uint16_t header_data,
			  uint8_t hec, int whiten_clock)
{
	uint64_t syncword = btbb_gen_syncword(lap);
	uint32_t header = (uint32_t) header_data | ((uint32_t) hec << 10);
	int i;

	for (i = 0; i < 64; i++)
		stream[i] = (char) ((syncword >> i) & 1);
	for (i = 0; i < 4; i++)
		stream[64 + i] = (char) (((syncword >> 63) ^ (i + 1)) & 1);
	for (i = 0; i < 18; i++) {
		char bit = (char) ((header >> i) & 1);

		if (whiten_clock >= 0)
			bit ^= (char) WHITENING_DATA[
				(INDICES[whiten_clock & 0x3f] + i) % 127];
		stream[68 + i * 3] = bit;
		stream[69 + i * 3] = bit;
		stream[70 + i * 3] = bit;
	}
	return 122;
}

/*
 * That packet as it reaches a receiver that is listening at a given clock.
 * One run of the whitening sequence covers the header and the payload with no
 * reinitializing in between (Vol 2, Part B, Section 7.2), so the payload
 * starts 18 bits along it.  Returns the number of symbols.
 */
static int spec_dh1_symbols(char *stream, int clock)
{
	int length, i;

	memset(stream, 0, MAX_SYMBOLS);
	length = lay_out_header(stream, SPEC_DH1_LAP, SPEC_DH1_HEADER_DATA,
				SPEC_DH1_HEC, clock);

	for (i = 0; SPEC_DH1_PAYLOAD[i] != '\0'; i++)
		stream[length + i] = (char)
			((SPEC_DH1_PAYLOAD[i] - '0') ^
			 WHITENING_DATA[(INDICES[clock & 0x3f] + 18 + i) % 127]);
	return length + i;
}

/* That packet handed to the decoder at the clock a board read it at. */
static btbb_packet *spec_dh1_packet(char *stream, int length, uint32_t clkn)
{
	btbb_packet *pkt = NULL;

	/* finding the access code is what gives the packet its LAP */
	if (btbb_find_ac(stream, 1, SPEC_DH1_LAP, 0, &pkt) != 0)
		return NULL;
	if (pkt == NULL)
		return NULL;
	btbb_packet_set_data(pkt, stream, length, 0, clkn);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);
	return pkt;
}

/*
 * The packet whose CRC settles the address part is the one a reader most
 * needs, because it is the first evidence that address part was derived from,
 * and a capture tool records its header by reading it again at the piconet's
 * clock.  That clock is the one the search settled on, which the caller gets
 * as the offset from the clock its board read the packet at: at the board's
 * own clock the same symbols dewhiten to a different header, so the record
 * would carry 18 bits of a header nobody sent, failing their own check.
 */
static void test_the_settled_offset_reads_the_packets_own_header(void)
{
	/* The piconet's CLK1-6 is 37 and the board's own clock reads 5, so the
	 * two are 32 slots apart.  A board hears a piconet on its own clock,
	 * and the two run some fixed number of slots apart until the follow
	 * starts, so a fixture whose clocks agreed would not notice the offset
	 * being dropped. */
	const int piconet_clk6 = 0x25;
	const uint32_t board_clkn = 10;
	char stream[MAX_SYMBOLS];
	btbb_piconet *pn = btbb_piconet_new();
	btbb_packet *pkt;
	int length, offset;

	CHECK(pn != NULL);
	if (pn == NULL)
		return;
	btbb_init_piconet(pn, SPEC_DH1_LAP);
	length = spec_dh1_symbols(stream, piconet_clk6);
	pkt = spec_dh1_packet(stream, length, board_clkn);
	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}

	/* The CRC comes out at one trial clock, which settles both the
	 * address part and CLK1-6. */
	CHECK_EQ(1, btbb_uap_from_header(pkt, pn));
	CHECK_EQ(SPEC_DH1_UAP, btbb_piconet_get_uap(pn));
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK6_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));

	/* The offset is reported from here on, in the 625 us slots CLK1-6
	 * counts, because that is the clock the search has. */
	offset = btbb_piconet_get_clk_offset(pn);
	CHECK_EQ((piconet_clk6 - (int) ((board_clkn >> 1) & 0x3f)) & 0x3f,
		 offset);

	/* What a capture tool does with it: the packet is handed back its own
	 * symbols at the piconet's clock, which is the clock it read them at
	 * plus twice the offset, CLKN counting half-slots, and the header is
	 * read again. */
	btbb_packet_set_uap(pkt, btbb_piconet_get_uap(pn));
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_data(pkt, stream, length, 0,
			     board_clkn + 2 * (uint32_t) offset);
	CHECK_EQ(piconet_clk6, pkt->clkn & 0x3f);
	CHECK_EQ(1, btbb_decode_header(pkt));
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED));
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID));

	/* and the header it read is the packet's own, check bits and all */
	CHECK_EQ(SPEC_DH1_HEADER_DATA, air_to_host16(pkt->packet_header, 10));
	CHECK_EQ(SPEC_DH1_HEC, pkt->packet_hec);
	CHECK_EQ(3, pkt->packet_lt_addr);
	CHECK_EQ(PACKET_TYPE_DH1, pkt->packet_type);

	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * The trial clock whose CRC comes out is the packet's own decode, so the
 * packet comes out of the search carrying both halves of what that trial
 * read: the payload, and the header it was unwhitened with.  A caller writing
 * a capture record needs no second read of its own, and nothing it could do
 * would keep both - handing the packet its symbols again at the piconet's
 * clock is what reads the header, and that is also what drops the payload.
 */
static void test_the_settling_packet_carries_its_own_header(void)
{
	/* whitened at the piconet's CLK1-6 of 37, read by a board whose own
	 * clock says 5: the header only reads at the piconet's */
	const int piconet_clk6 = 0x25;
	const uint32_t board_clkn = 10;
	char stream[MAX_SYMBOLS];
	btbb_piconet *pn = btbb_piconet_new();
	btbb_packet *pkt;
	int length;

	CHECK(pn != NULL);
	if (pn == NULL)
		return;
	btbb_init_piconet(pn, SPEC_DH1_LAP);
	length = spec_dh1_symbols(stream, piconet_clk6);
	pkt = spec_dh1_packet(stream, length, board_clkn);
	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}

	CHECK_EQ(1, btbb_uap_from_header(pkt, pn));
	CHECK_EQ(SPEC_DH1_UAP, btbb_piconet_get_uap(pn));

	/* the header the settling trial read, with its check bits holding
	 * under the address part it gave */
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED));
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID));
	CHECK_EQ(SPEC_DH1_HEADER_DATA, air_to_host16(pkt->packet_header, 10));
	CHECK_EQ(SPEC_DH1_HEC, pkt->packet_hec);
	CHECK_EQ(3, pkt->packet_lt_addr);
	CHECK_EQ(PACKET_TYPE_DH1, pkt->packet_type);
	CHECK_EQ(0, pkt->corrected_header_bits);

	/* and the payload that trial decoded, under the CRC answer it gave:
	 * reading the header has not cost the record its payload */
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD));
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED));
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT));
	CHECK(btbb_packet_get_payload_length(pkt) > 0);

	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/* ------------------- giving up a clock that does not work ---------------- */

/*
 * The HEC that goes with this header data and this UAP. The decoder recovers
 * a UAP from a header's HEC, and the Bluetooth specification's own sample
 * data pins that in tests/test_spec_bredr.c, so trying all 256 of them here
 * saves writing a second copy of the generator.
 */
static uint8_t hec_for(uint16_t header_data, uint8_t uap)
{
	int hec;

	for (hec = 0; hec < 256; hec++) {
		if (uap_from_hec(header_data, (uint8_t) hec) == uap)
			return (uint8_t) hec;
	}
	return 0;
}

/*
 * A BR/EDR packet as one reaches btbb_process_packet(): the access code for
 * the given LAP, the access code trailer, and the 18 header bits each sent
 * three times, which is the rate 1/3 code of Vol 2, Part B, Section 7.4.
 * Nothing is whitened, so the header reads back as it was built whatever
 * clock the decoder is given.
 */
static btbb_packet *packet_of_at_clock(uint32_t lap, uint8_t channel,
				       uint16_t header_data, uint8_t hec,
				       int length, int whiten_clock)
{
	char stream[400];
	btbb_packet *pkt = NULL;
	int i;

	memset(stream, 0, sizeof(stream));
	lay_out_header(stream, lap, header_data, hec, whiten_clock);
	/* Where a payload belongs, alternating symbols: a payload header that
	 * claims a length, and bits under it that no CRC will check out. */
	for (i = 122; i < length; i++)
		stream[i] = (char) (i & 1);
	/* finding the access code is what gives the packet its LAP */
	if (btbb_find_ac(stream, 1, lap, 0, &pkt) != 0)
		return NULL;
	if (pkt == NULL)
		return NULL;
	/* btbb_packet_set_data() is given the 312.5 us clock and keeps the
	 * 625 us slot count, so the clock the header was whitened for is
	 * handed over doubled */
	btbb_packet_set_data(pkt, stream, length, channel,
			     (whiten_clock >= 0) ?
			     (uint32_t) (whiten_clock << 1) : 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, whiten_clock >= 0);
	return pkt;
}

/* A packet whose header is not whitened, so it reads back as it was built
 * whatever clock the decoder tries. */
static btbb_packet *packet_of(uint32_t lap, uint8_t channel,
			      uint16_t header_data, uint8_t hec, int length)
{
	return packet_of_at_clock(lap, channel, header_data, hec, length, -1);
}

/* The same packet, of the one piconet the tests above follow. */
static btbb_packet *followed_packet(uint16_t header_data, uint8_t hec,
				    int length)
{
	return packet_of(TEST_LAP, 0, header_data, hec, length);
}

/* An lt_addr of 1 and the packet type NULL, which has no payload to decode. */
#define FOLLOWED_HEADER (1 | (PACKET_TYPE_NULL << 3))
/* The same, as a DM1, whose payload carries a rate 2/3 code and a CRC. */
#define FOLLOWED_DM1_HEADER (1 | (PACKET_TYPE_DM1 << 3))

/* The UAP a header built for another piconet checks out against, and never
 * this piconet's. */
#define OTHER_UAP (TEST_UAP ^ 0xff)

/* A packet of this piconet that decodes, and one whose header checks out
 * against another UAP, so this piconet's HEC check fails. */
static btbb_packet *decodable_packet(void)
{
	return followed_packet(FOLLOWED_HEADER,
			       hec_for(FOLLOWED_HEADER, TEST_UAP), 122);
}

static btbb_packet *failed_header_packet(void)
{
	return followed_packet(FOLLOWED_HEADER,
			       hec_for(FOLLOWED_HEADER, OTHER_UAP), 122);
}

/*
 * A packet of this piconet whose header checks out and whose payload does
 * not: a DM1 whose payload symbols no rate 2/3 block can be made of, which is
 * what a packet with bit errors in it comes to.
 */
static btbb_packet *failed_payload_packet(void)
{
	return followed_packet(FOLLOWED_DM1_HEADER,
			       hec_for(FOLLOWED_DM1_HEADER, TEST_UAP), 400);
}

/*
 * A piconet following a clock it recovered itself, which is the state
 * btbb_process_packet() leaves behind once hop reversal has accepted one.
 */
static btbb_piconet *following_a_recovered_clock(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	uint32_t offset;

	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	for (offset = 100; offset < 100 * 40; offset += 100) {
		observe(pn, (int) offset, channel_at(pn, offset));
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;
	}
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(1, pn->clock_recovered);
	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);
	return pn;
}

/*
 * A packet whitened at one clock and handed over at another, as a board hears
 * a piconet: the two run a fixed number of slots apart until the follow
 * starts.  A header only reads at the clock it was whitened for, so a fixture
 * whose clocks agreed would not notice the board's being used instead.
 */
static btbb_packet *packet_read_slots_late(uint32_t lap, uint8_t channel,
					   uint16_t header_data, uint8_t hec,
					   int whiten_clock, int slots)
{
	char stream[400];
	btbb_packet *pkt = NULL;

	memset(stream, 0, sizeof(stream));
	lay_out_header(stream, lap, header_data, hec, whiten_clock);
	/* finding the access code is what gives the packet its LAP */
	if (btbb_find_ac(stream, 1, lap, 0, &pkt) != 0)
		return NULL;
	if (pkt == NULL)
		return NULL;
	/* the clock goes in as the 312.5 us count, which is two ticks a slot */
	btbb_packet_set_data(pkt, stream, 122, channel,
			     (uint32_t) ((whiten_clock + slots) << 1));
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);
	return pkt;
}

/*
 * The other way a search settles: packet after packet rules candidate clocks
 * out until one is left, with no CRC to settle it - an idle link carries NULL
 * and POLL packets, which have no payload and so no CRC at all.  The clock is
 * known from that moment, so the header the surviving candidate reads is the
 * packet's own and the packet carries it, as it does on a CRC.
 */
static void test_a_clock_settled_by_elimination_records_the_header(void)
{
	/* Clocks far enough apart that each one picks a different whitening
	 * sequence, as packets of a real piconet do. */
	static const uint32_t clocks[8] = {
		3, 11, 20, 29, 38, 47, 55, 62
	};
	/* how far the board's clock runs ahead of the piconet's */
	const int slots_late = 7;
	const uint8_t uap = 0x5b;
	btbb_piconet *pn = btbb_piconet_new();
	int settled = 0, i;

	CHECK(pn != NULL);
	if (pn == NULL)
		return;
	btbb_init_piconet(pn, TEST_LAP);

	for (i = 0; i < 8 && !settled; i++) {
		/* NULL and POLL packets of several members of the piconet.
		 * Two candidate clocks can agree on a UAP for as long as
		 * every packet is identical, so the header varies: a wrong
		 * clock reads a different type out of the same symbols, and a
		 * type the decoder goes looking for a payload for is what
		 * rules that clock out. */
		uint16_t header = (uint16_t) ((i % 7 + 1) |
					      (((i & 1) ? PACKET_TYPE_POLL :
						PACKET_TYPE_NULL) << 3));
		btbb_packet *pkt = packet_read_slots_late(
			TEST_LAP, (uint8_t) (10 + i), header,
			hec_for(header, uap), (int) clocks[i], slots_late);

		CHECK(pkt != NULL);
		if (pkt == NULL)
			break;
		settled = btbb_uap_from_header(pkt, pn);
		if (settled) {
			CHECK_EQ(uap, btbb_piconet_get_uap(pn));
			/* the header of this packet, read at the clock that
			 * was left, checking out against that address part */
			CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED));
			CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID));
			CHECK_EQ(header,
				 air_to_host16(pkt->packet_header, 10));
			CHECK_EQ(header & 0x7, pkt->packet_lt_addr);
			CHECK_EQ((header >> 3) & 0xf, pkt->packet_type);
			CHECK_EQ(0, pkt->corrected_header_bits);
			/* and no payload: these types carry none, and no
			 * trial's CRC came out to make one the packet's */
			CHECK(!btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD));
		}
		btbb_packet_unref(pkt);
	}

	/* the candidates do run out: a search that never settled would leave
	 * every check above unrun */
	CHECK(settled);

	btbb_piconet_unref(pn);
}

/*
 * Header after header of the piconet failing its HEC means the clock or the
 * UAP being followed is not the piconet's. Nothing winnows while following, so
 * the library used to carry on reporting failures for as long as the capture
 * ran. Now both are given up, discovery starts over and the caller is told, so
 * that it can take its receiver off the offset the library gave it.
 */
static void test_a_run_of_header_failures_gives_up_the_clock(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	btbb_packet *pkt = failed_header_packet();
	int reported = 0;
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	for (i = 0; i < FOLLOW_HEADER_FAILURES; i++) {
		/* nothing is reported until the whole run has gone by */
		CHECK_EQ(0, reported);
		CHECK(btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
		reported = btbb_process_packet(pkt, pn);
	}
	CHECK_EQ(BTBB_PROCESS_LOCK_LOST, reported);

	/* Back to discovery: no clock, no UAP, nothing followed, no hops
	 * remembered. */
	CHECK(!btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK6_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_UAP_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK_EQ(0, btbb_piconet_get_uap(pn));
	CHECK_EQ(0, pn->packets_observed);
	CHECK_EQ(0, pn->follow_header_failures);
	CHECK_EQ(0, pn->clock_recovered);
	/* and the AFH map learned on the way is still there to search in */
	CHECK_EQ(1, btbb_piconet_get_channel_seen(pn, 0));

	/*
	 * Discovery really does start again, rather than filtering on the UAP
	 * it just gave up: try_hop() pins the UAP it was called with for as
	 * long as the piconet has one, and every clock candidate whose header
	 * disagrees with it is then ruled out, so the piconet would never find
	 * the UAP the packets really carry.
	 */
	for (i = 0; i < 2 * FOLLOW_HEADER_FAILURES; i++)
		CHECK_EQ(0, btbb_process_packet(pkt, pn));
	/* the candidates are the UAP these headers do check out against */
	CHECK_EQ(OTHER_UAP, pn->clock6_candidates[0]);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * And nothing else of the old lock is left to get in the way of recovering:
 * every field clock recovery works from reads as it does on a piconet that
 * has only just been discovered. The AFH map is the exception, kept on
 * purpose so the new search starts from the channels already seen.
 */
static void test_a_lost_lock_leaves_the_piconet_ready_to_search(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	btbb_piconet *fresh = discovered(NULL, 0);
	btbb_packet *pkt = failed_header_packet();
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		btbb_piconet_unref(fresh);
		return;
	}
	for (i = 0; i < FOLLOW_HEADER_FAILURES; i++)
		(void) btbb_process_packet(pkt, pn);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_FOLLOWING));

	/* nothing is left of the clock that failed */
	CHECK(pn->clock_candidates == NULL);
	CHECK_EQ(0, pn->num_candidates);
	CHECK_EQ(0, pn->winnowed);
	CHECK_EQ(0, pn->clk27_confirmations);
	CHECK_EQ(0, pn->clk6_offset);
	CHECK_EQ(0, btbb_piconet_get_clk_offset(pn));
	CHECK_EQ(0, pn->packets_observed);
	/* and the address the search starts from is the same as a fresh
	 * piconet's: the LAP it was given, no UAP */
	CHECK_EQ(fresh->LAP, pn->LAP);
	CHECK_EQ(0, btbb_piconet_get_uap(pn));
	CHECK(btbb_piconet_get_flag(pn, BTBB_LAP_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_UAP_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK6_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_GOT_FIRST_PACKET));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
	btbb_piconet_unref(fresh);
}

/*
 * A header that checks out says the clock and UAP are the piconet's, so the
 * run starts again from there: scattered failures on a noisy link never add
 * up to a lock given up.
 */
static void test_a_header_that_checks_out_ends_the_run(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	btbb_packet *bad = failed_header_packet();
	btbb_packet *good = decodable_packet();
	int i, round;

	CHECK(bad != NULL);
	CHECK(good != NULL);
	if (bad == NULL || good == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	for (round = 0; round < 3; round++) {
		for (i = 0; i < FOLLOW_HEADER_FAILURES - 1; i++)
			CHECK_EQ(0, btbb_process_packet(bad, pn));
		CHECK_EQ(FOLLOW_HEADER_FAILURES - 1,
			 pn->follow_header_failures);
		CHECK_EQ(0, btbb_process_packet(good, pn));
		CHECK_EQ(0, pn->follow_header_failures);
	}
	CHECK(btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	btbb_packet_unref(bad);
	btbb_packet_unref(good);
	btbb_piconet_unref(pn);
}

/*
 * A payload that will not decode is a packet lost to bit errors, not evidence
 * about the clock: its header checked out under that clock and the piconet's
 * UAP. Counting those would throw away a good clock on any link noisy enough
 * to break payloads, which is most of them.
 */
static void test_a_failed_payload_is_not_evidence(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	btbb_packet *pkt = failed_payload_packet();
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	/* The packet really is one the library fails to decode, and its header
	 * really did check out. Decoded here as btbb_process_packet() decodes
	 * a packet of a followed piconet, which is what sets the clock flags
	 * the header decode needs. */
	btbb_packet_set_uap(pkt, TEST_UAP);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_flag(pkt, BTBB_CLK27_VALID, 1);
	CHECK_EQ(0, btbb_decode(pkt));
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED));
	CHECK(btbb_packet_get_flag(pkt, BTBB_HEC_VALID));

	for (i = 0; i < 4 * FOLLOW_HEADER_FAILURES; i++)
		CHECK_EQ(0, btbb_process_packet(pkt, pn));
	CHECK_EQ(0, pn->follow_header_failures);
	CHECK(btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * A clock the caller set is the caller's, not the library's: ubertooth-follow
 * reads one from an adapter and follows it without any discovery. There is no
 * search to fall back on, so however many headers fail, the library keeps
 * following what it was told, and nothing counts up in the meantime.
 */
static void test_a_clock_the_caller_set_is_kept(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	btbb_packet *pkt = failed_header_packet();
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	btbb_piconet_set_clk_offset(pn, 1234);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);

	for (i = 0; i < 4 * FOLLOW_HEADER_FAILURES; i++)
		CHECK_EQ(0, btbb_process_packet(pkt, pn));
	CHECK(btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK_EQ(1234, btbb_piconet_get_clk_offset(pn));
	/* nothing counts up for a clock that is not the library's to give
	 * up, so nothing can run away either */
	CHECK_EQ(0, pn->follow_header_failures);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * A piconet following a clock the library accepted, set up directly instead
 * of by hop reversal. The follow path reads the clock offset, the UAP and
 * whether the clock is the library's own, and nothing else; hop reversal is a
 * walk over 2^21 clock values, so the tests below that only need a lock to
 * lose set one up this way.
 */
static void follow_a_clock(btbb_piconet *pn, int clk_offset)
{
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_clk_offset(pn, clk_offset);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);
	pn->clock_recovered = 1;
	pn->lock_worked = 0;
}

static btbb_piconet *following_clock_offset(int clk_offset)
{
	btbb_piconet *pn = discovered(NULL, 0);

	follow_a_clock(pn, clk_offset);
	return pn;
}

/*
 * Feed one failing header after another until the clock is given up, and
 * return how many it took. Each one arrives slots_apart 625 us slots after
 * the last, which is what decides whether the time bound or the count of
 * failures gets there first.
 */
static int headers_until_the_clock_is_given_up(btbb_piconet *pn,
					       btbb_packet *pkt,
					       uint32_t slots_apart)
{
	int sent;

	for (sent = 1; sent <= 2 * FOLLOW_HEADER_FAILURES; sent++) {
		/* the clock the packet arrived at, which is where the library
		 * reads the time from */
		pkt->clkn = (uint32_t) (sent - 1) * slots_apart;
		if (btbb_process_packet(pkt, pn) == BTBB_PROCESS_LOCK_LOST)
			return sent;
	}
	return 0;
}

/*
 * A clock that was given up is not chosen again. Discovery starting over used
 * to have no memory of it, so the same wrong clock could be accepted again,
 * fail to decode again, and go round that loop for as long as the capture
 * ran. What is remembered is the offset between the two clocks, because a
 * candidate CLK1-27 value means nothing after a restart: it is the piconet's
 * clock at the first packet of an attempt, and the next attempt has a
 * different first packet.
 */
static void test_a_clock_that_never_worked_is_not_chosen_again(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	btbb_packet *pkt = failed_header_packet();
	int with_it, without_it;
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	/* the clock it recovered is the observer's own, so the offset is 0 */
	CHECK_EQ(0, btbb_piconet_get_clk_offset(pn));
	for (i = 0; i < FOLLOW_HEADER_FAILURES - 1; i++)
		CHECK_EQ(0, btbb_process_packet(pkt, pn));
	CHECK_EQ(BTBB_PROCESS_LOCK_LOST, btbb_process_packet(pkt, pn));
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(0, pn->rejected_offsets[0]);

	/* Discovery again, from the state it started in, with the same first
	 * packet: the clock just given up is not a candidate this time. */
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	pn->first_pkt_time = 0;
	first_hop(pn, channel_at(pn, 0));
	with_it = btbb_init_hop_reversal(0, pn);
	CHECK(with_it > 1);
	CHECK(!candidate_listed(pn, 0));

	/* and nothing else went with it: the same search with nothing
	 * rejected finds exactly that one clock more */
	pn->num_rejected = 0;
	without_it = btbb_init_hop_reversal(0, pn);
	CHECK_EQ(with_it + 1, without_it);
	CHECK(candidate_listed(pn, 0));
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * The clock is remembered as the offset between the two clocks, which is what
 * makes it the same clock to the next attempt: that attempt has a different
 * first packet, so the candidate CLK1-27 value standing for the clock given
 * up is a different number. Here the new first packet is late enough that the
 * piconet's clock wraps between it and that clock.
 */
static void test_a_rejected_clock_survives_a_new_first_packet(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	btbb_packet *pkt = failed_header_packet();
	const uint32_t offset = 10;	/* slots between the two clocks */
	const uint32_t first = SEQUENCE_LENGTH - 4;
	const uint32_t same_clock = (first + offset) & (SEQUENCE_LENGTH - 1);
	int with_it, without_it;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	follow_a_clock(pn, (int) offset * 2);	/* in CLKN ticks */
	CHECK(headers_until_the_clock_is_given_up(pn, pkt, 1) > 0);
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(offset, pn->rejected_offsets[0]);

	/* discovery again, from a first packet at another time */
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	pn->first_pkt_time = first;
	pn->clk6_offset = (int) (offset & 0x3f);
	first_hop(pn, channel_at(pn, same_clock));
	with_it = btbb_init_hop_reversal(0, pn);
	CHECK(with_it > 1);
	CHECK(same_clock < offset);	/* the clock really did wrap */
	CHECK(!candidate_listed(pn, same_clock));

	/* and it is that clock that is missing, not something else */
	pn->num_rejected = 0;
	without_it = btbb_init_hop_reversal(0, pn);
	CHECK_EQ(with_it + 1, without_it);
	CHECK(candidate_listed(pn, same_clock));
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * A rejection is only about the hops one UAP predicts. A failed header says
 * the clock or the UAP was wrong without saying which, and both go into the
 * address hop selection runs on, so discovery that goes on to find a
 * different UAP is searching a different sequence and the clock it gave up
 * belongs in it again. Without that, a wrong UAP would cost the piconet the
 * clock that was right all along.
 */
static void test_a_rejected_clock_binds_only_under_its_own_uap(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	btbb_packet *pkt = failed_header_packet();
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	for (i = 0; i < FOLLOW_HEADER_FAILURES; i++)
		(void) btbb_process_packet(pkt, pn);
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(TEST_UAP, pn->rejected_uaps[0]);

	/* the same UAP: the clock it gave up stays out of the search */
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	pn->first_pkt_time = 0;
	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(!candidate_listed(pn, 0));

	/* a UAP the next round of discovery found instead: another sequence,
	 * and the same clock is a candidate in it */
	btbb_piconet_set_uap(pn, OTHER_UAP);
	address_precalc((int) (((((uint32_t) OTHER_UAP) << 24) | TEST_LAP) &
			       0xfffffff), pn);
	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(candidate_listed(pn, 0));
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * A clock that did decode a header is given up when it stops working, but it
 * is not ruled out: it was the piconet's at least once, so it may be the
 * piconet's still, on a link that has gone bad or quiet. Ruling it out on
 * that evidence would keep the library from ever locking it again.
 */
static void test_a_clock_that_worked_is_not_ruled_out(void)
{
	btbb_piconet *pn = following_clock_offset(0);
	btbb_packet *good = decodable_packet();
	btbb_packet *bad = failed_header_packet();

	CHECK(good != NULL);
	CHECK(bad != NULL);
	if (good == NULL || bad == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	CHECK_EQ(0, btbb_process_packet(good, pn));
	CHECK_EQ(1, pn->lock_worked);
	CHECK_EQ(FOLLOW_HEADER_FAILURES,
		 headers_until_the_clock_is_given_up(pn, bad, 1));
	CHECK_EQ(0, pn->num_rejected);
	btbb_packet_unref(good);
	btbb_packet_unref(bad);
	btbb_piconet_unref(pn);
}

/*
 * A slow link gives a wrong clock up on half the failures. On a busy link the
 * full count arrives in a few tens of milliseconds; at four packets a second
 * twenty headers take 4.75 s, and all that time the caller's receiver hops on
 * an offset that has never decoded anything. Once the run has been failing for
 * FOLLOW_FAILURE_SLOTS, FOLLOW_FAILURES_IN_TIME of them are enough. It still
 * takes a packet to answer on, so this halves the headers rather than putting
 * an upper bound on the time.
 */
static void test_a_slow_link_gives_a_wrong_clock_up_on_fewer_failures(void)
{
	/* 400 slots is a quarter of a second between headers: quiet for a
	 * followed link, and slow enough that the time bound is reached
	 * before the full count of failures is. */
	const uint32_t quiet = 400;
	btbb_piconet *busy_pn = following_clock_offset(0);
	btbb_piconet *quiet_pn = following_clock_offset(0);
	btbb_piconet *backwards_pn = following_clock_offset(0);
	btbb_packet *pkt = failed_header_packet();
	char out[4096], err[4096];
	int busy_headers, quiet_headers, backwards_headers;
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL || capture_or_fail() != 0) {
		btbb_piconet_unref(busy_pn);
		btbb_piconet_unref(quiet_pn);
		btbb_piconet_unref(backwards_pn);
		return;
	}
	/* a packet every slot pair, which is a link carrying traffic */
	busy_headers = headers_until_the_clock_is_given_up(busy_pn, pkt, 2);
	quiet_headers = headers_until_the_clock_is_given_up(quiet_pn, pkt,
							    quiet);
	/* A clock that runs backwards, which a caller handing packets over
	 * out of order gives: no time has elapsed, so it takes the full count
	 * of failures however far apart the clocks read. */
	backwards_headers = 0;
	for (i = 1; i <= 2 * FOLLOW_HEADER_FAILURES; i++) {
		pkt->clkn = (uint32_t) -(i * (int) quiet);
		if (btbb_process_packet(pkt, backwards_pn) ==
		    BTBB_PROCESS_LOCK_LOST) {
			backwards_headers = i;
			break;
		}
	}
	capture_stop(out, sizeof(out), err, sizeof(err));

	/* On the busy link the count of failures gets there first, and the
	 * whole run took 38 slots, 23 ms. */
	CHECK_EQ(FOLLOW_HEADER_FAILURES, busy_headers);
	/* On the quiet link the time bound does, after the failures needed to
	 * reach it. */
	CHECK_EQ(FOLLOW_FAILURES_IN_TIME, quiet_headers);
	CHECK(quiet_headers < FOLLOW_HEADER_FAILURES);
	CHECK_EQ(FOLLOW_HEADER_FAILURES, backwards_headers);
	/* and the run that ended it had been failing for 3600 slots, 2250 ms,
	 * which the note reports */
	CHECK(strstr(err, "over 23 ms") != NULL);
	CHECK(strstr(err, "over 2250 ms") != NULL);
	if (failures != 0)
		printf("stderr was: %s\n", err);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(busy_pn);
	btbb_piconet_unref(quiet_pn);
	btbb_piconet_unref(backwards_pn);
}

/*
 * The clocks given up belong to the LAP they were given up for. Another LAP
 * is another piconet, whose clock has nothing to do with them, and a tool
 * that reuses one piconet object for a second address would otherwise start
 * the new search with clocks ruled out for no reason.
 */
static void test_rejected_clocks_are_forgotten_with_the_lap(void)
{
	btbb_piconet *pn = following_clock_offset(0x80);
	btbb_packet *pkt = failed_header_packet();

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	CHECK(headers_until_the_clock_is_given_up(pn, pkt, 1) > 0);
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(0x40, pn->rejected_offsets[0]);	/* in slots */

	/* the same piconet, still */
	btbb_init_piconet(pn, TEST_LAP);
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(1, pn->lock_losses);

	/* a different one */
	btbb_init_piconet(pn, TEST_LAP ^ 0xff);
	CHECK_EQ(0, pn->num_rejected);
	CHECK_EQ(0, pn->lock_losses);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * The list of rejected clocks is short and bounded: once it is full the
 * oldest goes, because the clock a restart would otherwise choose again is
 * the one most recently given up. A clock is rejected on the evidence of one
 * follow attempt, which says the clock or the UAP was wrong without saying
 * which, so a long memory of them would risk ruling out a clock that was
 * right under the UAP found next time.
 */
static void test_the_rejected_clocks_are_a_short_list(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	btbb_packet *pkt = failed_header_packet();
	const int losses = MAX_REJECTED_CLOCKS + 4;
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	/* a different clock each time round, each given up in turn */
	for (i = 1; i <= losses; i++) {
		follow_a_clock(pn, i * 2);
		CHECK(headers_until_the_clock_is_given_up(pn, pkt, 1) > 0);
	}
	CHECK_EQ(losses, pn->lock_losses);
	CHECK_EQ(MAX_REJECTED_CLOCKS, pn->num_rejected);
	/* the last MAX_REJECTED_CLOCKS of them, in the order they failed */
	for (i = 0; i < MAX_REJECTED_CLOCKS; i++)
		CHECK_EQ(losses - MAX_REJECTED_CLOCKS + 1 + i,
			 pn->rejected_offsets[i]);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * A piconet that has ended or left range never gives the library a clock that
 * works, and the search goes round again for every clock that is left. It
 * says so once and then keeps looking quietly, rather than reporting the same
 * loss for as long as the capture runs.
 */
static void test_giving_up_is_reported_once(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	btbb_packet *pkt = failed_header_packet();
	char out[4096], err[4096];
	const char *at;
	int given_up = 0, said_so = 0;
	int i;

	CHECK(pkt != NULL);
	if (pkt == NULL || capture_or_fail() != 0) {
		btbb_piconet_unref(pn);
		return;
	}
	for (i = 1; i <= LOCK_LOSS_TRIES + 2; i++) {
		follow_a_clock(pn, i * 2);
		(void) headers_until_the_clock_is_given_up(pn, pkt, 1);
	}
	capture_stop(out, sizeof(out), err, sizeof(err));

	for (at = err; (at = strstr(at, "giving up this clock")) != NULL; at++)
		given_up++;
	for (at = err; (at = strstr(at, "Still listening")) != NULL; at++)
		said_so++;
	/* one line per clock up to the point where it says it cannot find
	 * one, and nothing after that */
	CHECK_EQ(LOCK_LOSS_TRIES, given_up);
	CHECK_EQ(1, said_so);
	CHECK_EQ(LOCK_LOSS_TRIES + 2, pn->lock_losses);
	/* and nothing of it on stdout */
	CHECK_EQ(0, (int) strlen(out));
	if (failures != 0)
		printf("stderr was: %s\n", err);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * That note is about a piconet nothing works for. A capture that loses a few
 * clocks, then locks one that decodes, and loses another an hour later should
 * hear about that one: a clock that works starts the count again.
 */
static void test_a_working_lock_restores_the_reporting(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	btbb_packet *bad = failed_header_packet();
	btbb_packet *good = decodable_packet();
	char out[4096], err[4096];
	int i;

	CHECK(bad != NULL);
	CHECK(good != NULL);
	if (bad == NULL || good == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	for (i = 1; i <= LOCK_LOSS_TRIES; i++) {
		follow_a_clock(pn, i * 2);
		CHECK(headers_until_the_clock_is_given_up(pn, bad, 1) > 0);
	}
	CHECK_EQ(1, pn->lock_loss_reported);

	/* a clock that decodes a header of the piconet */
	follow_a_clock(pn, 100);
	CHECK_EQ(0, btbb_process_packet(good, pn));
	CHECK_EQ(1, pn->lock_worked);
	CHECK_EQ(0, pn->lock_losses);
	CHECK_EQ(0, pn->lock_loss_reported);

	/* so the loss that comes after it is reported */
	if (capture_or_fail() != 0) {
		btbb_packet_unref(bad);
		btbb_packet_unref(good);
		btbb_piconet_unref(pn);
		return;
	}
	CHECK(headers_until_the_clock_is_given_up(pn, bad, 1) > 0);
	capture_stop(out, sizeof(out), err, sizeof(err));
	CHECK(strstr(err, "giving up this clock") != NULL);
	if (failures != 0)
		printf("stderr was: %s\n", err);
	btbb_packet_unref(bad);
	btbb_packet_unref(good);
	btbb_piconet_unref(pn);
}

/*
 * A caller can hand a clock back. The library gives one up on headers that
 * fail their HEC, which is evidence about CLK1-6 and the UAP; a wrong CLK1-27
 * carries the CLK1-6 discovery confirmed, so it dewhitens and checks out
 * whatever it receives and gets only the channel wrong. Its symptom is
 * silence, which the library cannot judge: it is called per packet and keeps
 * no timer. A capture tool knows how long it has been following in wall-clock
 * time, so btbb_piconet_drop_clock() lets it say so, and the clock is then
 * ruled out and discovery starts again exactly as after a lock the library
 * gave up itself.
 */
static void test_the_caller_can_give_up_a_quiet_clock(void)
{
	btbb_piconet *pn = following_a_recovered_clock();
	uint8_t first_channel = channel_at(pn, 0);
	int with_it, without_it;

	/* the clock it recovered is the observer's own, so the offset is 0 */
	CHECK_EQ(0, btbb_piconet_get_clk_offset(pn));
	CHECK_EQ(1, btbb_piconet_drop_clock(pn));

	/* back to discovery, with no clock and no UAP left to filter on */
	CHECK(!btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_CLK6_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_UAP_VALID));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK_EQ(0, btbb_piconet_get_uap(pn));
	CHECK_EQ(0, btbb_piconet_get_clk_offset(pn));
	CHECK_EQ(0, pn->clock_recovered);
	CHECK(pn->clock_candidates == NULL);
	/* and the channels seen on the way are still there to search in */
	CHECK_EQ(1, btbb_piconet_get_channel_seen(pn, first_channel));

	/* the clock is remembered, and the loss counted, as one the library
	 * gave up on failing headers would have been */
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(0, pn->rejected_offsets[0]);
	CHECK_EQ(TEST_UAP, pn->rejected_uaps[0]);
	CHECK_EQ(1, pn->lock_losses);

	/* so the next attempt does not return to it */
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	pn->first_pkt_time = 0;
	first_hop(pn, channel_at(pn, 0));
	with_it = btbb_init_hop_reversal(0, pn);
	CHECK(with_it > 1);
	CHECK(!candidate_listed(pn, 0));

	/* and nothing else went with it: the same search with nothing
	 * rejected finds exactly that one clock more */
	pn->num_rejected = 0;
	without_it = btbb_init_hop_reversal(0, pn);
	CHECK_EQ(with_it + 1, without_it);
	CHECK(candidate_listed(pn, 0));
	btbb_piconet_unref(pn);
}

/*
 * Only a clock the library recovered is the library's to give up. A piconet
 * still searching has none, and a clock the caller set itself - ubertooth-
 * follow reads one from an adapter - belongs to the caller, with no search to
 * fall back on, so a drop leaves it alone and says so rather than throwing
 * away the only clock there is. A null piconet answers the same way, as
 * btbb_process_packet() takes one.
 */
static void test_dropping_a_clock_the_library_has_not_got_does_nothing(void)
{
	btbb_piconet *searching = discovered(NULL, 0);
	btbb_piconet *caller_set = discovered(NULL, 0);

	CHECK_EQ(0, btbb_piconet_drop_clock(NULL));

	CHECK_EQ(0, btbb_piconet_drop_clock(searching));
	CHECK_EQ(0, searching->num_rejected);
	CHECK_EQ(0, searching->lock_losses);
	CHECK(btbb_piconet_get_flag(searching, BTBB_LAP_VALID));

	btbb_piconet_set_clk_offset(caller_set, 1234);
	btbb_piconet_set_flag(caller_set, BTBB_CLK27_VALID, 1);
	btbb_piconet_set_flag(caller_set, BTBB_FOLLOWING, 1);
	CHECK_EQ(0, btbb_piconet_drop_clock(caller_set));
	CHECK(btbb_piconet_get_flag(caller_set, BTBB_FOLLOWING));
	CHECK(btbb_piconet_get_flag(caller_set, BTBB_CLK27_VALID));
	CHECK_EQ(1234, btbb_piconet_get_clk_offset(caller_set));
	CHECK_EQ(0, caller_set->num_rejected);
	CHECK_EQ(0, caller_set->lock_losses);

	btbb_piconet_unref(searching);
	btbb_piconet_unref(caller_set);
}

/*
 * A clock the caller hands back is ruled out even though it decoded headers,
 * where a clock the library gives up on failing headers is not. That is the
 * whole difference between the two kinds of evidence. A wrong CLK1-27 carries
 * the CLK1-6 discovery confirmed, so every one of the few packets it hears
 * dewhitens and checks out: having decoded something says nothing about a
 * follow that has gone quiet, and leaving the clock available would let
 * discovery lock it again, which is the loop this exists to break.
 */
static void test_a_dropped_clock_is_ruled_out_even_if_it_worked(void)
{
	btbb_piconet *pn = following_clock_offset(0);
	btbb_packet *good = decodable_packet();
	btbb_packet *bad = failed_header_packet();

	CHECK(good != NULL);
	CHECK(bad != NULL);
	if (good == NULL || bad == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	CHECK_EQ(0, btbb_process_packet(good, pn));
	CHECK_EQ(1, pn->lock_worked);

	CHECK_EQ(1, btbb_piconet_drop_clock(pn));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_FOLLOWING));
	CHECK_EQ(1, pn->num_rejected);
	CHECK_EQ(0, pn->rejected_offsets[0]);

	/* and the library's own rule is untouched: a clock that worked and
	 * then failed header after header is still kept available */
	follow_a_clock(pn, 100);
	CHECK_EQ(0, btbb_process_packet(good, pn));
	CHECK_EQ(1, pn->lock_worked);
	CHECK_EQ(FOLLOW_HEADER_FAILURES,
		 headers_until_the_clock_is_given_up(pn, bad, 1));
	CHECK_EQ(1, pn->num_rejected);
	btbb_packet_unref(good);
	btbb_packet_unref(bad);
	btbb_piconet_unref(pn);
}

/*
 * The caller warns its own user, so a drop prints no line of its own: the HEC
 * failures the library's own note describes have not happened. What it does
 * say, once, is that this piconet has now given up clock after clock and may
 * have ended or left range, because that is as true of clocks the caller
 * handed back as of clocks the library rejected.
 */
static void test_dropped_clocks_are_counted_and_reported_once(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	char out[4096], err[4096];
	const char *at;
	int said_so = 0;
	int i;

	if (capture_or_fail() != 0) {
		btbb_piconet_unref(pn);
		return;
	}
	/* a different clock each time round, each handed back in turn */
	for (i = 1; i <= LOCK_LOSS_TRIES + 2; i++) {
		follow_a_clock(pn, i * 2);
		CHECK_EQ(1, btbb_piconet_drop_clock(pn));
	}
	capture_stop(out, sizeof(out), err, sizeof(err));

	for (at = err; (at = strstr(at, "Still listening")) != NULL; at++)
		said_so++;
	CHECK_EQ(1, said_so);
	/* nothing about failing headers, none of which arrived */
	CHECK(strstr(err, "failed their HEC") == NULL);
	CHECK_EQ(LOCK_LOSS_TRIES + 2, pn->lock_losses);
	CHECK_EQ(LOCK_LOSS_TRIES + 2, pn->num_rejected);
	/* and nothing of it on stdout */
	CHECK_EQ(0, (int) strlen(out));
	if (failures != 0)
		printf("stderr was: %s\n", err);
	btbb_piconet_unref(pn);
}

/*
 * Once the library has said a piconet may have ended or left range it stays
 * quiet about every later loss, whoever decided it, until a clock decodes a
 * header. Drops the caller made count toward that, so a capture whose tool
 * has handed back three clocks hears nothing further from the library about a
 * fourth that fails its headers - the tool is reporting each loss itself, and
 * the library promised to stop. A clock that works restores both.
 */
static void test_the_quiet_after_the_note_covers_both_kinds(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	btbb_packet *bad = failed_header_packet();
	btbb_packet *good = decodable_packet();
	char out[4096], err[4096];
	int i;

	CHECK(bad != NULL);
	CHECK(good != NULL);
	if (bad == NULL || good == NULL) {
		btbb_piconet_unref(pn);
		return;
	}
	for (i = 1; i <= LOCK_LOSS_TRIES; i++) {
		follow_a_clock(pn, i * 2);
		CHECK_EQ(1, btbb_piconet_drop_clock(pn));
	}
	CHECK_EQ(1, pn->lock_loss_reported);

	/* a clock that fails its headers, with the library already quiet */
	if (capture_or_fail() != 0) {
		btbb_packet_unref(bad);
		btbb_packet_unref(good);
		btbb_piconet_unref(pn);
		return;
	}
	follow_a_clock(pn, 100);
	CHECK(headers_until_the_clock_is_given_up(pn, bad, 1) > 0);
	capture_stop(out, sizeof(out), err, sizeof(err));
	CHECK(strstr(err, "failed their HEC") == NULL);
	CHECK_EQ(0, (int) strlen(out));

	/* and a clock that decodes a header restores the reporting */
	follow_a_clock(pn, 200);
	CHECK_EQ(0, btbb_process_packet(good, pn));
	CHECK_EQ(0, pn->lock_loss_reported);
	if (capture_or_fail() != 0) {
		btbb_packet_unref(bad);
		btbb_packet_unref(good);
		btbb_piconet_unref(pn);
		return;
	}
	CHECK(headers_until_the_clock_is_given_up(pn, bad, 1) > 0);
	capture_stop(out, sizeof(out), err, sizeof(err));
	CHECK(strstr(err, "failed their HEC") != NULL);
	if (failures != 0)
		printf("stderr was: %s\n", err);
	btbb_packet_unref(bad);
	btbb_packet_unref(good);
	btbb_piconet_unref(pn);
}

/*
 * Everything UAP and clock discovery has to say about its progress goes to
 * stderr. A tool that dumps its capture to stdout would otherwise have these
 * lines in the dump, with nothing to tell them from capture data.
 */
static void test_discovery_notes_go_to_stderr(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	char out[4096], err[4096];
	uint32_t offset;

	first_hop(pn, channel_at(pn, 0));
	if (capture_or_fail() != 0) {
		btbb_piconet_unref(pn);
		return;
	}
	/* the candidate list, and the hops that narrow it to the clock */
	btbb_init_hop_reversal(0, pn);
	for (offset = 100; offset < 100 * 40; offset += 100) {
		observe(pn, (int) offset, channel_at(pn, offset));
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;
	}
	/* and the AFH map, which is a caller's own request to print */
	btbb_print_afh_map(pn);
	capture_stop(out, sizeof(out), err, sizeof(err));

	CHECK(btbb_piconet_get_flag(pn, BTBB_CLK27_VALID));
	CHECK(strstr(err, "initial CLK1-27 candidates") != NULL);
	CHECK(strstr(err, "Acquired CLK1-27") != NULL);
	/* nothing of the discovery on stdout, and the map still on it */
	CHECK(strstr(out, "CLK1-27") == NULL);
	CHECK(strncmp(out, "AFH map: 0x", 11) == 0);
	CHECK(strstr(err, "AFH map: 0x") == NULL);
	if (failures != 0)
		printf("stdout was: %s\nstderr was: %s\n", out, err);
	btbb_piconet_unref(pn);
}

/* The same, for the notes the paths that give up write. */
static void test_discovery_complaints_go_to_stderr(void)
{
	char symbols[400];
	btbb_packet *pkt = btbb_packet_new();
	btbb_piconet *pn = discovered(NULL, 0);
	char out[4096], err[4096];

	memset(symbols, 0, sizeof(symbols));
	first_hop(pn, channel_at(pn, 0));
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	/* the remembered pattern is full, which starts discovery over */
	pn->packets_observed = MAX_PATTERN_LENGTH;
	btbb_packet_set_data(pkt, symbols, (int) sizeof(symbols), 11,
			     (pn->first_pkt_time + 123) << 1);

	if (capture_or_fail() != 0) {
		btbb_packet_unref(pkt);
		btbb_piconet_unref(pn);
		return;
	}
	try_hop(pkt, pn);
	capture_stop(out, sizeof(out), err, sizeof(err));

	CHECK(strstr(err, "More hops than we can remember") != NULL);
	CHECK_EQ(0, strlen(out));
	if (failures != 0)
		printf("stdout was: %s\nstderr was: %s\n", out, err);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
}

/*
 * Reversing the hops an aliased receiver observes is not implemented, so
 * asking for it is refused rather than served with ordinary hop reversal.
 * The piconet's 'aliased' field, which decided this, was never assigned, so
 * a caller passing 1 used to get candidates chosen against unfolded
 * channels - channels such a receiver never reports - and no sign of it.
 */
static void test_aliased_hop_reversal_is_refused(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	char out[4096], err[4096];

	first_hop(pn, channel_at(pn, 0));
	if (capture_or_fail() != 0) {
		btbb_piconet_unref(pn);
		return;
	}
	CHECK_EQ(0, btbb_init_hop_reversal(1, pn));
	capture_stop(out, sizeof(out), err, sizeof(err));

	CHECK(strstr(err, "aliased hop reversal is not implemented") != NULL);
	CHECK_EQ(0, strlen(out));
	/* nothing was started, so nothing has to be given up either */
	CHECK_EQ(0, pn->num_candidates);
	CHECK(pn->clock_candidates == NULL);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	CHECK(!btbb_piconet_get_flag(pn, BTBB_IS_ALIASED));

	/* and ordinary hop reversal is unaffected */
	CHECK(btbb_init_hop_reversal(0, pn) > 1);
	CHECK(!btbb_piconet_get_flag(pn, BTBB_IS_ALIASED));
	if (failures != 0)
		printf("stdout was: %s\nstderr was: %s\n", out, err);
	btbb_piconet_unref(pn);
}

/*
 * A refused aliased request changes nothing, so a piconet already recovering
 * its clock the ordinary way carries on: the candidates it has chosen, and
 * the state it chose them under, are still its own.
 */
static void test_a_refused_aliased_request_leaves_recovery_alone(void)
{
	btbb_piconet *pn = discovered(NULL, 0);
	uint32_t *candidates;
	char out[4096], err[4096];
	int count;

	first_hop(pn, channel_at(pn, 0));
	count = btbb_init_hop_reversal(0, pn);
	CHECK(count > 1);
	candidates = pn->clock_candidates;

	if (capture_or_fail() != 0) {
		btbb_piconet_unref(pn);
		return;
	}
	CHECK_EQ(0, btbb_init_hop_reversal(1, pn));
	capture_stop(out, sizeof(out), err, sizeof(err));

	CHECK(strstr(err, "aliased hop reversal is not implemented") != NULL);
	CHECK_EQ(count, pn->num_candidates);
	CHECK(pn->clock_candidates == candidates);
	CHECK(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT));
	/* and the hops that follow still narrow that list to the clock */
	observe(pn, 100, channel_at(pn, 100));
	CHECK(pn->num_candidates > 0);
	CHECK(pn->num_candidates < count);
	btbb_piconet_unref(pn);
}

/* Three LAPs, as a survey of several piconets hears them. */
#define SURVEY_LAP_A 0x2a3b4c
#define SURVEY_LAP_B 0x5d6e7f
#define SURVEY_LAP_C 0x010203

/* One packet of a LAP, on a channel, through the survey. The piconet
 * argument is ignored in survey mode: the survey keeps its own. */
static void survey_hears(uint32_t lap, uint8_t channel)
{
	btbb_packet *pkt = packet_of(lap, channel, FOLLOWED_HEADER,
				     hec_for(FOLLOWED_HEADER, TEST_UAP), 122);

	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK_EQ(0, btbb_process_packet(pkt, NULL));
	btbb_packet_unref(pkt);
}

/* Empty the survey and leave survey mode, so the tests either side of these
 * find the library as it was. The survey's reference to each piconet comes
 * with the piconet, so the caller releases it. */
static void survey_end(void)
{
	btbb_piconet *pn;

	while ((pn = btbb_next_survey_result()) != NULL)
		btbb_piconet_unref(pn);
	survey_mode = 0;
}

/*
 * A survey's results used to be reachable only through
 * btbb_next_survey_result(), which takes each piconet out of the survey. So a
 * tool surveying piconets could not read the UAP the survey had recovered for
 * a LAP while the survey was still running: ubertooth-rx -z, whose whole job
 * is finding addresses, wrote capture records with no reference address in
 * them. btbb_survey_piconet() reads the live piconet and leaves it there.
 */
static void test_a_survey_piconet_is_read_without_being_removed(void)
{
	btbb_piconet *a, *b;

	btbb_init_survey();
	survey_hears(SURVEY_LAP_A, 10);
	survey_hears(SURVEY_LAP_B, 20);

	a = btbb_survey_piconet(SURVEY_LAP_A);
	b = btbb_survey_piconet(SURVEY_LAP_B);
	CHECK(a != NULL);
	CHECK(b != NULL);
	if ((a == NULL) || (b == NULL)) {
		survey_end();
		return;
	}
	CHECK(a != b);
	CHECK_EQ(SURVEY_LAP_A, btbb_piconet_get_lap(a));
	CHECK_EQ(SURVEY_LAP_B, btbb_piconet_get_lap(b));
	/* each piconet holds the channels its own LAP was heard on */
	CHECK_EQ(1, btbb_piconet_get_channel_seen(a, 10));
	CHECK_EQ(0, btbb_piconet_get_channel_seen(a, 20));

	/* reading it leaves it in the survey: the same piconet comes back */
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) == a);
	/* the reference stays the survey's, which is what lets a caller read
	 * a piconet and drop it without releasing the survey's copy */
	CHECK_EQ(1, a->refcount);
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) == a);
	CHECK_EQ(1, a->refcount);
	/* and it is still the piconet the survey updates */
	survey_hears(SURVEY_LAP_A, 30);
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) == a);
	CHECK_EQ(1, btbb_piconet_get_channel_seen(a, 30));

	/* the UAP the survey recovers for a LAP, which is what a caller
	 * wanting to record a reference address is after, is readable while
	 * the survey runs. It is set here through the survey's own lookup,
	 * the one btbb_process_packet() uses, and read through the accessor */
	btbb_piconet_set_uap(get_piconet(SURVEY_LAP_A), TEST_UAP);
	CHECK_EQ(TEST_UAP,
		 btbb_piconet_get_uap(btbb_survey_piconet(SURVEY_LAP_A)));

	/* nothing was taken out of the survey along the way */
	CHECK_EQ(2, HASH_COUNT(piconet_survey));
	survey_end();
}

/* A LAP the survey has not heard has no piconet, and neither does any LAP
 * before a survey starts or after its results have been read. */
static void test_a_lap_the_survey_has_not_heard_has_no_piconet(void)
{
	/* whatever ran before this, the survey starts out empty here */
	survey_end();
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) == NULL);

	btbb_init_survey();
	survey_hears(SURVEY_LAP_A, 10);
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) != NULL);
	CHECK(btbb_survey_piconet(SURVEY_LAP_B) == NULL);

	survey_end();
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) == NULL);
}

/*
 * A piconet btbb_next_survey_result() hands out is gone from the survey. Its
 * entry used to be freed while the hash table's buckets still pointed at it,
 * so looking that LAP up again - which is what a survey carrying on after
 * some of its results have been read does, for every further packet of that
 * piconet - walked freed memory and handed back the released piconet.
 */
static void test_a_piconet_taken_out_of_the_survey_is_gone_from_it(void)
{
	static const uint32_t laps[3] = {
		SURVEY_LAP_A, SURVEY_LAP_B, SURVEY_LAP_C
	};
	btbb_piconet *was[3];
	btbb_piconet *taken, *fresh;
	uint32_t taken_lap;
	int i;

	btbb_init_survey();
	for (i = 0; i < 3; i++) {
		survey_hears(laps[i], (uint8_t) (10 + i * 10));
		was[i] = btbb_survey_piconet(laps[i]);
		CHECK(was[i] != NULL);
	}
	CHECK_EQ(3, HASH_COUNT(piconet_survey));

	taken = btbb_next_survey_result();
	CHECK(taken != NULL);
	if (taken == NULL) {
		survey_end();
		return;
	}
	taken_lap = btbb_piconet_get_lap(taken);
	CHECK_EQ(2, HASH_COUNT(piconet_survey));
	for (i = 0; i < 3; i++) {
		if (was[i] == taken)
			CHECK(btbb_survey_piconet(laps[i]) == NULL);
		else
			CHECK(btbb_survey_piconet(laps[i]) == was[i]);
	}

	/* a further packet of that LAP starts a new piconet, rather than
	 * being written through the one that was released */
	survey_hears(taken_lap, 40);
	fresh = btbb_survey_piconet(taken_lap);
	CHECK(fresh != NULL);
	CHECK(fresh != taken);
	CHECK_EQ(3, HASH_COUNT(piconet_survey));

	btbb_piconet_unref(taken);
	survey_end();
}

/*
 * The case the accessor exists for: a UAP the survey worked out for itself,
 * read back while the survey is still running. The survey is given packets of
 * one LAP at clocks of its own choosing, each header whitened for the clock it
 * arrives at, which is how discovery narrows its 64 candidate values for the
 * clock of the first packet down to the one that agrees on a UAP.
 */
static void test_the_uap_the_survey_recovered_is_readable(void)
{
	/* Clocks far enough apart that each one picks a different whitening
	 * sequence, as packets of a real piconet do. */
	static const uint32_t clocks[8] = {
		3, 11, 20, 29, 38, 47, 55, 62
	};
	const uint8_t uap = 0x5b;
	btbb_piconet *pn;
	char out[4096], err[4096];
	int i;

	survey_end();
	btbb_init_survey();
	if (capture_or_fail() != 0)
		return;
	for (i = 0; i < 8; i++) {
		/* NULL and POLL packets of several devices of the piconet, as
		 * an idle link carries. Two candidate clocks can agree on a
		 * UAP for as long as every packet is identical, so the header
		 * varies: a wrong clock reads a different type out of the
		 * same symbols, and a type the decoder goes looking for a
		 * payload for is what rules that clock out. */
		uint16_t header = (uint16_t) ((i % 7 + 1) |
					      (((i & 1) ? PACKET_TYPE_POLL :
						PACKET_TYPE_NULL) << 3));
		btbb_packet *pkt = packet_of_at_clock(
			SURVEY_LAP_A, (uint8_t) (10 + i), header,
			hec_for(header, uap), 122, (int) clocks[i]);

		if (pkt == NULL) {
			CHECK(pkt != NULL);
			break;
		}
		CHECK_EQ(0, btbb_process_packet(pkt, NULL));
		btbb_packet_unref(pkt);
		pn = btbb_survey_piconet(SURVEY_LAP_A);
		if ((pn != NULL) && btbb_piconet_get_flag(pn, BTBB_UAP_VALID))
			break;
	}
	capture_stop(out, sizeof(out), err, sizeof(err));

	pn = btbb_survey_piconet(SURVEY_LAP_A);
	CHECK(pn != NULL);
	if (pn == NULL) {
		survey_end();
		return;
	}
	/* the survey found it, and said so on stderr */
	CHECK(btbb_piconet_get_flag(pn, BTBB_UAP_VALID));
	CHECK_EQ(uap, btbb_piconet_get_uap(pn));
	CHECK(strstr(err, "UAP = 0x5b found after") != NULL);
	CHECK_EQ(0, strlen(out));
	if (failures != 0)
		printf("stdout was: %s\nstderr was: %s\n", out, err);

	/* and it is still the survey's: reading it took nothing out */
	CHECK_EQ(1, HASH_COUNT(piconet_survey));
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) == pn);
	survey_end();
}

/*
 * The survey gives back everything it took. Reading the last piconet out of it
 * frees that LAP's entry and, with it, the hash table and the bucket array the
 * entry was the last one in. The walk this replaces freed the entry on its
 * own, so every survey read to the end left its table behind.
 */
static void test_the_last_result_takes_the_survey_table_with_it(void)
{
	btbb_piconet *pn;
	int frees;

	survey_end();
	btbb_init_survey();
	survey_hears(SURVEY_LAP_A, 10);
	CHECK_EQ(1, HASH_COUNT(piconet_survey));

	frees = free_calls;
	pn = btbb_next_survey_result();
	frees = free_calls - frees;
	CHECK(pn != NULL);
	CHECK(piconet_survey == NULL);
	/* the entry, the table, and the bucket array */
	CHECK(frees >= 3);
	if (pn != NULL)
		btbb_piconet_unref(pn);
	survey_mode = 0;
}

/*
 * A survey that cannot make room for a piconet drops the packet. Both
 * allocations it takes are failed in turn: the survey's own entry for the LAP,
 * and the piconet itself, which is the larger of the two and so the one a
 * process short of memory fails first. Either used to be written through.
 */
static void test_a_survey_that_cannot_allocate_drops_the_packet(void)
{
	/* built before allocations are refused, since finding the access
	 * code allocates the packet */
	btbb_packet *pkt = packet_of(SURVEY_LAP_A, 10, FOLLOWED_HEADER,
				     hec_for(FOLLOWED_HEADER, TEST_UAP), 122);
	int round;

	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_init_survey();
	for (round = 0; round < 2; round++) {
		char out[4096], err[4096];
		int frees;

		if (capture_or_fail() != 0)
			break;
		if (round == 0)
			refuse_allocations = 1;	/* the survey's entry */
		else
			refuse_callocs = 1;	/* the piconet */
		frees = free_calls;
		CHECK_EQ(0, btbb_process_packet(pkt, NULL));
		frees = free_calls - frees;
		refuse_allocations = 0;
		refuse_callocs = 0;
		capture_stop(out, sizeof(out), err, sizeof(err));

		/* where the entry was made and the piconet was not, the entry
		 * is given back: it is reachable from nothing afterwards, and
		 * a pointer left in a stack slot can keep a sanitizer from
		 * calling it a leak */
		CHECK_EQ(round, frees);
		CHECK(btbb_survey_piconet(SURVEY_LAP_A) == NULL);
		CHECK_EQ(0, HASH_COUNT(piconet_survey));
		/* either failure says so, and on stderr */
		CHECK_EQ(0, strlen(out));
		CHECK(strstr(err, (round == 0) ?
			     "could not allocate a survey entry" :
			     "could not allocate a piconet") != NULL);
	}
	btbb_packet_unref(pkt);

	/* and the next packet, with room again, is surveyed */
	survey_hears(SURVEY_LAP_A, 10);
	CHECK(btbb_survey_piconet(SURVEY_LAP_A) != NULL);
	survey_end();
}

int main(void)
{
	/* first, while the error correction table has not been built yet */
	test_a_failed_allocation_is_reported();
	test_afh_candidates_fit_their_list();
	test_first_hop_alone_does_not_look_like_afh();
	test_two_hops_on_one_channel_look_like_afh();
	test_only_a_repeat_inside_a_slot_pair_means_adapted_hopping();
	test_a_repeat_across_a_pair_boundary_is_not_adapted_hopping();
	test_an_adapted_sequence_is_recognized_before_its_hops_are_dropped();
	test_choosing_the_candidates_again_recompares_every_hop();
	test_candidates_follow_the_map_as_it_is_learned();
	test_candidates_rechosen_only_for_a_new_sequence();
	test_one_walk_finds_the_same_candidates();
	test_clk6_offset_fixes_the_known_clock_bits();
	test_remembered_pattern_is_bounded();
	test_set_afh_map_allocates_nothing();
	test_a_uap_with_its_top_bit_set();
	test_an_allocation_failure_leaves_no_candidates();
	test_an_allocation_failure_partway_through_the_list();
	test_the_clock_is_confirmed_before_it_is_accepted();
	test_a_wrong_clock_is_not_accepted_on_a_short_map();
	test_discovery_notes_go_to_stderr();
	test_discovery_complaints_go_to_stderr();
	test_aliased_hop_reversal_is_refused();
	test_the_clocks_of_the_device_and_the_observer_differ();
	test_every_candidate_agrees_on_clk1_6();
	test_the_settled_offset_reads_the_packets_own_header();
	test_the_settling_packet_carries_its_own_header();
	test_a_clock_settled_by_elimination_records_the_header();
	test_a_run_of_header_failures_gives_up_the_clock();
	test_a_lost_lock_leaves_the_piconet_ready_to_search();
	test_a_header_that_checks_out_ends_the_run();
	test_a_failed_payload_is_not_evidence();
	test_a_clock_the_caller_set_is_kept();
	test_a_clock_that_never_worked_is_not_chosen_again();
	test_a_rejected_clock_survives_a_new_first_packet();
	test_a_rejected_clock_binds_only_under_its_own_uap();
	test_a_clock_that_worked_is_not_ruled_out();
	test_a_slow_link_gives_a_wrong_clock_up_on_fewer_failures();
	test_rejected_clocks_are_forgotten_with_the_lap();
	test_the_rejected_clocks_are_a_short_list();
	test_giving_up_is_reported_once();
	test_a_working_lock_restores_the_reporting();
	test_the_caller_can_give_up_a_quiet_clock();
	test_dropping_a_clock_the_library_has_not_got_does_nothing();
	test_a_dropped_clock_is_ruled_out_even_if_it_worked();
	test_dropped_clocks_are_counted_and_reported_once();
	test_the_quiet_after_the_note_covers_both_kinds();
	test_a_refused_aliased_request_leaves_recovery_alone();
	test_a_survey_piconet_is_read_without_being_removed();
	test_a_lap_the_survey_has_not_heard_has_no_piconet();
	test_a_piconet_taken_out_of_the_survey_is_gone_from_it();
	test_the_uap_the_survey_recovered_is_readable();
	test_the_last_result_takes_the_survey_table_with_it();
	test_a_survey_that_cannot_allocate_drops_the_packet();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
