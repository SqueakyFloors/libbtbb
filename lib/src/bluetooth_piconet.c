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

#include "bluetooth_packet.h"
#include "bluetooth_piconet.h"
#include "btbb_uthash.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int perm_table_initialized = 0;
char perm_table[0x20][0x20][0x200];

/* Report how UAP and clock discovery is getting on. These go to stderr so
 * that a tool dumping capture data to stdout holds nothing but the dump,
 * which is the rule the LE decode complaints follow too. What the caller
 * asked to be printed, btbb_print_afh_map() below, still goes to stdout. */
#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static void piconet_note(const char *fmt, ...)
{
	va_list ap;

	/* keep the two streams in order when both are a terminal */
	fflush(stdout);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
}

/* reset UAP/clock discovery */
static void reset(btbb_piconet *pn);

btbb_piconet *
btbb_piconet_new(void)
{
	btbb_piconet *pn = (btbb_piconet *)calloc(1, sizeof(btbb_piconet));

	if (pn == NULL) {
		/* reported here, as btbb_packet_new() reports a packet it
		 * could not allocate, so that a caller whose piconet never
		 * arrives has a reason for it */
		piconet_note("error: could not allocate a piconet\n");
		return NULL;
	}
	pn->refcount = 1;
	return pn;
}

void
btbb_piconet_ref(btbb_piconet *pn)
{
	pn->refcount++;
}

void
btbb_piconet_unref(btbb_piconet *pn)
{
	pn->refcount--;
	if (pn->refcount == 0) {
		/* A piconet dropped while its clock was still being recovered
		 * still owns its candidate list. */
		free(pn->clock_candidates);
		free(pn);
	}
}

/* A bit of a hack? to set survey mode */
static int survey_mode = 0;
int btbb_init_survey() {
	survey_mode = 1;
	return 0;
}

void btbb_init_piconet(btbb_piconet *pn, uint32_t lap)
{
	/* The clocks given up so far are this piconet's.  Another LAP is
	 * another piconet, whose clock has nothing to do with them. */
	if (lap != pn->LAP) {
		pn->num_rejected = 0;
		pn->lock_losses = 0;
		pn->lock_loss_reported = 0;
	}
	pn->LAP = lap;
	btbb_piconet_set_flag(pn, BTBB_LAP_VALID, 1);
}

void btbb_piconet_set_flag(btbb_piconet *pn, int flag, int val)
{
	uint32_t mask = 1L << flag;
	pn->flags &= ~mask;
	if (val)
		pn->flags |= mask;
}

int btbb_piconet_get_flag(const btbb_piconet *pn, const int flag)
{
	uint32_t mask = 1L << flag;
	return ((pn->flags & mask) != 0);
}

void btbb_piconet_set_uap(btbb_piconet *pn, uint8_t uap)
{
	pn->UAP = uap;
	btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 1);
}

uint8_t btbb_piconet_get_uap(const btbb_piconet *pn)
{
	return pn->UAP;
}

uint32_t btbb_piconet_get_lap(const btbb_piconet *pn)
{
	return pn->LAP;
}

uint16_t btbb_piconet_get_nap(const btbb_piconet *pn)
{
	return pn->NAP;
}

uint64_t btbb_piconet_get_bdaddr(const btbb_piconet *pn)
{
	return ((uint64_t) pn->NAP) << 32 | ((uint32_t) pn->UAP) << 24 | pn->LAP;
}

int btbb_piconet_get_clk_offset(const btbb_piconet *pn)
{
	return pn->clk_offset;
}

void btbb_piconet_set_clk_offset(btbb_piconet *pn, int clk_offset)
{
	pn->clk_offset = clk_offset;
}

void btbb_piconet_set_afh_map(btbb_piconet *pn, uint8_t *afh_map) {
	int i;
	for(i=0; i<10; i++)
		pn->afh_map[i] = afh_map[i];
	/* Rebuild the mapping tables and the used-channel count from the new
	 * map, so they describe it before anything hops on it. */
	precalc(pn);
	/* With the address known as well, the piconet can predict hops on the
	 * new map straight away.  The UAP is widened before it is shifted,
	 * which a signed int would not hold. */
	if(btbb_piconet_get_flag(pn, BTBB_UAP_VALID))
		address_precalc((int) (((((uint32_t) pn->UAP) << 24) |
					pn->LAP) & 0xfffffff), pn);
}

uint8_t *btbb_piconet_get_afh_map(btbb_piconet *pn) {
	return pn->afh_map;
}

uint8_t btbb_piconet_set_channel_seen(btbb_piconet *pn, uint8_t channel)
{
	if(channel < BT_NUM_CHANNELS &&
	   !(pn->afh_map[channel/8] & 0x1 << (channel % 8))) {
		pn->afh_map[channel/8] |= 0x1 << (channel % 8);
		/* The AFH mapping table has to follow the map it is built
		 * from, or hop selection would index past its entries. */
		precalc(pn);
		return 1;
	}
	return 0;
}

uint8_t btbb_piconet_clear_channel_seen(btbb_piconet *pn, uint8_t channel)
{
	if(channel < BT_NUM_CHANNELS &&
	   (pn->afh_map[channel/8] & 0x1 << (channel % 8))) {
		pn->afh_map[channel/8] &= ~(0x1 << (channel % 8));
		precalc(pn);
		return 1;
	}
	return 0;
}

uint8_t btbb_piconet_get_channel_seen(btbb_piconet *pn, uint8_t channel)
{
	if(channel < BT_NUM_CHANNELS)
		return ( pn->afh_map[channel/8] & (1 << (channel % 8)) ) != 0;
	else
		return 1;
}

/* do all the precalculation that can be done before knowing the address */
void precalc(btbb_piconet *pn)
{
	int i = 0;
	int j = 0;
	int chan;

	/* populate frequency register bank*/
	for (i = 0; i < BT_NUM_CHANNELS; i++)
		pn->bank[i] = ((i * 2) % BT_NUM_CHANNELS);
	/* actual frequency is 2402 + pn->bank[i] MHz */

	/* The AFH mapping table is built by walking the basic table, so it
	 * keeps that channel order, and only the entries actually placed are
	 * counted, so a stray reserved bit 79 in the map cannot index past
	 * the used channels. */
	for (i = 0; i < BT_NUM_CHANNELS; i++) {
		chan = pn->bank[i];
		if(btbb_piconet_get_channel_seen(pn, chan))
			pn->afh_bank[j++] = chan;
	}
	pn->used_channels = j;
}

