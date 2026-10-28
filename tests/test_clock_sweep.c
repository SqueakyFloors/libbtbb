/*
 * Three measurements of clock recovery, kept as a test so the numbers quoted
 * in issues #44, #49 and #50 can be reproduced and do not quietly drift.
 *
 * The sweep: a device hops on a 40-channel AFH map; the observer has seen all
 * of it but one channel.  The observer catches one packet every four slots, up
 * to 40 of them, and tries to recover the device's CLK1-27.  Each true clock
 * value is one trial, ending in a true lock, a wrong lock, every candidate
 * ruled out so discovery starts over, or still undecided after 40 packets.
 * Each lock is then followed with the packets the observer's receiver would
 * really hear on that offset, so a trial also says whether the clock survived
 * being followed.  It runs twice: once with the observer's clock reading the
 * same as the device's, which is how every other test has it, and once with
 * the two a long way apart.  A clock read in the wrong unit anywhere in
 * recovery would move every prediction in the second run and not the first,
 * so the two agreeing is what pins the unit (#50).
 *
 * The recovery: the other way into the same dead end, and the one the library
 * can see - a CLK1-6 or a UAP accepted by elimination rather than by a CRC,
 * under which every header fails its HEC.  This measures how long the library
 * takes to give that clock up, at a busy and at a quiet packet rate, and
 * whether the discovery that follows ends up back on the same clock.  It runs
 * twice as well: once with the clocks already given up remembered, and once
 * with them forgotten, which is what the library did before (#44).
 *
 * The false drops: how often a header fails its HEC while the clock and the
 * UAP are right, which is what btbb_process_packet() counts towards giving a
 * clock up.  Packets are run through the access code search at a range of bit
 * error rates, as a receiver's would be, and only those it finds are decoded.
 * FOLLOW_HEADER_FAILURES of those failures in a row gives up the clock, so
 * the conditional rate to that power is the chance of dropping a clock that
 * was right (#44).
 *
 * All three are slow enough to be run small by default: "test_clock_sweep
 * 400" is the full sweep the issues quote.
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
#include "stream_capture.h"

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
/* The UAP a header built for another piconet checks out against, and never
 * this piconet's: a header carrying it fails this piconet's HEC check. */
#define OTHER_UAP (TEST_UAP ^ 0xff)

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

/* ---------------------------- packets of this piconet -------------------- */

/* The access code search allows this many bit errors in the 64-bit code,
 * which is what ubertooth-rx passes by default. */
#define MAX_AC_ERRORS 2
/* An lt_addr of 1 and the packet type NULL, which has no payload. */
#define NULL_HEADER (1 | (PACKET_TYPE_NULL << 3))

static uint8_t hec_for(uint16_t data, uint8_t uap)
{
	int hec;

	for (hec = 0; hec < 256; hec++) {
		if (uap_from_hec(data, (uint8_t) hec) == uap)
			return (uint8_t) hec;
	}
	return 0;
}

/*
 * Whiten header bits the way a sender does, at its own clock: the same
 * sequence the library's unwhiten() takes off again, which it indexes by
 * CLK1-6 and nothing else.  A receiver dewhitening with a clock whose low six
 * bits differ gets another header out.
 */
static void whiten_bits(char *bits, int length, uint32_t clock)
{
	int index = INDICES[clock & 0x3f];
	int i;

	for (i = 0; i < length; i++) {
		bits[i] ^= WHITENING_DATA[index];
		index = (index + 1) % 127;
	}
}

/* One packet of this piconet: the access code, its trailer, and the 18 header
 * bits each sent three times, which is the rate 1/3 code.  Whitened at the
 * sender's clock where the caller asks for it. */
static void build_packet(char *stream, size_t size, uint8_t hec, int whitened,
			 uint32_t clock)
{
	uint64_t syncword = btbb_gen_syncword(TEST_LAP);
	uint32_t header = (uint32_t) NULL_HEADER | ((uint32_t) hec << 10);
	char bits[18];
	int i;

	memset(stream, 0, size);
	for (i = 0; i < 64; i++)
		stream[i] = (char) ((syncword >> i) & 1);
	for (i = 0; i < 4; i++)
		stream[64 + i] = (char) (((syncword >> 63) ^ (i + 1)) & 1);
	for (i = 0; i < 18; i++)
		bits[i] = (char) ((header >> i) & 1);
	if (whitened)
		whiten_bits(bits, 18, clock);
	for (i = 0; i < 18; i++) {
		stream[68 + i * 3] = bits[i];
		stream[69 + i * 3] = bits[i];
		stream[70 + i * 3] = bits[i];
	}
}

