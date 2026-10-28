/*
 * Two measurements of clock recovery, kept as a test so the numbers quoted in
 * issues #44, #49 and #50 can be reproduced and do not quietly drift.
 *
 * The sweep: a device hops on a 40-channel AFH map; the observer has seen all
 * of it but one channel.  The observer catches one packet every four slots, up
 * to 40 of them, and tries to recover the device's CLK1-27.  Each true clock
 * value is one trial, ending in a true lock, a wrong lock, every candidate
 * ruled out so discovery starts over, or still undecided after 40 packets.
 * It runs twice: once with the observer's clock reading the same as the
 * device's, which is how every other test has it, and once with the two a
 * long way apart.  A clock read in the wrong unit anywhere in recovery would
 * move every prediction in the second run and not the first, so the two
 * agreeing is what pins the unit (#50).
 *
 * The false drops: how often a header fails its HEC while the clock and the
 * UAP are right, which is what btbb_process_packet() counts towards giving a
 * clock up.  Packets are run through the access code search at a range of bit
 * error rates, as a receiver's would be, and only those it finds are decoded.
 * FOLLOW_HEADER_FAILURES of those failures in a row gives up the clock, so
 * the conditional rate to the twentieth power is the chance of dropping a
 * clock that was right (#44).
 *
 * Both are slow enough to be run small by default: "test_clock_sweep 400" is
 * the full sweep the issues quote.
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

#define TEST_LAP 0x345678
#define TEST_UAP 0x02

/* channels 0-39, and the same without channel 7 */
static const uint8_t MAP_DEVICE[10] = {0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t MAP_OBSERVER[10] = {0x7f, 0xff, 0xff, 0xff, 0xff};

#define PACKETS 40
#define SLOTS_APART 4

/* How far the observer's clock reads from the device's, in 625 us slots, for
 * the second run of the sweep. */
#define CLOCKS_APART 0x0123456

/* The clock values the sweep walks, how far apart they are, and how many of
 * them by default.  The step spreads a short default run over the same range
 * the full one covers, so a couple of dozen trials still hold locks of both
 * kinds; the full run the issues quote is every clock value, "400". */
#define FIRST_CLOCK 0x1000000
#define DEFAULT_TRIALS 24
#define DEFAULT_STEP 17

static btbb_piconet *discovered(const uint8_t *map)
{
	btbb_piconet *pn = btbb_piconet_new();

	btbb_init_piconet(pn, TEST_LAP);
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	memcpy(pn->afh_map, map, sizeof(pn->afh_map));
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, 1);
	precalc(pn);
	address_precalc(((TEST_UAP << 24) | TEST_LAP) & 0xfffffff, pn);
	return pn;
}

static uint8_t channel_at(btbb_piconet *pn, uint32_t clk1_27)
{
	return (uint8_t) single_hop((int) (clk1_27 << 1), pn);
}

enum outcome { TRUE_LOCK, WRONG_LOCK, DROPPED, UNDECIDED, OUTCOMES };
static const char *NAMES[OUTCOMES] = {"true lock", "wrong lock",
				      "dropped", "undecided"};

/*
 * Once a clock is accepted the caller puts its receiver on that offset and the
 * board hops from it. How many of the device's next slot pairs does it then
 * land on the device's channel? With the piconet's own clock, all of them.
 */
#define FOLLOWED_SLOT_PAIRS 1000

static int slots_on_the_right_channel(btbb_piconet *device, uint32_t locked,
				      uint32_t true_clock)
{
	int agree = 0;
	int i;

	for (i = 0; i < FOLLOWED_SLOT_PAIRS; i++) {
		uint32_t step = (uint32_t) (i * 2);

		if (channel_at(device, locked + step) ==
		    channel_at(device, true_clock + step))
			agree++;
	}
	return agree;
}

struct totals {
	int counts[OUTCOMES];
	int wrong_locks_with_another_clk1_6;
	long wrong_lock_slots;
	long true_lock_slots;
};

