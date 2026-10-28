/* -*- c -*- */
/*
 * Copyright 2007 - 2013 Dominic Spill, Michael Ossmann, Will Code
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
#ifndef INCLUDED_BLUETOOTH_PICONET_H
#define INCLUDED_BLUETOOTH_PICONET_H
#include "btbb.h"

/* maximum number of hops to remember */
#define MAX_PATTERN_LENGTH 1000

/* number of channels in use */
#define BT_NUM_CHANNELS 79

/* Nmin, the fewest used channels AFH is defined for
 * (Bluetooth Core v5.1 | Vol 2, Part B, Section 2.3.1). */
#define AFH_MIN_USED_CHANNELS 20

/*
 * How many further observed hops have to agree with the last CLK1-27
 * candidate standing before it is taken as the piconet's clock.
 *
 * Candidates are narrowed by comparing each observed hop with the hop the
 * library predicts, and while the AFH map is still being learned those
 * predictions are wrong for the channels not yet seen.  A clock that is not
 * the piconet's can survive every comparison made so far and be the last one
 * left; once it is accepted, following begins and nothing winnows again, so
 * every packet after it fails to decode and there is no way back.
 *
 * Each confirming hop is another chance for a wrong candidate to disagree,
 * and another chance for the map to gain the channel that would have ruled it
 * out, which starts the search again from the map as it then stands.  The
 * cost is this many more packets before following starts.
 */
#define CLK27_CONFIRM_HOPS 4

/*
 * How many packet headers in a row have to fail their HEC while following a
 * piconet before the clock the library recovered is given up.
 *
 * Following starts once a clock is accepted, and from then on nothing
 * winnows: a clock that was wrong is used for every packet that follows.
 * Every candidate shares the CLK1-6 that discovery found, so what a wrong
 * candidate gets wrong is which channel to listen on, not the dewhitening.
 * The clock bits a header does depend on are CLK1-6 and the UAP, and those
 * can be wrong, accepted by elimination rather than by a CRC.  Then every
 * header fails and, until now, there was no way back.
 *
 * Only the HEC counts, not any failure to decode: it is checked against the
 * piconet's UAP over a header dewhitened with CLK1-6, so it is evidence about
 * exactly the two things being given up here.  A payload that will not decode
 * says nothing about either, and counting those would throw away a good clock
 * on a link with bit errors in it.
 *
 * The run still has to be long enough that bit errors in the headers
 * themselves do not cross it on a clock that is right.  What protects it
 * there is the access code search the packet came through, which allows a
 * couple of bit errors in 64: a packet noisy enough to break its own
 * majority-voted header hardly ever gets that far.  Measured over a run of
 * noisy packets (tests/test_clock_sweep.c), of the headers that reach the HEC
 * check under the right clock and UAP, 0.15% fail it at a bit error rate of
 * 0.5% and 24% fail at 10%, where the access code already rejects 96 packets
 * in 100.  Twenty in a row is 4e-57 and 5e-13 of a chance in those two
 * cases.  Against a wrong CLK1-6 or UAP it costs almost nothing: the HEC is 8
 * bits, so about one header in 256 passes by chance and twenty failures in a
 * row arrive in about twenty packets.
 *
 * The run is counted in packets, not in seconds, so on a link quiet enough
 * that twenty headers take hours it is twenty headers all the same.  The
 * rates above are what makes that safe rather than any time bound.
 */
#define FOLLOW_HEADER_FAILURES 20

/*
 * The time a clock whose headers keep failing may be followed for, in 625 us
 * slots, and how many failures are enough once it has passed.  3200 slots is
 * 2 seconds.
 *
 * At the traffic a followed link carries, FOLLOW_HEADER_FAILURES headers
 * arrive in a few tens of milliseconds, so the count alone already gives up a
 * wrong clock well inside a second.  A near-idle link is what this is for:
 * there twenty headers can take minutes, and all that time the caller's
 * receiver is hopping on an offset that has never decoded anything.  Once the
 * run has lasted this long, the failures it already holds are taken as
 * enough, so the answer arrives within a few seconds of the first failure
 * however quiet the link is.
 *
 * Fewer failures is weaker evidence, which is why this path needs the time as
 * well: a run that reaches it has to have been failing for seconds without
 * one header checking out.  Measured over noisy packets
 * (tests/test_clock_sweep.c), the longest run of failed headers under the
 * right clock and UAP is three in 20000 packets at a bit error rate of 10%,
 * where the access code search already rejects 96 packets in 100.
 */
#define FOLLOW_FAILURE_SLOTS 3200
#define FOLLOW_FAILURES_IN_TIME 10

/*
 * How many clock offsets a piconet remembers having given up, and how many
 * clocks may be given up before the library says it cannot find one.
 *
 * Discovery starting over used to have no memory of the clock that had just
 * failed, so it could choose the same one again, accept it again, fail to
 * decode again, and go round that loop for as long as the capture ran.  A
 * rejected clock is kept out of the candidate list instead, so each attempt
 * rules out one more of them.
 *
 * The list is deliberately short.  A clock is rejected on the evidence of one
 * follow attempt, and the evidence says the clock or the UAP was wrong
 * without saying which, so a rejection can in principle rule out a clock that
 * would have been right under the UAP found next time.  Eight entries out of
 * the 2^21 clock values an attempt starts from make that vanishingly
 * unlikely, and the oldest entry is dropped once the list is full, so what is
 * remembered is always the clocks that failed most recently.
 */
#define MAX_REJECTED_CLOCKS 8
#define LOCK_LOSS_TRIES 3

struct btbb_piconet {

	uint32_t refcount;

	uint32_t flags;

	/* AFH channel map - either read or derived from observed packets */
	uint8_t afh_map[10];