/* do precalculation that requires the address */
void address_precalc(int address, btbb_piconet *pn)
{
	/* precalculate some of single_hop()/gen_hop()'s variables */
	pn->a1 = (address >> 23) & 0x1f;
	pn->b = (address >> 19) & 0x0f;
	pn->c1 = ((address >> 4) & 0x10) +
		((address >> 3) & 0x08) +
		((address >> 2) & 0x04) +
		((address >> 1) & 0x02) +
		(address & 0x01);
	pn->d1 = (address >> 10) & 0x1ff;
	pn->e = ((address >> 7) & 0x40) +
		((address >> 6) & 0x20) +
		((address >> 5) & 0x10) +
		((address >> 4) & 0x08) +
		((address >> 3) & 0x04) +
		((address >> 2) & 0x02) +
		((address >> 1) & 0x01);
}

#ifdef WC4
/* These are optimization experiments, which don't help much for
 * x86. Hold on to them to see whether they're useful on ARM. */

#ifdef NEVER
#define BUTTERFLY(z,p,c,a,b)					     \
	if ( ((p&(1<<c))!=0) & (((z&(1<<a))!=0) ^ ((z&(1<<b))!=0)) ) \
		z ^= ((1<<a)|(1<<b))
#endif

#define BUTTERFLY(z,p,c,a,b) \
	if ( (((z>>a)^(z>>b)) & (p>>c)) & 0x1 ) \
		z ^= ((1<<a)|(1<<b))

int perm5(int z, int p_high, int p_low)
{
	int p = (p_high << 5) | p_low;
	BUTTERFLY(z,p,13,1,2);
	BUTTERFLY(z,p,12,0,3);
	BUTTERFLY(z,p,11,1,3);
	BUTTERFLY(z,p,10,2,4);
	BUTTERFLY(z,p, 9,0,3);
	BUTTERFLY(z,p, 8,1,4);
	BUTTERFLY(z,p, 7,3,4);
	BUTTERFLY(z,p, 6,0,2);
	BUTTERFLY(z,p, 5,1,3);
	BUTTERFLY(z,p, 4,0,4);
	BUTTERFLY(z,p, 3,3,4);
	BUTTERFLY(z,p, 2,1,2);
	BUTTERFLY(z,p, 1,2,3);
	BUTTERFLY(z,p, 0,0,1);

	return z;
}
#endif // WC4

/* 5 bit permutation */
/* assumes z is constrained to 5 bits, p_high to 5 bits, p_low to 9 bits */
int perm5(int z, int p_high, int p_low)
{
	int i, tmp, output, z_bit[5], p[14];
	int index1[] = {0, 2, 1, 3, 0, 1, 0, 3, 1, 0, 2, 1, 0, 1};
	int index2[] = {1, 3, 2, 4, 4, 3, 2, 4, 4, 3, 4, 3, 3, 2};

	/* bits of p_low and p_high are control signals */
	for (i = 0; i < 9; i++)
		p[i] = (p_low >> i) & 0x01;
	for (i = 0; i < 5; i++)
		p[i+9] = (p_high >> i) & 0x01;

	/* bit swapping will be easier with an array of bits */
	for (i = 0; i < 5; i++)
		z_bit[i] = (z >> i) & 0x01;

	/* butterfly operations */
	for (i = 13; i >= 0; i--) {
		/* swap bits according to index arrays if control signal tells us to */
		if (p[i]) {
			tmp = z_bit[index1[i]];
			z_bit[index1[i]] = z_bit[index2[i]];
			z_bit[index2[i]] = tmp;
		}
	}

	/* reconstruct output from rearranged bits */
	output = 0;
	for (i = 0; i < 5; i++)
		output += z_bit[i] << i;

	return(output);
}

void perm_table_init(void)
{
	/* populate perm_table for all possible inputs */
	int z, p_high, p_low;
	for (z = 0; z < 0x20; z++)
		for (p_high = 0; p_high < 0x20; p_high++)
			for (p_low = 0; p_low < 0x200; p_low++)
				perm_table[z][p_high][p_low] = perm5(z, p_high, p_low);
}

/* drop-in replacement for perm5() using lookup table */
int fast_perm(int z, int p_high, int p_low)
{
	if (!perm_table_initialized) {
		perm_table_init();
		perm_table_initialized = 1;
	}

	return(perm_table[z][p_high][p_low]);
}

/* determine channel for a particular hop */
/* borrowed from ubertooth firmware to support AFH */
char single_hop(int clock, btbb_piconet *pn)
{
	int a, c, d, x, y1, y2, perm, next_channel;
	uint32_t base_f, f, f_dash;

	int n_used = pn->used_channels;
	/* Fewer used channels than AFH is defined for (or an unset map) falls
	 * back to basic hopping, which also avoids dividing by zero. */
	int afh = btbb_piconet_get_flag(pn, BTBB_IS_AFH) &&
			n_used >= AFH_MIN_USED_CHANNELS;

	/* AFH same channel mechanism (2.6.1): the peripheral slot reuses the
	 * channel of the central slot before it, i.e. CLK1 = 0. */
	if (afh)
		clock &= ~0x2;
	/* following variable names used in section 2.6 of the spec */
	x = (clock >> 2) & 0x1f;
	y1 = (clock >> 1) & 0x01;
	y2 = y1 << 5;
	a = (pn->a1 ^ (clock >> 21)) & 0x1f;
	/* b is already defined */
	c = (pn->c1 ^ (clock >> 16)) & 0x1f;
	d = (pn->d1 ^ (clock >> 7)) & 0x1ff;
	/* e is already defined */
	base_f = (clock >> 3) & 0x1fffff0;
	f = base_f % BT_NUM_CHANNELS;

	/* Under AFH, CLK1 is masked off above, so y1 and y2 are zero and both
	 * terms below drop out. They are kept so that this reads line for line
	 * like next_hop() in the Ubertooth firmware. */
	perm = fast_perm(
		((x + a) % 32) ^ pn->b,
		(y1 * 0x1f) ^ c,
		d);
	/* hop selection */
	next_channel = pn->bank[(perm + pn->e + f + y2) % BT_NUM_CHANNELS];
	/* AFH re-mapping (2.6.3.1): a used basic channel is kept unchanged; an
	 * unused one is replaced by entry k' = (PERM5 + E + F' + Y2) mod N of
	 * the AFH mapping table, where F' = 16 * CLK27-7 mod N. */
	if(afh && !btbb_piconet_get_channel_seen(pn, next_channel)) {
		f_dash = base_f % n_used;
		next_channel = pn->afh_bank[(perm + pn->e + f_dash + y2) % n_used];
	}
	return next_channel;
}