/*
 * The same packet as one reaches btbb_process_packet() while a piconet is
 * being followed: found by the access code search, which is what gives it its
 * LAP, and not whitened, so its header reads back as it was built whatever
 * clock the decoder is given.
 */
static btbb_packet *packet_with_hec(uint8_t hec)
{
	char stream[200];
	btbb_packet *pkt = NULL;

	build_packet(stream, sizeof(stream), hec, 0, 0);
	if (btbb_find_ac(stream, 1, TEST_LAP, 0, &pkt) != 0 || pkt == NULL)
		return NULL;
	btbb_packet_set_data(pkt, stream, 122, 0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	return pkt;
}

/* A header that checks out under this piconet's UAP, and one that does not. */
static btbb_packet *decodable_packet(void)
{
	return packet_with_hec(hec_for(NULL_HEADER, TEST_UAP));
}

static btbb_packet *failing_header_packet(void)
{
	return packet_with_hec(hec_for(NULL_HEADER, OTHER_UAP));
}

/* A piconet following a clock the library accepted, which is the state the
 * failure counting works in. */
static void follow_a_clock(btbb_piconet *pn)
{
	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);
	pn->clock_recovered = 1;
	pn->lock_worked = 0;
	pn->follow_header_failures = 0;
}

/* ------------------------------- the sweep ------------------------------- */

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
	/* what following each lock did with it */
	int correct;
	int wrong_recovered;
	int wrong_stuck;
	int false_drops;
};

/*
 * One attempt at CLK1-27 on a piconet that has its LAP, its UAP and its
 * CLK1-6: the state discovery leaves behind, and where hop reversal starts
 * whether this is the first attempt or one after a clock was given up.
 */
static enum outcome attempt(btbb_piconet *pn, btbb_piconet *device,
			    uint32_t true_clock, uint32_t apart,
			    struct totals *t)
{
	uint32_t first_pkt_time = true_clock - apart;
	uint32_t offset;
	enum outcome result = UNDECIDED;

	btbb_piconet_set_uap(pn, TEST_UAP);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);
	btbb_piconet_set_flag(pn, BTBB_IS_AFH, 1);
	pn->first_pkt_time = first_pkt_time;
	pn->clk6_offset = (int) (apart & 0x3f);
	pn->pattern_indices[0] = 0;
	pn->pattern_channels[0] = channel_at(device, true_clock);
	btbb_piconet_set_channel_seen(pn, pn->pattern_channels[0]);
	pn->packets_observed = 1;
	pn->winnowed = 0;

	if (btbb_init_hop_reversal(0, pn) == 0)
		return DROPPED;

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

	return result;
}

/*
 * Follow a lock the way the caller does, and see whether the library keeps
 * it.  The receiver sits on the offset the library gave it and hops and
 * counts from there, so the observer hears a packet only in the slot pairs
 * where that lands on the device's channel.  Which channel that is comes from
 * the piconet the library has built, map and all, as the board the caller
 * hops is given it; every packet the observer does hear arrives stamped with
 * the locked clock rather than the device's.  The
 * packets are whitened at the device's clock, as the air carries them, so
 * whether a header decodes is decided by the clock under test: a lock whose
 * CLK1-6 differed from the device's would dewhiten to another header and fail
 * its HEC.  Returns 1 if the library gave the clock up.
 */
static int follow_the_lock(btbb_piconet *pn, btbb_piconet *device,
			   btbb_packet *pkt, uint32_t true_clock)
{
	uint32_t locked = (pn->first_pkt_time + followed_offset(pn)) &
			  (SEQUENCE_LENGTH - 1);
	uint8_t hec = hec_for(NULL_HEADER, TEST_UAP);
	int lost = 0;
	int i;

	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);
	for (i = 0; i < FOLLOWED_SLOT_PAIRS && !lost; i++) {
		uint32_t step = (uint32_t) (i * 2);
		char stream[200];

		if (channel_at(pn, locked + step) !=
		    channel_at(device, true_clock + step))
			continue;	/* the receiver is on another channel */
		build_packet(stream, sizeof(stream), hec, 1,
			     true_clock + step);
		btbb_packet_set_data(pkt, stream, 122, 0,
				     (locked + step) << 1);
		btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);
		lost = btbb_process_packet(pkt, pn) == BTBB_PROCESS_LOCK_LOST;
	}
	return lost;
}