	/* Number of entries in afh_bank, i.e. channels 0-78 the map marks
	 * used.  Maintained by precalc(), which is the only writer. */
	uint8_t used_channels;

	/* lower address part (of master's BD_ADDR) */
	uint32_t LAP;

	/* upper address part (of master's BD_ADDR) */
	uint8_t UAP;

	/* non-significant address part (of master's BD_ADDR) */
	uint16_t NAP;

	/* CLK1-27 candidates */
	uint32_t *clock_candidates;

	/* these values for single_hop() can be precalculated */
	int b, e;

	/* these values for single_hop() can be precalculated in part (e.g. a1
	 * is the precalculated part of a) */
	int a1, c1, d1;

	/* Basic mapping table (Vol 2, Part B, Section 2.6.2): even channels in
	 * ascending order followed by odd channels.  AFH consults it first, so
	 * it always describes the full basic sequence. */
	int bank[BT_NUM_CHANNELS];

	/* AFH mapping table (2.6.3.1): the basic table with every unused
	 * channel removed, holding used_channels entries. */
	int afh_bank[BT_NUM_CHANNELS];

	/* number of candidates for CLK1-27 */
	int num_candidates;

	/* The hop sequence clock_candidates was chosen from: whether hop
	 * selection was adapted to candidates_afh_map, and that map.  A
	 * piconet whose map has moved on since would winnow against a
	 * sequence it no longer follows. */
	uint8_t candidates_afh;
	uint8_t candidates_afh_map[10];

	/* number of packets observed during one attempt at UAP/clock discovery */
	int packets_observed;

	/* total number of packets observed */
	int total_packets_observed;

	/* number of observed packets that have been used to winnow the candidates */
	int winnowed;

	/* How many winnowing comparisons have left one candidate standing.
	 * The first of them is the comparison that chose it; the rest are the
	 * hops that confirm it, counted against CLK27_CONFIRM_HOPS above. */
	int clk27_confirmations;

	/* True once hop reversal has accepted a clock for this piconet, which
	 * is the clock that can be given up again.  A caller that set the
	 * clock itself, from an adapter say, owns it: the library has no
	 * better one to fall back on, so it does not take that one away. */
	uint8_t clock_recovered;

	/* Packet headers in a row that failed their HEC while following,
	 * counted against FOLLOW_HEADER_FAILURES above. */
	int follow_header_failures;

	/* The piconet clock, CLK1-27, of the first failure of that run, so
	 * that how long the run has been going is known in slots. */
	uint32_t first_failure_time;

	/* True once a header checked out under the clock being followed: that
	 * clock has decoded something, whatever happens later, so giving it
	 * up does not mean it was never the piconet's. */
	uint8_t lock_worked;

	/* Clocks given up for this LAP, as offsets between the piconet's
	 * clock and the observer's in 625 us slots.  An offset survives
	 * discovery starting over, where a candidate CLK1-27 value could not:
	 * candidates are clocks at the first packet of an attempt, and the
	 * next attempt has a different first packet.  Cleared when the LAP
	 * changes, those being another piconet's clocks. */
	uint32_t rejected_offsets[MAX_REJECTED_CLOCKS];
	int num_rejected;

	/* How many clocks have been given up for this LAP, and whether the
	 * library has already said it cannot find one that works. */
	int lock_losses;
	uint8_t lock_loss_reported;

	/* CLK1-6 candidates */
	int clock6_candidates[64];

	/* remember patterns of observed hops */
	int pattern_indices[MAX_PATTERN_LENGTH];
	uint8_t pattern_channels[MAX_PATTERN_LENGTH];

	/* Offset between CLKN (local) and CLK of piconet, in 312.5 us CLKN
	 * ticks.  Valid once BTBB_CLK27_VALID is set, and what
	 * btbb_piconet_get_clk_offset() returns. */
	int clk_offset;

	/* Offset between the local and the piconet clock in 625 us CLK1-6
	 * slots, 6 bits wide.  Found first, while BTBB_CLK6_VALID is set and
	 * CLK1-27 is still being searched for, so it is a separate field:
	 * masking clk_offset's CLKN ticks to 6 bits would not give slots. */
	int clk6_offset;

	/* The observer's own clock when the first packet arrived, as CLK1-27:
	 * a count of 625 us slots, the same unit as btbb_packet's clkn, which
	 * is where it comes from.  Candidate clocks, the hop differences in
	 * pattern_indices and clk6_offset are all in those slots too; the
	 * only clock here in 312.5 us CLKN ticks is clk_offset above, which
	 * is what a receiver is told to hop on. */
	uint32_t first_pkt_time;

	/* queue of packets to be decoded */
	pkt_queue *queue;
};

/* the number of CLK1-27 values, which is also the length of a full hopping
 * sequence: hops happen at every other clock value */
#define SEQUENCE_LENGTH 134217728

/* do all the precalculation that can be done before knowing the address */
void precalc(btbb_piconet *pnet);

/* do precalculation that requires the address */
void address_precalc(int address, btbb_piconet *pnet);

/* drop-in replacement for perm5() using lookup table */
int fast_perm(int z, int p_high, int p_low);

/* 5 bit permutation */
/* assumes z is constrained to 5 bits, p_high to 5 bits, p_low to 9 bits */
int perm5(int z, int p_high, int p_low);

/* The channel a particular hop lands on, computed when it is asked for: the
 * piconet's current AFH map is part of the answer, and that map is still being
 * learned while a clock is being recovered. */
char single_hop(int clock, btbb_piconet *pnet);

void try_hop(btbb_packet *pkt, btbb_piconet *pn);

#endif /* INCLUDED_BLUETOOTH_PICONET_H */