/* The channel a CLK1-27 value hops to, which is the channel a packet sent at
 * that clock is observed on. */
static char observed_channel(uint32_t clk1_27, btbb_piconet *pn)
{
	/* single_hop() takes CLKN, which is CLK1-27 shifted up by one. */
	return single_hop((int) (clk1_27 << 1), pn);
}

/*
 * A clock as the offset between the piconet's clock and the observer's, in
 * 625 us slots: the form a clock is remembered in once it has been given up.
 * A CLK1-27 candidate value cannot be remembered as it stands, because it is
 * the piconet's clock at the first packet of one discovery attempt and the
 * next attempt has a different first packet.  The observer's own clock runs
 * on through both, so the difference identifies the same clock either side of
 * a restart.  Both clocks count slots and both wrap at SEQUENCE_LENGTH, so
 * the subtraction is taken modulo that.
 */
static uint32_t clock_as_offset(const btbb_piconet *pn, uint32_t clk1_27)
{
	return (clk1_27 - pn->first_pkt_time) & (SEQUENCE_LENGTH - 1);
}

/* The clock being followed, in that same form.  It is read back out of the
 * CLKN-tick offset the caller was given, which is twice the slot offset. */
static uint32_t followed_offset(const btbb_piconet *pn)
{
	return ((uint32_t) pn->clk_offset >> 1) & (SEQUENCE_LENGTH - 1);
}

/* Was this clock given up under the UAP the piconet is searching with now?
 * A rejection made under another UAP describes hops this search does not
 * predict, so it is not held against it. */
static int clock_rejected(const btbb_piconet *pn, uint32_t offset)
{
	int i;

	for (i = 0; i < pn->num_rejected; i++) {
		if (pn->rejected_offsets[i] == offset &&
		    pn->rejected_uaps[i] == pn->UAP)
			return 1;
	}
	return 0;
}

/* Remember a clock as one not to accept again for this LAP and UAP.  The
 * oldest is dropped once the list is full: the newest rejection is the one a
 * restart would otherwise choose again. */
static void reject_clock(btbb_piconet *pn, uint32_t offset)
{
	int i;

	if (clock_rejected(pn, offset))
		return;
	if (pn->num_rejected == MAX_REJECTED_CLOCKS) {
		for (i = 1; i < MAX_REJECTED_CLOCKS; i++) {
			pn->rejected_offsets[i - 1] = pn->rejected_offsets[i];
			pn->rejected_uaps[i - 1] = pn->rejected_uaps[i];
		}
		pn->num_rejected--;
	}
	pn->rejected_uaps[pn->num_rejected] = pn->UAP;
	pn->rejected_offsets[pn->num_rejected++] = offset;
}

/*
 * How many entries the candidate list starts with, and the least it grows by.
 * 16384 entries is 64 kB. The list reaches about 26500 entries for the basic
 * hopping sequence and about 105000 for the fewest channels AFH is defined
 * for, so this is a few doublings short of either.
 */
#define CANDIDATE_BLOCK 16384

/*
 * Walk the CLK1-27 values whose low 6 bits are already known and collect
 * those that hop to the channel the first packet arrived on, into a list the
 * caller owns and frees. Returns the number of candidates found, or -1 if the
 * list could not be allocated, in which case *candidates is NULL.
 *
 * The walk is 2^21 clock values, and each one's hop is computed from the
 * piconet's current AFH map rather than read from a table, which makes this
 * the expensive part of starting hop reversal: tens of milliseconds, inside
 * the packet callback of whatever is capturing. So it happens once. The list
 * grows as matches are found, rather than being counted in a walk of its own,
 * and is handed back holding exactly the candidates found. The hop sequence
 * does not visit the channels evenly, and under AFH it only ever visits the
 * used ones, so there is no useful size to allocate up front.
 *
 * Growing before each write is what keeps the candidates inside the list
 * however many of them there turn out to be. The two-pass version instead
 * trusted its first pass to have counted them, and sizing that list from an
 * even spread over the channels is what overflowed it under AFH.
 */
static int init_candidates(char channel, int known_clock_bits,
			   btbb_piconet *pn, uint32_t **candidates)
{
	uint32_t clk1_27;
	uint32_t *list = NULL;
	uint32_t *grown;
	int count = 0;	/* candidates found so far */
	int room = 0;	/* entries the list has space for */

	*candidates = NULL;

	/* only try clock values that match our known bits */
	for (clk1_27 = (uint32_t) known_clock_bits; clk1_27 < SEQUENCE_LENGTH;
	     clk1_27 += 0x40) {
		if (observed_channel(clk1_27, pn) != channel)
			continue;
		/* A clock already followed and given up is not a candidate
		 * again, so discovery starting over cannot end where it just
		 * came from. */
		if (clock_rejected(pn, clock_as_offset(pn, clk1_27)))
			continue;
		if (count == room) {
			/* Double the list each time, so the copying costs a
			 * constant amount per candidate however long it
			 * turns out to be. */
			room = room ? room * 2 : CANDIDATE_BLOCK;
			grown = (uint32_t *) malloc(sizeof(uint32_t) *
						    (size_t) room);
			if (grown == NULL) {
				free(list);
				return -1;
			}
			/* there is nothing to copy into the first block, and
			 * memcpy() is not defined for a null source even
			 * with a length of 0 */
			if (count > 0)
				memcpy(grown, list,
				       sizeof(uint32_t) * (size_t) count);
			free(list);
			list = grown;
		}
		list[count++] = clk1_27;
	}

	if (count < room) {
		/* Hand back a list of exactly the candidates found, as the
		 * counting pass used to give. A larger one would be correct,
		 * so a failure here is not worth losing the candidates
		 * over. */
		grown = (uint32_t *) malloc(sizeof(uint32_t) * (size_t) count);
		if (grown != NULL) {
			memcpy(grown, list,
			       sizeof(uint32_t) * (size_t) count);
			free(list);
			list = grown;
		}
	}

	*candidates = list;
	return count;
}