static void sweep(const char *label, uint32_t apart, int trials, int step)
{
	btbb_piconet *device = discovered(MAP_DEVICE);
	/* one packet object, whose symbols and clock each slot pair of the
	 * follow phase replaces; built by the access code search so that it
	 * carries this piconet's LAP */
	btbb_packet *pkt = decodable_packet();
	struct totals t;
	/* Over a sweep the library writes thousands of progress notes and
	 * dumps every packet it decodes.  Swallowed: the counts below are
	 * what this test reports. */
	char swallowed[256];
	int i;

	memset(&t, 0, sizeof(t));
	CHECK(pkt != NULL);
	if (pkt == NULL || capture_start() != 0) {
		btbb_piconet_unref(device);
		return;
	}
	for (i = 0; i < trials; i++) {
		uint32_t true_clock = FIRST_CLOCK + (uint32_t) (i * step);
		btbb_piconet *pn = discovered(MAP_OBSERVER);
		enum outcome result = attempt(pn, device, true_clock, apart,
					      &t);
		int lost;

		t.counts[result]++;
		if (result == TRUE_LOCK || result == WRONG_LOCK) {
			lost = follow_the_lock(pn, device, pkt, true_clock);
			if (result == TRUE_LOCK) {
				if (lost)
					t.false_drops++;
				else
					t.correct++;
			} else if (lost) {
				t.wrong_recovered++;
			} else {
				t.wrong_stuck++;
			}
		}
		btbb_piconet_unref(pn);
	}
	capture_stop(swallowed, sizeof(swallowed), swallowed,
		     sizeof(swallowed));

	fprintf(stderr, "%-8s %d clocks from 0x%07x step %d, observer clock %u slots away\n",
		label, trials, FIRST_CLOCK, step, apart);
	for (i = 0; i < OUTCOMES; i++)
		fprintf(stderr, "  %-11s %4d\n", NAMES[i], t.counts[i]);
	fprintf(stderr, "  followed for %d slot pairs: correct %d, wrong and stuck %d, "
		"wrong then recovered %d, false drops %d\n",
		FOLLOWED_SLOT_PAIRS, t.correct, t.wrong_stuck,
		t.wrong_recovered, t.false_drops);
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
	/* Which the follow phase then says of the locks themselves: a wrong
	 * CLK1-27 carries discovery's CLK1-6, so every header it hears still
	 * dewhitens and checks out and nothing in the library sees the wrong
	 * channel, while a clock that is right is never given up. */
	CHECK(t.wrong_stuck == t.counts[WRONG_LOCK]);
	CHECK(t.wrong_recovered == 0);
	CHECK(t.correct == t.counts[TRUE_LOCK]);
	CHECK(t.false_drops == 0);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(device);
}

/*
 * A control for the follow phase: the same packets against a clock one slot
 * out, which the library does give up.  Without it the follow phase above
 * could be passing because nothing it does could fail.  One slot is the
 * sharpest case: the AFH same channel mechanism masks CLK1 off, so the
 * receiver still lands on the device's channel every time and hears every
 * packet, and only the dewhitening tells the two clocks apart.
 */
static void the_follow_phase_can_fail(void)
{
	btbb_piconet *device = discovered(MAP_DEVICE);
	btbb_piconet *pn = discovered(MAP_DEVICE);
	btbb_packet *pkt = decodable_packet();
	char swallowed[256];
	int lost;

	CHECK(pkt != NULL);
	if (pkt == NULL || capture_start() != 0) {
		btbb_piconet_unref(device);
		btbb_piconet_unref(pn);
		return;
	}
	follow_a_clock(pn);
	/* the clock the receiver counts on: the device's, one slot out */
	pn->first_pkt_time = FIRST_CLOCK;
	btbb_piconet_set_clk_offset(pn, 2);	/* one 625 us slot */
	lost = follow_the_lock(pn, device, pkt, FIRST_CLOCK);
	capture_stop(swallowed, sizeof(swallowed), swallowed,
		     sizeof(swallowed));

	fprintf(stderr, "  a clock one slot out, same channels: %s\n",
		lost ? "given up" : "KEPT");
	CHECK(lost);
	btbb_packet_unref(pkt);
	btbb_piconet_unref(pn);
	btbb_piconet_unref(device);
}

/* ----------------- recovering from a clock that never decodes ------------ */

/*
 * How far apart the failing headers arrive, in 625 us slots: a link carrying
 * traffic, where a packet comes every slot pair, and a quiet one at four
 * packets a second.
 */
#define BUSY_SLOTS 2
#define QUIET_SLOTS 400

struct recovery {
	int locks;		/* attempts that accepted a clock */
	int lost;		/* locks the failing headers gave up */
	int gave_up_the_true_clock;	/* of those, the device's own clock */
	int stuck;		/* restarts that chose the same clock again */
	/* and what the others found instead */
	int true_clock;
	int another_wrong;
	int no_lock;
	long headers;		/* headers to give one up, summed */
	long slots;		/* and the slots they spanned */
};

