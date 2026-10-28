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
 * How many packets in a row have to fail to decode while following a piconet
 * before the clock the library recovered is given up.
 *
 * Following starts once a clock is accepted, and from then on nothing
 * winnows: a clock that was wrong is used for every packet that follows.
 * Every candidate shares the CLK1-6 that CRC-confirmed discovery found, so
 * what a wrong candidate gets wrong is the hopping, not the dewhitening; the
 * clock bits a decode does depend on are CLK1-6 and the UAP, and those can
 * also be wrong, accepted by elimination rather than by a CRC.  Then every
 * packet fails to decode and, until now, there was no way back.
 *
 * The run has to be long enough that a correctly followed piconet on a noisy
 * link does not cross it: a failure there is a packet lost to bit errors, and
 * giving the clock up costs a fresh discovery, which #49 measured at 10.7 to
 * 14.7 packets.  Twenty is longer than that, so the library cannot give up a
 * lock faster than it can win one back.  Against a wrong CLK1-6 or UAP it
 * costs almost nothing: the header's 8-bit HEC has to agree with the
 * piconet's UAP, so about one packet in 256 decodes by chance and twenty
 * failures in a row arrive in about twenty packets.
 */
#define FOLLOW_DECODE_FAILURES 20

struct btbb_piconet {

	uint32_t refcount;

	uint32_t flags;

	/* true if using a particular aliased receiver implementation */
	int aliased;

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

	/* Packets in a row that failed to decode while following, counted
	 * against FOLLOW_DECODE_FAILURES above. */
	int follow_decode_failures;

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

/* number of aliased channels received */
#define ALIASED_CHANNELS 25

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