/* Is hop selection adapted to the piconet's AFH map? Below Nmin the sequence
 * is the basic one whatever the map says. */
static int afh_in_use(btbb_piconet *pn)
{
	return btbb_piconet_get_flag(pn, BTBB_IS_AFH) &&
		pn->used_channels >= AFH_MIN_USED_CHANNELS;
}

/* The AFH map as hop selection reads it: bit 79 is reserved, there being no
 * channel 79, so two maps differing only there are the same map. */
static void channel_map(const btbb_piconet *pn, uint8_t *out)
{
	memcpy(out, pn->afh_map, sizeof(pn->afh_map));
	out[9] &= 0x7f;
}

/* initialize the hop reversal process */
int btbb_init_hop_reversal(int aliased, btbb_piconet *pn)
{
	uint32_t address;
	uint32_t clock;
	int count;
	int restarted = btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT);

	/*
	 * An aliased receiver observes the 79 channels folded into 25, so the
	 * candidates would have to be chosen and winnowed against folded
	 * channels. That is not implemented, and asking for it used to be
	 * accepted and then quietly reversed as if unaliased, which chooses
	 * the candidates against channels the receiver never reports.
	 *
	 * A refused request changes nothing, so a piconet already recovering
	 * its clock the ordinary way carries on doing that.
	 */
	if (aliased) {
		piconet_note("aliased hop reversal is not implemented\n");
		return 0;
	}

	/* A second call replaces the candidates of the first. */
	free(pn->clock_candidates);
	pn->clock_candidates = NULL;

	/*
	 * Hops are predicted one at a time from the piconet's current state
	 * rather than read from a precomputed sequence, so that a map still
	 * being learned is followed as it grows.
	 */
	precalc(pn);
	address = (((uint32_t) pn->UAP << 24) | pn->LAP) & 0xfffffff;
	address_precalc((int) address, pn);

	/* Every bit of state the candidates below are chosen under, set before
	 * they are, so an allocation failure cannot leave half of it. */
	pn->num_candidates = 0;
	pn->winnowed = 0;
	pn->clk27_confirmations = 0;
	pn->candidates_afh = (uint8_t) afh_in_use(pn);
	channel_map(pn, pn->candidates_afh_map);
	btbb_piconet_set_flag(pn, BTBB_HOP_REVERSAL_INIT, 1);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 0);
	/* the hops being reversed are the piconet's own, not an aliased
	 * receiver's view of them */
	btbb_piconet_set_flag(pn, BTBB_IS_ALIASED, 0);

	/* The piconet's CLK1-6 when the first packet arrived: the observer's
	 * own CLK1-27 at that moment, whose low 6 bits are its CLK1-6, plus
	 * the offset between the two clocks in the same 625 us slots. Every
	 * candidate CLK1-27 below has these low bits. */
	clock = (pn->clk6_offset + pn->first_pkt_time) & 0x3f;
	count = init_candidates(pn->pattern_channels[0], (int) clock, pn,
				&pn->clock_candidates);
	/* -1 is a list that could not be allocated, which leaves the piconet
	 * with no candidates, as a walk that found none does. */
	if (count > 0)
		pn->num_candidates = count;

	if (restarted)
		piconet_note("%d CLK1-27 candidates for the new AFH map\n",
			     pn->num_candidates);
	else
		piconet_note("%d initial CLK1-27 candidates\n",
			     pn->num_candidates);

	return pn->num_candidates;
}

void try_hop(btbb_packet *pkt, btbb_piconet *pn)
{
	uint8_t filter_uap = pn->UAP;

	/* Decode packet - fixing clock drift in the process */
	btbb_decode(pkt);

	if (btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
		//pn->winnowed = 0;
		/* The remembered pattern is a fixed-size array, as it is in
		 * btbb_uap_from_header(): start over rather than write past
		 * the end of it. */
		if (pn->packets_observed >= MAX_PATTERN_LENGTH) {
			piconet_note("Oops. More hops than we can remember.\n");
			reset(pn);
			return;
		}
		/* Both clocks are CLK1-27, so the difference is a count of
		 * 625 us slots, which is what channel_winnow() adds to a
		 * CLK1-27 candidate. */
		pn->pattern_indices[pn->packets_observed] =
			pkt->clkn - pn->first_pkt_time;
		pn->pattern_channels[pn->packets_observed] = pkt->channel;
		pn->packets_observed++;
		pn->total_packets_observed++;
		btbb_winnow(pn);
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
			piconet_note("got CLK1-27\n");
			piconet_note("clock offset = %d.\n", pn->clk_offset);
		}
	} else {
		if (btbb_piconet_get_flag(pn, BTBB_CLK6_VALID)) {
			btbb_uap_from_header(pkt, pn);
			if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
				piconet_note("got CLK1-27\n");
				piconet_note("clock offset = %d.\n", pn->clk_offset);
			}
		} else {
			if (btbb_uap_from_header(pkt, pn)) {
				if (filter_uap == pn->UAP) {
					btbb_init_hop_reversal(0, pn);
					btbb_winnow(pn);
				} else {
					piconet_note("failed to confirm UAP\n");
				}
			}
		}
	}

	if(!btbb_piconet_get_flag(pn, BTBB_UAP_VALID)) {
		btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 1);
		pn->UAP = filter_uap;
	}
}