/*
 * Follow a clock whose headers all fail, which is what a CLK1-6 or a UAP
 * accepted by elimination rather than by a CRC comes to.  Returns how many
 * headers it took for the library to give the clock up, with the slots they
 * spanned in *slots, or 0 if it never did.
 */
static int lose_the_clock(btbb_piconet *pn, btbb_packet *bad,
			  uint32_t first_clkn, uint32_t slots_apart,
			  uint32_t *slots)
{
	int sent;

	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);
	for (sent = 1; sent <= 4 * FOLLOW_HEADER_FAILURES; sent++) {
		uint32_t span = (uint32_t) (sent - 1) * slots_apart;

		bad->clkn = first_clkn + span;
		if (btbb_process_packet(bad, pn) == BTBB_PROCESS_LOCK_LOST) {
			*slots = span;
			return sent;
		}
	}
	*slots = 0;
	return 0;
}

/*
 * A clock given up, and then the same attempt again from the channels that
 * were known when the first one started.  With the clocks it has given up
 * remembered, the library cannot end up back on the one that failed; with
 * them forgotten, which is what it did before, the same inputs give the same
 * clock every time.
 *
 * The scenario fails every header whatever clock the winnowing produced, so
 * in some trials the clock given up is the device's own, which is what a
 * right clock under a wrong UAP comes to.  Those trials are the pessimistic
 * reading of the measurement: the second attempt here is handed the same UAP,
 * where real discovery that was wrong about the UAP goes on to find another
 * one, and a rejection made under one UAP is not held against a search using
 * another (tests/test_piconet.c).
 */
static void recovery_sweep(const char *label, int trials, int step,
			   uint32_t slots_apart, int remember)
{
	btbb_piconet *device = discovered(MAP_DEVICE);
	btbb_packet *bad = failing_header_packet();
	struct recovery r;
	struct totals ignored;
	char swallowed[256];
	int i;

	memset(&r, 0, sizeof(r));
	memset(&ignored, 0, sizeof(ignored));
	CHECK(bad != NULL);
	if (bad == NULL || capture_start() != 0) {
		btbb_piconet_unref(device);
		return;
	}
	for (i = 0; i < trials; i++) {
		uint32_t true_clock = FIRST_CLOCK + (uint32_t) (i * step);
		btbb_piconet *pn = discovered(MAP_OBSERVER);
		enum outcome first = attempt(pn, device, true_clock, 0,
					     &ignored);
		enum outcome second;
		uint32_t offset, slots = 0;
		int headers;

		if (first != TRUE_LOCK && first != WRONG_LOCK) {
			btbb_piconet_unref(pn);
			continue;
		}
		r.locks++;
		offset = followed_offset(pn);
		headers = lose_the_clock(pn, bad, true_clock, slots_apart,
					 &slots);
		if (headers == 0) {
			btbb_piconet_unref(pn);
			continue;
		}
		r.lost++;
		if (first == TRUE_LOCK)
			r.gave_up_the_true_clock++;
		r.headers += headers;
		r.slots += slots;
		if (!remember)
			pn->num_rejected = 0;
		memcpy(pn->afh_map, MAP_OBSERVER, sizeof(pn->afh_map));
		precalc(pn);
		second = attempt(pn, device, true_clock, 0, &ignored);
		if (second != TRUE_LOCK && second != WRONG_LOCK)
			r.no_lock++;
		else if (followed_offset(pn) == offset)
			r.stuck++;	/* the clock that just failed */
		else if (second == TRUE_LOCK)
			r.true_clock++;
		else
			r.another_wrong++;
		btbb_piconet_unref(pn);
	}
	capture_stop(swallowed, sizeof(swallowed), swallowed,
		     sizeof(swallowed));

	fprintf(stderr, "%-24s %d locks (%d of them the device's own clock), %d given up; "
		"the search that followed: %d the same clock, %d the device's, "
		"%d another wrong one, %d no lock",
		label, r.locks, r.gave_up_the_true_clock, r.lost, r.stuck,
		r.true_clock, r.another_wrong, r.no_lock);
	if (r.lost > 0)
		fprintf(stderr, ", after %.1f headers over %.0f ms",
			(double) r.headers / (double) r.lost,
			0.625 * (double) r.slots / (double) r.lost);
	fprintf(stderr, "\n");

	/* The scenario has to produce locks to lose, and every one of them
	 * has to be given up: that part is #59's and is measured here only so
	 * the time it takes is. */
	CHECK(r.locks > 0);
	CHECK(r.lost == r.locks);
	/* And it takes the same headers and the same time every trial: the
	 * full count, or half of it where that already spans the time the
	 * shorter run needs. */
	if (r.lost > 0) {
		int expected = (int) slots_apart * (FOLLOW_FAILURES_IN_TIME - 1)
				>= FOLLOW_FAILURE_SLOTS
			? FOLLOW_FAILURES_IN_TIME : FOLLOW_HEADER_FAILURES;

		CHECK(r.headers == (long) r.lost * expected);
		CHECK(r.slots == (long) r.lost * (expected - 1) *
				 (long) slots_apart);
	}
	/* And then the whole point: a clock that never decoded a header is
	 * not chosen again, where before the same search ended on it. */
	if (remember)
		CHECK(r.stuck == 0);
	else
		CHECK(r.stuck == r.lost);
	btbb_packet_unref(bad);
	btbb_piconet_unref(device);
}