static enum outcome trial(btbb_piconet *device, uint32_t true_clock,
			  uint32_t apart, struct totals *t)
{
	btbb_piconet *pn = discovered(MAP_OBSERVER);
	uint32_t first_pkt_time = true_clock - apart;
	uint32_t offset;
	enum outcome result = UNDECIDED;

	pn->first_pkt_time = first_pkt_time;
	pn->clk6_offset = (int) (apart & 0x3f);
	pn->pattern_indices[0] = 0;
	pn->pattern_channels[0] = channel_at(device, true_clock);
	btbb_piconet_set_channel_seen(pn, pn->pattern_channels[0]);
	pn->packets_observed = 1;

	if (btbb_init_hop_reversal(0, pn) == 0) {
		btbb_piconet_unref(pn);
		return DROPPED;
	}

	for (offset = SLOTS_APART; offset <= SLOTS_APART * PACKETS;
	     offset += SLOTS_APART) {
		uint8_t channel = channel_at(device, true_clock + offset);

		btbb_piconet_set_channel_seen(pn, channel);
		pn->pattern_indices[pn->packets_observed] = (int) offset;
		pn->pattern_channels[pn->packets_observed] = channel;
		pn->packets_observed++;
		btbb_winnow(pn);

		if (!btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
			result = DROPPED;
			break;
		}
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
			uint32_t locked = pn->clock_candidates[0];
			int agree = slots_on_the_right_channel(device, locked,
							       true_clock);

			if (locked == true_clock) {
				result = TRUE_LOCK;
				t->true_lock_slots += agree;
			} else {
				int true_offset =
					(int) ((true_clock << 1) -
					       (first_pkt_time << 1));

				result = WRONG_LOCK;
				t->wrong_lock_slots += agree;
				/* Every candidate carries the CLK1-6
				 * discovery found, so a wrong one still
				 * dewhitens and checks every header it
				 * receives: what it gets wrong is the
				 * channel. */
				if ((pn->clk_offset & 0x7f) !=
				    (true_offset & 0x7f))
					t->wrong_locks_with_another_clk1_6++;
			}
			break;
		}
	}

	btbb_piconet_unref(pn);
	return result;
}

static void sweep(const char *label, uint32_t apart, int trials, int step)
{
	btbb_piconet *device = discovered(MAP_DEVICE);
	struct totals t;
	int i;

	memset(&t, 0, sizeof(t));
	for (i = 0; i < trials; i++)
		t.counts[trial(device,
			       FIRST_CLOCK + (uint32_t) (i * step), apart,
			       &t)]++;

	fprintf(stderr, "%-8s %d clocks from 0x%07x step %d, observer clock %u slots away\n",
		label, trials, FIRST_CLOCK, step, apart);
	for (i = 0; i < OUTCOMES; i++)
		fprintf(stderr, "  %-11s %4d\n", NAMES[i], t.counts[i]);
	if (t.counts[WRONG_LOCK] > 0)
		fprintf(stderr, "  wrong lock: %.2f%% of the next %d slot pairs on the device's channel\n",
			100.0 * (double) t.wrong_lock_slots /
			(double) (t.counts[WRONG_LOCK] * FOLLOWED_SLOT_PAIRS),
			FOLLOWED_SLOT_PAIRS);
	if (t.counts[TRUE_LOCK] > 0)
		fprintf(stderr, "  true lock:  %.2f%%\n",
			100.0 * (double) t.true_lock_slots /
			(double) (t.counts[TRUE_LOCK] * FOLLOWED_SLOT_PAIRS));

	/* The scenario is only a test of anything if clocks are recovered
	 * from it at all, and wrong ones accepted. */
	CHECK(t.counts[TRUE_LOCK] > 0);
	CHECK(t.counts[WRONG_LOCK] > 0);
	/* Not one wrong lock changes the CLK1-6 a receiver dewhitens with, so
	 * a run of failed headers cannot be what finds them. */
	CHECK(t.wrong_locks_with_another_clk1_6 == 0);
	btbb_piconet_unref(device);
}

/* ------------------------- false drops on a good clock ------------------- */

/* The access code search allows this many bit errors in the 64-bit code,
 * which is what ubertooth-rx passes by default. */
#define MAX_AC_ERRORS 2
/* An lt_addr of 1 and the packet type NULL, which has no payload. */
#define NULL_HEADER (1 | (PACKET_TYPE_NULL << 3))
#define BER_TRIALS 20000