/* reset UAP/clock discovery */
static void reset(btbb_piconet *pn)
{
	//printf("no candidates remaining! starting over . . .\n");

	if(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
		free(pn->clock_candidates);
		pn->clock_candidates = NULL;
	}
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 0);
	btbb_piconet_set_flag(pn, BTBB_HOP_REVERSAL_INIT, 0);
	btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 0);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 0);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 0);
	/* Discovery starting over means there is no clock to follow and
	 * nothing recovered to give up. */
	btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 0);
	pn->clock_recovered = 0;
	pn->follow_header_failures = 0;
	pn->lock_worked = 0;
	pn->packets_observed = 0;
	/* The candidate list has just been freed, so nothing may still read a
	 * count of it, and the offset it produced is no longer a clock the
	 * caller can hop on: btbb_piconet_get_clk_offset() reads 0 until
	 * BTBB_CLK27_VALID is set again. */
	pn->num_candidates = 0;
	pn->winnowed = 0;
	pn->clk27_confirmations = 0;
	pn->clk_offset = 0;
	/* The clocks given up for this LAP are the one thing a reset keeps,
	 * because keeping them is what stops the new search from ending where
	 * the last one did.  Only a new LAP clears them. */

	/*
	 * If we have recently observed two packets in a row on the same
	 * channel, try AFH next time.  If not, don't.
	 */
	btbb_piconet_set_flag(pn, BTBB_IS_AFH,
			      btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH));
	// btbb_piconet_set_flag(pn, BTBB_LOOKS_LIKE_AFH, 0);
	//int i;
	//for(i=0; i<10; i++)
	//	pn->afh_map[i] = 0;
}

/* narrow a list of candidate clock values based on a single observed hop */
static int channel_winnow(int offset, char channel, btbb_piconet *pn)
{
	int i;
	int new_count = 0; /* number of candidates after winnowing */
	uint32_t clk1_27;

	/* check every candidate */
	for (i = 0; i < pn->num_candidates; i++) {
		clk1_27 = (pn->clock_candidates[i] + (uint32_t) offset) % SEQUENCE_LENGTH;
		if (observed_channel(clk1_27, pn) == channel) {
			/* this candidate matches the latest hop */
			/* blow away old list of candidates with new one */
			/* safe because new_count can never be greater than i */
			pn->clock_candidates[new_count++] = pn->clock_candidates[i];
		}
	}
	pn->num_candidates = new_count;

	if (new_count == 1) {
		/* The hop that narrowed the list to one is the comparison
		 * that chose it, so it does not also confirm it. */
		pn->clk27_confirmations++;
		if (pn->clk27_confirmations > CLK27_CONFIRM_HOPS) {
			/* In CLKN ticks, which is what clk_offset carries, so
			 * shift both CLK1-27 values up by one. */
			pn->clk_offset = (int) ((pn->clock_candidates[0] << 1) -
						(pn->first_pkt_time << 1));
			/* The leading newline this line used to carry broke it
			 * out of the dump it landed in the middle of; on
			 * stderr it is a blank line and nothing more. */
			piconet_note("Acquired CLK1-27 = 0x%07x\n",
				     pn->clock_candidates[0]);
			btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 1);
			/* The library's own, so the library may give it up
			 * again if following it does not work out. */
			pn->clock_recovered = 1;
			pn->follow_header_failures = 0;
			/* nothing has decoded under this clock yet */
			pn->lock_worked = 0;
		}
	}
	else if (new_count == 0) {
		reset(pn);
	}
	//else {
	//printf("%d CLK1-27 candidates remaining (channel=%d)\n", new_count, channel);
	//}

	return new_count;
}

/* Would the piconet's hops now be predicted from a different sequence than the
 * candidates were chosen from? */
static int candidates_superseded(btbb_piconet *pn)
{
	uint8_t map[10];

	if (pn->candidates_afh != (uint8_t) afh_in_use(pn))
		return 1;
	if (!pn->candidates_afh)
		return 0;	/* the basic sequence, whatever the map says */
	channel_map(pn, map);
	return memcmp(pn->candidates_afh_map, map, sizeof(map)) != 0;
}

/* narrow a list of candidate clock values based on all observed hops */
int btbb_winnow(btbb_piconet *pn)
{
	int new_count = pn->num_candidates;
	int index, last_index;
	uint8_t channel, last_channel;

	/*
	 * The clock is known, so hops from here on are the piconet's to
	 * follow, not candidates to narrow. Without this the hop that the
	 * clock was accepted on would be compared again and the clock
	 * announced a second time.
	 */
	if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
		return new_count;

	/*
	 * The AFH map has changed since the candidates were chosen, so they
	 * were chosen from a hop sequence this piconet no longer follows, and
	 * the true clock may be among the ones already dropped. Choose them
	 * again from the map as it stands and re-winnow every hop remembered
	 * so far, rather than carry on narrowing a list built from a map that
	 * has been superseded.
	 *
	 * Which map it is decides this, not how many channels it marks used:
	 * two maps with the same count give different sequences. A map that
	 * hop selection is not adapted to does not come into it at all, so a
	 * channel seen for the first time while hopping basically costs
	 * nothing.
	 */
	if (btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT) &&
	    !btbb_piconet_get_flag(pn, BTBB_CLK27_VALID) &&
	    candidates_superseded(pn)) {
		new_count = btbb_init_hop_reversal(0, pn);
		if (new_count == 0) {
			reset(pn);
			return 0;
		}
	}

	for (; pn->winnowed < pn->packets_observed; pn->winnowed++) {
		index = pn->pattern_indices[pn->winnowed];
		channel = pn->pattern_channels[pn->winnowed];
		new_count = channel_winnow(index, channel, pn);
		if (new_count == 0)
			break;	/* reset() has started discovery over */
		/* one candidate, confirmed by enough later hops to be the
		 * clock: there is nothing left to narrow */
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID))
			break;

		/* There is no previous hop to compare the first one with. */
		if (pn->winnowed > 0) {
			last_index = pn->pattern_indices[pn->winnowed - 1];
			last_channel = pn->pattern_channels[pn->winnowed - 1];
			/*
			 * Two packets in a row on the same channel should only
			 * happen if adaptive frequency hopping is in use.
			 * There can be false positives, though, especially if
			 * there is aliasing.
			 */
			if (!btbb_piconet_get_flag(pn, BTBB_LOOKS_LIKE_AFH)
			    && (index == last_index + 1)
			    && (channel == last_channel)) {
				btbb_piconet_set_flag(pn, BTBB_LOOKS_LIKE_AFH, 1);
				piconet_note("Hopping pattern appears to be AFH\n");
			}
		}
	}

	return new_count;
}