/* ------------------------- false drops on a good clock ------------------- */

#define BER_TRIALS 20000

static uint32_t rng_state = 12345;

/* A little arithmetic generator, so the run is the same everywhere. */
static double urand(void)
{
	rng_state = rng_state * 1103515245u + 12345u;
	return (double) ((rng_state >> 8) & 0xffffff) / 16777216.0;
}

static double false_drop_rate(double ber, int trials)
{
	char clean[200];
	long found = 0, checked = 0, failed = 0, longest = 0, run = 0;
	long drops = 0;
	/* The noisy packets also go through the path that counts failures, on
	 * a piconet following a clock that is right, at the slowest rate the
	 * time bound is measured at.  That is the worst case for it: there a
	 * run of ten failures is enough, where a busy link needs twenty. */
	btbb_piconet *pn = discovered(MAP_DEVICE);
	uint32_t clkn = 0;
	char swallowed[256];
	int t;

	follow_a_clock(pn);
	if (capture_start() != 0) {
		CHECK(0);
		btbb_piconet_unref(pn);
		return 0.0;
	}
	build_packet(clean, sizeof(clean), hec_for(NULL_HEADER, TEST_UAP), 0,
		     0);
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
		pkt->clkn = clkn;
		clkn += QUIET_SLOTS;
		if (btbb_process_packet(pkt, pn) == BTBB_PROCESS_LOCK_LOST) {
			drops++;
			follow_a_clock(pn);	/* carry on measuring */
		}
		btbb_packet_unref(pkt);
	}
	capture_stop(swallowed, sizeof(swallowed), swallowed,
		     sizeof(swallowed));
	btbb_piconet_unref(pn);
	{
		double cond = checked ? (double) failed / (double) checked
				      : 0.0;
		double chance = 1.0;
		double in_time = 1.0;
		int k;

		for (k = 0; k < FOLLOW_HEADER_FAILURES; k++) {
			chance *= cond;
			if (k < FOLLOW_FAILURES_IN_TIME)
				in_time *= cond;
		}
		fprintf(stderr, "  ber %.3f: %ld found, %ld checked, %ld failed, "
			"%.4f of them, longest run %ld, %d in a row %.3g, "
			"%d in a row %.3g, clocks dropped %ld\n",
			ber, found, checked, failed, cond, longest,
			FOLLOW_HEADER_FAILURES, chance,
			FOLLOW_FAILURES_IN_TIME, in_time, drops);
		/* Nowhere near either run that gives a clock up: the full
		 * count, or the shorter one the time bound accepts.  And the
		 * clock that was right survived every one of these packets
		 * through the library's own path. */
		CHECK(longest < FOLLOW_FAILURES_IN_TIME);
		CHECK(drops == 0);
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
	the_follow_phase_can_fail();

	fprintf(stderr, "a clock whose headers all fail, and the search that follows:\n");
	recovery_sweep("busy link, remembered", trials, step, BUSY_SLOTS, 1);
	recovery_sweep("busy link, forgotten", trials, step, BUSY_SLOTS, 0);
	recovery_sweep("quiet link, remembered", trials, step, QUIET_SLOTS, 1);
	recovery_sweep("quiet link, forgotten", trials, step, QUIET_SLOTS, 0);
	/* What the quiet link cost before the time bound, which is the rule
	 * it replaced rather than a run of this program: the full count of
	 * failures, at the same packet rate. */
	fprintf(stderr, "%-24s %d headers over %.0f ms\n",
		"quiet, before the bound", FOLLOW_HEADER_FAILURES,
		0.625 * (double) ((FOLLOW_HEADER_FAILURES - 1) * QUIET_SLOTS));

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