static uint32_t rng_state = 12345;

/* A little arithmetic generator, so the run is the same everywhere. */
static double urand(void)
{
	rng_state = rng_state * 1103515245u + 12345u;
	return (double) ((rng_state >> 8) & 0xffffff) / 16777216.0;
}

static uint8_t hec_for(uint16_t data, uint8_t uap)
{
	int hec;

	for (hec = 0; hec < 256; hec++) {
		if (uap_from_hec(data, (uint8_t) hec) == uap)
			return (uint8_t) hec;
	}
	return 0;
}

/* One packet of this piconet: the access code, its trailer, and the 18 header
 * bits each sent three times. */
static void build_packet(char *stream, size_t size, uint8_t hec)
{
	uint64_t syncword = btbb_gen_syncword(TEST_LAP);
	uint32_t header = (uint32_t) NULL_HEADER | ((uint32_t) hec << 10);
	int i;

	memset(stream, 0, size);
	for (i = 0; i < 64; i++)
		stream[i] = (char) ((syncword >> i) & 1);
	for (i = 0; i < 4; i++)
		stream[64 + i] = (char) (((syncword >> 63) ^ (i + 1)) & 1);
	for (i = 0; i < 18; i++) {
		char bit = (char) ((header >> i) & 1);

		stream[68 + i * 3] = bit;
		stream[69 + i * 3] = bit;
		stream[70 + i * 3] = bit;
	}
}

static double false_drop_rate(double ber, int trials)
{
	char clean[200];
	long found = 0, checked = 0, failed = 0, longest = 0, run = 0;
	int t;

	build_packet(clean, sizeof(clean), hec_for(NULL_HEADER, TEST_UAP));
	for (t = 0; t < trials; t++) {
		char stream[200];
		btbb_packet *pkt = NULL;
		int i;

		memcpy(stream, clean, sizeof(stream));
		for (i = 0; i < 122; i++) {
			if (urand() < ber)
				stream[i] ^= 1;
		}
		if (btbb_find_ac(stream, 1, TEST_LAP, MAX_AC_ERRORS,
				 &pkt) != 0) {
			if (pkt != NULL)
				btbb_packet_unref(pkt);
			continue;
		}
		found++;
		/* decoded the way btbb_process_packet() decodes a packet of a
		 * followed piconet, with the right clock and UAP */
		btbb_packet_set_data(pkt, stream, 122, 0, 0);
		btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
		btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
		btbb_packet_set_uap(pkt, TEST_UAP);
		(void) btbb_decode(pkt);
		if (btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED)) {
			checked++;
			if (btbb_packet_get_flag(pkt, BTBB_HEC_VALID)) {
				run = 0;
			} else {
				failed++;
				run++;
				if (run > longest)
					longest = run;
			}
		}
		btbb_packet_unref(pkt);
	}
	{
		double cond = checked ? (double) failed / (double) checked
				      : 0.0;
		double chance = 1.0;
		int k;

		for (k = 0; k < FOLLOW_HEADER_FAILURES; k++)
			chance *= cond;
		fprintf(stderr, "  ber %.3f: %ld found, %ld checked, %ld failed, "
			"%.4f of them, longest run %ld, %d in a row %.3g\n",
			ber, found, checked, failed, cond, longest,
			FOLLOW_HEADER_FAILURES, chance);
		/* Nowhere near the run that gives a clock up. */
		CHECK(longest < FOLLOW_HEADER_FAILURES);
		return cond;
	}
}

int main(int argc, char **argv)
{
	int trials = argc > 1 ? atoi(argv[1]) : DEFAULT_TRIALS;
	int step = argc > 1 ? 1 : DEFAULT_STEP;

	if (trials < 1) {
		trials = DEFAULT_TRIALS;
		step = DEFAULT_STEP;
	}
	sweep("aligned", 0, trials, step);
	sweep("offset", CLOCKS_APART, trials, step);

	fprintf(stderr, "headers that fail their HEC under the right clock and UAP:\n");
	(void) false_drop_rate(0.005, BER_TRIALS);
	(void) false_drop_rate(0.02, BER_TRIALS);
	(void) false_drop_rate(0.10, BER_TRIALS);

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