/* use packet headers to determine UAP */
int btbb_uap_from_header(btbb_packet *pkt, btbb_piconet *pn)
{
	uint8_t UAP;
	int count, crc_chk, first_clock = 0;

	int remaining = 0;
	/* The packet's clock as CLK1-27, the 625 us slot count. Its name on
	 * the packet is clkn, but btbb_packet_set_data() shifts the 312.5 us
	 * value it is given down by one, and every clock below is in slots:
	 * first_pkt_time, the differences remembered in pattern_indices, and
	 * the CLK1-6 the low 6 bits give. */
	uint32_t clk1_27 = pkt->clkn;

	if (!btbb_piconet_get_flag(pn, BTBB_GOT_FIRST_PACKET))
		pn->first_pkt_time = clk1_27;

	// Set afh channel map
	btbb_piconet_set_channel_seen(pn, pkt->channel);

	if (pn->packets_observed < MAX_PATTERN_LENGTH) {
		pn->pattern_indices[pn->packets_observed] =
			clk1_27 - pn->first_pkt_time;
		pn->pattern_channels[pn->packets_observed] = pkt->channel;
	} else {
		piconet_note("Oops. More hops than we can remember.\n");
		reset(pn);
		return 0; //FIXME ought to throw exception
	}
	pn->packets_observed++;
	pn->total_packets_observed++;

	/* try every possible first packet clock value */
	for (count = 0; count < 64; count++) {
		/* skip eliminated candidates unless this is our first time through */
		if (pn->clock6_candidates[count] > -1
			|| !btbb_piconet_get_flag(pn, BTBB_GOT_FIRST_PACKET)) {
			/* clock value for the current packet assuming count was the clock of the first packet */
			int clock = (count + clk1_27 - pn->first_pkt_time) % 64;
			UAP = try_clock(clock, pkt);
			crc_chk = -1;

			/* The address part this clock reads out of the header
			 * is not the one this piconet is known to have, so
			 * this clock is not the piconet's and the packet is
			 * not its traffic. Rule the candidate out without
			 * decoding it: a trial run here would be thrown away
			 * whatever it found, and its payload and CRC answer
			 * would be left on the packet for a capture file to
			 * report. */
			if (btbb_piconet_get_flag(pn, BTBB_UAP_VALID)
				&& UAP != pn->UAP)
				crc_chk = -1;
			/* if this is the first packet: populate the candidate list */
			/* if not: check CRCs if UAPs match */
			else if (!btbb_piconet_get_flag(pn, BTBB_GOT_FIRST_PACKET)
				|| UAP == pn->clock6_candidates[count])
				crc_chk = crc_check(clock, pkt);

			switch(crc_chk) {
			case -1: /* UAP mismatch */
			case 0: /* CRC failure */
				pn->clock6_candidates[count] = -1;
				break;

			case 1: /* inconclusive result */
			case 2: /* Inconclusive, but looks better */
				pn->clock6_candidates[count] = UAP;
				/* remember this count because it may be the correct clock of the first packet */
				first_clock = count;
				remaining++;
				break;

			default: /* CRC success */
				pn->clk6_offset = (count - (pn->first_pkt_time & 0x3f)) & 0x3f;
				if (!btbb_piconet_get_flag(pn, BTBB_UAP_VALID))
					piconet_note("Correct CRC! UAP = 0x%x found after %d total packets.\n",
						     UAP, pn->total_packets_observed);
				else
					piconet_note("Correct CRC! CLK6 = 0x%x found after %d total packets.\n",
						     pn->clk6_offset, pn->total_packets_observed);
				pn->UAP = UAP;
				btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
				btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 1);
				pn->total_packets_observed = 0;
				return 1;
			}
		}
	}

	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 1);

	if (remaining == 1) {
		pn->clk6_offset = (first_clock - (pn->first_pkt_time & 0x3f)) & 0x3f;
		if (!btbb_piconet_get_flag(pn, BTBB_UAP_VALID))
			piconet_note("UAP = 0x%x found after %d total packets.\n",
				     pn->clock6_candidates[first_clock],
				     pn->total_packets_observed);
		else
			piconet_note("CLK6 = 0x%x found after %d total packets.\n",
				     pn->clk6_offset,
				     pn->total_packets_observed);
		pn->UAP = pn->clock6_candidates[first_clock];
		btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 1);
		btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 1);
		pn->total_packets_observed = 0;
		return 1;
	}

	if (remaining == 0) {
		reset(pn);
	}

	return 0;
}

/* FIXME: comment out enqueue and dequeue because they are
 * never used.  Try to find out what tey were meant to be
 * used for before the next release.
 */
///* add a packet to the queue */
//static void enqueue(btbb_packet *pkt, btbb_piconet *pn)
//{
//	pkt_queue *head;
//	//pkt_queue item;
//
//	btbb_packet_ref(pkt);
//	pkt_queue item = {pkt, NULL};
//	head = pn->queue;
//
//	if (head == NULL) {
//		pn->queue = &item;
//	} else {
//		for(; head->next != NULL; head = head->next)
//		  ;
//		head->next = &item;
//	}
//}
//
///* pull the first packet from the queue (FIFO) */
//static btbb_packet *dequeue(btbb_piconet *pn)
//{
//	btbb_packet *pkt;
//
//	if (pn->queue == NULL) {
//		pkt = NULL;
//	} else {
//		pkt = pn->queue->pkt;
//		pn->queue = pn->queue->next;
//		btbb_packet_unref(pkt);
//	}
//
//	return pkt;
//}

/* Print AFH map from observed packets */
void btbb_print_afh_map(btbb_piconet *pn) {
	uint8_t *afh_map;
	afh_map = pn->afh_map;

	/* Print like hcitool does */
	printf("AFH map: 0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	       afh_map[0], afh_map[1], afh_map[2], afh_map[3], afh_map[4],
	       afh_map[5], afh_map[6], afh_map[7], afh_map[8], afh_map[9]);

	// /* Printed ch78 -> ch0 */
	// printf("\tAFH Map=0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	//        afh_map[9], afh_map[8], afh_map[7], afh_map[6], afh_map[5],
	//        afh_map[4], afh_map[3], afh_map[2], afh_map[1], afh_map[0]);
}

/* Container for survey piconets */
typedef struct {
    uint32_t key; /* LAP */
    btbb_piconet *pn;
    UT_hash_handle hh;
} survey_hash;

static survey_hash *piconet_survey = NULL;

/* The survey's piconet for a LAP, started if the survey has not heard that LAP
 * before. NULL where there is no room for one. Not declared in btbb.h:
 * btbb_survey_piconet() is the public way in. */
btbb_piconet *get_piconet(uint32_t lap)
{
	survey_hash *s;
	btbb_piconet *pn;
	HASH_FIND(hh, piconet_survey, &lap, 4, s);

	if (s == NULL) {
		s = malloc(sizeof(survey_hash));
		if (s == NULL) {
			piconet_note("error: could not allocate a survey "
				     "entry\n");
			return NULL;
		}
		pn = btbb_piconet_new();
		if (pn == NULL) {
			/* nothing goes into the survey without a piconet: an
			 * entry holding none would answer for that LAP from
			 * then on */
			free(s);
			return NULL;
		}
		btbb_init_piconet(pn, lap);

		s->key = lap;
		s->pn = pn;
		HASH_ADD(hh, piconet_survey, key, 4, s);
	} else {
		pn = s->pn;
	}
	return pn;
}

/* The survey's own piconet for a LAP, left where it is */
btbb_piconet *btbb_survey_piconet(uint32_t lap)
{
	survey_hash *s;

	HASH_FIND(hh, piconet_survey, &lap, 4, s);
	return (s != NULL) ? s->pn : NULL;
}

/* Destructively iterate over survey results */
btbb_piconet *btbb_next_survey_result() {
	btbb_piconet *pn = NULL;
	survey_hash *tmp;

	if (piconet_survey != NULL) {
		tmp = piconet_survey;
		pn = tmp->pn;
		/* Out of the hash table, not just off the front of the list.
		 * The entry used to be freed while the table's buckets still
		 * pointed at it, so a later lookup of that LAP - which is
		 * what happens when a survey carries on after some of its
		 * results have been read - walked freed memory and handed
		 * back a piconet that had been released. */
		HASH_DEL(piconet_survey, tmp);
		free(tmp);
	}
	return pn;
}

/* How long the current run of failing headers has been going, in 625 us
 * slots.  Both values are clocks the packets carried, so whichever clock the
 * caller's receiver counts, their difference is elapsed slots; it wraps at
 * SEQUENCE_LENGTH, so the subtraction is taken modulo that. */
static uint32_t failure_run_slots(const btbb_piconet *pn, uint32_t clkn)
{
	uint32_t slots = (clkn - pn->first_failure_time) &
			 (SEQUENCE_LENGTH - 1);

	/* A run that reads as more than half the clock's range has not been
	 * going for eleven hours: the packet clock went backwards, which a
	 * caller handing over a capture out of order can do. Nothing has
	 * elapsed as far as the time bound is concerned, so it takes the full
	 * count of failures. */
	if (slots > SEQUENCE_LENGTH / 2)
		return 0;
	return slots;
}

/*
 * Has the run of failing headers said enough to give the clock up?  Either it
 * holds the full count of failures, or half of them where the run has already
 * been failing for FOLLOW_FAILURE_SLOTS.  The second is what a slow link
 * costs: it still takes a packet to answer on, but ten of them rather than
 * twenty.
 */
static int follow_run_is_enough(const btbb_piconet *pn, uint32_t clkn)
{
	if (pn->follow_header_failures >= FOLLOW_HEADER_FAILURES)
		return 1;
	if (pn->follow_header_failures < FOLLOW_FAILURES_IN_TIME)
		return 0;
	return failure_run_slots(pn, clkn) >= FOLLOW_FAILURE_SLOTS;
}

/*
 * Give up the clock being followed and start discovery again: rule the clock
 * out where it never decoded anything, count the loss, and drop the UAP that
 * went with it.  Both the library's own decision and the caller's end here,
 * so a clock given up either way is remembered the same way, as the offset
 * between the two clocks under the UAP it was followed with.  Whether this
 * clock is one to rule out is the caller's to say, because it depends on what
 * the evidence was.
 */
static void drop_followed_clock(btbb_piconet *pn, int rule_it_out)
{
	if (rule_it_out)
		reject_clock(pn, followed_offset(pn));

	pn->lock_losses++;
	if (pn->lock_losses >= LOCK_LOSS_TRIES && !pn->lock_loss_reported) {
		/* Said once.  The search carries on either way, each attempt
		 * ruling out one more clock, but a piconet that has ended or
		 * left range would otherwise have the library reporting the
		 * same loss for as long as the capture runs. */
		piconet_note("note: %d clocks given up for this piconet; it may have ended "
			     "or left range. Still listening, and not saying so again "
			     "until a clock works\n", pn->lock_losses);
		pn->lock_loss_reported = 1;
	}

	/* The UAP goes with the clock. Discovery cannot find another while
	 * try_hop() is still filtering on this one, which it does for as long
	 * as the piconet has one. */
	pn->UAP = 0;
	reset(pn);
}

/*
 * Give up the clock being followed because header after header has failed its
 * HEC, which is what btbb_process_packet() does once the failures have said
 * enough.
 */
static int give_up_clock(btbb_piconet *pn, uint32_t clkn)
{
	/* A slot is 625 us, so five eighths of a millisecond. */
	unsigned int ms = (unsigned int) (failure_run_slots(pn, clkn) * 5 / 8);

	if (!pn->lock_loss_reported)
		piconet_note("note: %d packet headers in a row failed their HEC over %u ms; "
			     "giving up this clock and UAP and starting discovery again\n",
			     pn->follow_header_failures, ms);

	/*
	 * A clock that never decoded one header was never the piconet's, so
	 * discovery is not to choose it again.  A clock that did work and
	 * then stopped is given up but not ruled out: it may still be the
	 * piconet's, on a link that has changed or gone quiet, and ruling it
	 * out would keep the library from ever locking it again.
	 */
	drop_followed_clock(pn, !pn->lock_worked);
	return BTBB_PROCESS_LOCK_LOST;
}

/*
 * Give up the clock at the caller's request.  Failing headers are evidence
 * about CLK1-6 and the UAP; a wrong CLK1-27 fails differently, because every
 * candidate carries the CLK1-6 that discovery confirmed, so it dewhitens and
 * checks out whatever it does receive.  What it gets wrong is the channel to
 * listen on, and the symptom is silence: measured over a sweep of clock
 * values, a receiver following a wrong one lands on the piconet's channel in
 * about one slot pair in eighteen.
 *
 * The library cannot see that on its own.  It is called per packet and keeps
 * no timer, so a clock that hears next to nothing leaves next to nothing to
 * count, and the rate alone does not tell it apart from a quiet link.  The
 * caller knows how long it has been following in wall-clock time and that it
 * put its receiver on the offset the library gave it, so the caller is what
 * judges the silence; this is how it hands the clock back.
 */
int btbb_piconet_drop_clock(btbb_piconet *pn)
{
	/*
	 * Only a clock the library recovered is the library's to give up.  A
	 * caller that set the clock itself, from an adapter say, owns it:
	 * there is nothing better to fall back on, so the library neither
	 * takes it away nor rules the offset out.  The same test answers for
	 * a piconet that has no clock yet, where there is nothing to drop.
	 */
	if (pn == NULL || !pn->clock_recovered)
		return 0;

	/*
	 * This clock is ruled out whether or not it decoded a header, which
	 * is where the caller's evidence differs from the library's.  A clock
	 * given up on failing headers may be the piconet's on a link that
	 * went bad, so one that decoded something is kept available.  A clock
	 * that goes quiet cannot be: the few packets a wrong CLK1-27 does
	 * hear all dewhiten and check out, so having decoded a header says
	 * nothing here, and silence over a stretch of time is evidence about
	 * which channel the receiver sat on, which is what CLK1-27 decides.
	 * Not ruling it out would leave discovery free to return to it, which
	 * is the loop this whole mechanism exists to break.
	 *
	 * What it costs is a piconet whose link went idle under a clock that
	 * was right: that clock is ruled out too, and it is a candidate again
	 * only once eight further clocks have been given up or the LAP
	 * changes, since the list is eight entries and drops the oldest.  So
	 * the threshold belongs to the caller, which should set it long enough
	 * that giving the clock up is what its user wants.
	 *
	 * The caller asked for this and warns its own user, so nothing is
	 * printed here beyond the note that several clocks have now been
	 * given up for this piconet.
	 */
	drop_followed_clock(pn, 1);
	return 1;
}

int btbb_process_packet(btbb_packet *pkt, btbb_piconet *pn) {
	if (survey_mode) {
		pn = get_piconet(btbb_packet_get_lap(pkt));
		/* out of memory: the packet is dropped rather than written
		 * through a piconet that was never made */
		if (pn == NULL)
			return 0;
		btbb_piconet_set_channel_seen(pn, pkt->channel);
		if(btbb_header_present(pkt) && !btbb_piconet_get_flag(pn, BTBB_UAP_VALID))
			btbb_uap_from_header(pkt, pn);
		return 0;
	}

	if(pn)
		btbb_piconet_set_channel_seen(pn, pkt->channel);

	/* If piconet structure is given, a LAP is given, and packet
	 * header is readable, do further analysis. If UAP has not yet
	 * been determined, attempt to calculate it from headers. Once
	 * UAP is known, try to determine clk6 and clk27. Once clocks
	 * are known, follow the piconet. */
	if (pn && btbb_piconet_get_flag(pn, BTBB_LAP_VALID) &&
	    btbb_header_present(pkt)) {

		/* Have LAP/UAP/clocks, now hopping along with the piconet. */
		if (btbb_piconet_get_flag(pn, BTBB_FOLLOWING)) {
			btbb_packet_set_uap(pkt, btbb_piconet_get_uap(pn));
			btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
			btbb_packet_set_flag(pkt, BTBB_CLK27_VALID, 1);

			if(btbb_decode(pkt))
				btbb_print_packet(pkt);
			else
				piconet_note("Failed to decode packet\n");

			/*
			 * The header's HEC is the only thing here that says
			 * anything about the clock: it is checked against the
			 * piconet's UAP, over a header dewhitened with CLK1-6,
			 * so a header that checks out confirms both whatever
			 * the payload goes on to do. A payload that will not
			 * decode is a packet lost to bit errors and no
			 * evidence at all, and a header the rate 1/3 code
			 * could not recover is none either.
			 */
			if (btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED)) {
				if (btbb_packet_get_flag(pkt, BTBB_HEC_VALID)) {
					pn->follow_header_failures = 0;
					/* This clock has decoded a header, so
					 * it was the piconet's at least once.
					 * Recovery is working for this
					 * piconet, so the losses that came
					 * before are history: a loss after
					 * this one is worth reporting again. */
					if (pn->clock_recovered) {
						pn->lock_worked = 1;
						pn->lock_losses = 0;
						pn->lock_loss_reported = 0;
					}
				} else if (pn->clock_recovered) {
					if (pn->follow_header_failures == 0)
						pn->first_failure_time =
							pkt->clkn;
					pn->follow_header_failures++;
					/* Header after header of this piconet
					 * failing under the clock and UAP
					 * being followed means they are not
					 * the piconet's. Nothing winnows
					 * while following, so start discovery
					 * over and tell the caller: it put
					 * the receiver on the offset the
					 * library gave it, and has to take it
					 * off again. */
					if (follow_run_is_enough(pn,
								 pkt->clkn))
						return give_up_clock(pn,
								     pkt->clkn);
				}
			}
		}

		/* Have LAP/UAP, need clocks. */
		else if (btbb_piconet_get_uap(pn)) {
			try_hop(pkt, pn);
			if (btbb_piconet_get_flag(pn, BTBB_CLK6_VALID) &&
			    btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
				btbb_piconet_set_flag(pn, BTBB_FOLLOWING, 1);
				return -1;
			}
		}

		/* Have LAP, need UAP. */
		else {
			btbb_uap_from_header(pkt, pn);
		}
	}
	return 0;
}
