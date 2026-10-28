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
#include "uthash.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int perm_table_initialized = 0;
char perm_table[0x20][0x20][0x200];

/* reset UAP/clock discovery */
static void reset(btbb_piconet *pn);

btbb_piconet *
btbb_piconet_new(void)
{
	btbb_piconet *pn = (btbb_piconet *)calloc(1, sizeof(btbb_piconet));
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
	pn->candidates_stale = 1;
	if(btbb_piconet_get_flag(pn, BTBB_UAP_VALID))
		get_hop_pattern(pn);
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
		pn->candidates_stale = 1;
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
		pn->candidates_stale = 1;
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

/* generate the complete hopping sequence */
static void gen_hops(btbb_piconet *pn)
{
	/* a, b, c, d, e, f, x, y1, y2 are variable names used in section 2.6 of the spec */
	/* b is already defined */
	/* e is already defined */
	int a, c, d, x;
	uint32_t base_f, f, f_dash;
	int h, i, j, k, c_flipped, perm_in, perm_out, chan;

	int n_used = pn->used_channels;
	/* Fewer used channels than AFH is defined for (or an unset map) falls
	 * back to basic hopping, which also avoids dividing by zero. */
	int afh = btbb_piconet_get_flag(pn, BTBB_IS_AFH) &&
			n_used >= AFH_MIN_USED_CHANNELS;

	/* sequence index = clock >> 1 */
	/* (hops only happen at every other clock value) */
	int index = 0;
	base_f = 0;
	f = 0;
	f_dash = 0;

	/* nested loops for optimization (not recalculating every variable with every clock tick) */
	for (h = 0; h < 0x04; h++) { /* clock bits 26-27 */
		for (i = 0; i < 0x20; i++) { /* clock bits 21-25 */
			a = pn->a1 ^ i;
			for (j = 0; j < 0x20; j++) { /* clock bits 16-20 */
				c = pn->c1 ^ j;
				c_flipped = c ^ 0x1f;
				for (k = 0; k < 0x200; k++) { /* clock bits 7-15 */
					d = pn->d1 ^ k;
					for (x = 0; x < 0x20; x++) { /* clock bits 2-6 */
						perm_in = ((x + a) % 32) ^ pn->b;

						/* y1 (clock bit 1) = 0, y2 = 0 */
						perm_out = fast_perm(perm_in, c, d);
						chan = pn->bank[(perm_out + pn->e + f) % BT_NUM_CHANNELS];
						/* AFH re-mapping (2.6.3.1): a used basic
						 * channel is kept unchanged; an unused one is
						 * replaced by entry k' of the AFH mapping
						 * table. */
						if (afh && !btbb_piconet_get_channel_seen(pn, chan))
							chan = pn->afh_bank[(perm_out + pn->e + f_dash) % n_used];
						pn->sequence[index] = chan;

						if (afh) {
							/* AFH same channel mechanism (2.6.1): the
							 * peripheral slot reuses the channel of the
							 * central slot before it, i.e. CLK1 = 0. */
							pn->sequence[index + 1] = chan;
						} else {
							/* y1 (clock bit 1) = 1, y2 = 32 */
							perm_out = fast_perm(perm_in, c_flipped, d);
							pn->sequence[index + 1] = pn->bank[(perm_out + pn->e + f + 32) % BT_NUM_CHANNELS];
						}

						index += 2;
					}
					base_f += 16;
					f = base_f % BT_NUM_CHANNELS;
					/* F' = 16 * CLK27-7 mod N, taken from base_f and
					 * not from F, which is already reduced mod 79. */
					if (afh)
						f_dash = base_f % n_used;
				}
			}
		}
	}
}

/* Fill a sequence buffer this piconet already owns. */
static void fill_hop_pattern(btbb_piconet *pn, char *sequence)
{
	/* Building the sequence takes seconds, so say so, but on stderr:
	 * standard output carries only what the caller asked for, which for a
	 * tool streaming a capture there is the capture. */
	fprintf(stderr, "\nCalculating complete hopping sequence.\n");
	pn->sequence = sequence;

	precalc(pn);
	address_precalc(((pn->UAP<<24) | pn->LAP) & 0xfffffff, pn);
	gen_hops(pn);

	fprintf(stderr, "Hopping sequence calculated.\n");
}

/* Function to calculate piconet hopping patterns and add to hash map */
void gen_hop_pattern(btbb_piconet *pn)
{
	/* this holds the entire hopping sequence */
	fill_hop_pattern(pn, (char*) malloc(SEQUENCE_LENGTH));
}

/* Container for hopping pattern */
/*
 * Container for hopping pattern, one per address. A sequence is 128 MB, so
 * rather than keep one per map ever seen, the entry records what it was built
 * from and is rebuilt in place when that no longer describes the piconet.
 */
typedef struct {
    uint32_t address;	/* the key */
    uint8_t afh;	/* the sequence is adapted to afh_map */
    uint8_t afh_map[10];
    char *sequence;
    UT_hash_handle hh;
} hopping_struct;

static hopping_struct *hopping_map = NULL;

/* Function to fetch piconet hopping patterns */
void get_hop_pattern(btbb_piconet *pn)
{
	hopping_struct *s;
	uint32_t address;
	uint8_t afh;

	/* The map may have been written through btbb_piconet_get_afh_map(),
	 * which leaves the tables describing an older one. */
	precalc(pn);
	address = (((uint32_t)pn->UAP << 24) | pn->LAP) & 0xfffffff;
	/* Below Nmin the sequence is the basic one, whatever the map says. */
	afh = btbb_piconet_get_flag(pn, BTBB_IS_AFH) &&
		pn->used_channels >= AFH_MIN_USED_CHANNELS;

	HASH_FIND(hh, hopping_map, &address, sizeof(address), s);

	if (s == NULL) {
		gen_hop_pattern(pn);
		s = malloc(sizeof(hopping_struct));
		s->address = address;
		s->afh = afh;
		memcpy(s->afh_map, pn->afh_map, sizeof(s->afh_map));
		s->sequence = pn->sequence;
		HASH_ADD(hh, hopping_map, address, sizeof(address), s);
	} else if (s->afh != afh ||
		   (afh && memcmp(s->afh_map, pn->afh_map,
				  sizeof(s->afh_map)) != 0)) {
		/* Two maps marking the same channel count used still give
		 * different sequences, so the map itself decides this. */
		s->afh = afh;
		memcpy(s->afh_map, pn->afh_map, sizeof(s->afh_map));
		fill_hop_pattern(pn, s->sequence);
	} else {
		pn->sequence = s->sequence;
	}
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

/* look up channel for a particular hop */
char hop(int clock, btbb_piconet *pn)
{
	return pn->sequence[clock];
}

static char aliased_channel(char channel)
{
	return ((channel + 24) % ALIASED_CHANNELS) + 26;
}

/* The channel a CLK1-27 value hops to, as the receiver observes it. */
static char observed_channel(uint32_t clk1_27, btbb_piconet *pn)
{
	/* single_hop() takes CLKN, which is CLK1-27 shifted up by one. */
	char channel = single_hop((int) (clk1_27 << 1), pn);

	if (pn->aliased)
		return aliased_channel(channel);
	return channel;
}

/*
 * Walk the CLK1-27 values whose low 6 bits are already known and find those
 * that hop to the channel the first packet arrived on.
 *
 * With candidates NULL this only counts them, which is how the caller learns
 * how large the list has to be: the hop sequence does not visit the channels
 * evenly, and under AFH it only ever visits the used ones, so an estimate
 * from a channel count is not a bound. Writing is capped at max entries as
 * well, so a future miscount cannot run past the end of the list.
 */
static int init_candidates(char channel, int known_clock_bits, btbb_piconet *pn,
			   uint32_t *candidates, int max)
{
	uint32_t clk1_27;
	int count = 0; /* total number of candidates */

	/* only try clock values that match our known bits */
	for (clk1_27 = (uint32_t) known_clock_bits; clk1_27 < SEQUENCE_LENGTH;
	     clk1_27 += 0x40) {
		if (observed_channel(clk1_27, pn) == channel) {
			if (candidates != NULL) {
				if (count == max)
					break;
				candidates[count] = clk1_27;
			}
			count++;
		}
	}
	return count;
}

/* initialize the hop reversal process */
int btbb_init_hop_reversal(int aliased, btbb_piconet *pn)
{
	uint32_t address;
	uint32_t clock;
	int count;

	/* A second call replaces the candidates of the first. */
	if (btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
		free(pn->clock_candidates);
		pn->clock_candidates = NULL;
	}

	/*
	 * Hops are predicted one at a time from the piconet's current state
	 * rather than read from a precomputed sequence, so that a map still
	 * being learned is followed as it grows.
	 */
	precalc(pn);
	address = (((uint32_t) pn->UAP << 24) | pn->LAP) & 0xfffffff;
	address_precalc((int) address, pn);

	clock = (pn->clk6_offset + pn->first_pkt_time) & 0x3f;
	count = init_candidates(pn->pattern_channels[0], (int) clock, pn, NULL, 0);
	pn->num_candidates = 0;
	if (count > 0) {
		pn->clock_candidates = (uint32_t*)
			malloc(sizeof(uint32_t) * (size_t) count);
		if (pn->clock_candidates == NULL)
			return 0;
		pn->num_candidates = init_candidates(pn->pattern_channels[0],
						     (int) clock, pn,
						     pn->clock_candidates,
						     count);
	}
	pn->winnowed = 0;
	pn->candidates_stale = 0;
	btbb_piconet_set_flag(pn, BTBB_HOP_REVERSAL_INIT, 1);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 0);
	btbb_piconet_set_flag(pn, BTBB_IS_ALIASED, aliased);

	printf("%d initial CLK1-27 candidates\n", pn->num_candidates);

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
			printf("Oops. More hops than we can remember.\n");
			reset(pn);
			return;
		}
		pn->pattern_indices[pn->packets_observed] =
			pkt->clkn - pn->first_pkt_time;
		pn->pattern_channels[pn->packets_observed] = pkt->channel;
		pn->packets_observed++;
		pn->total_packets_observed++;
		btbb_winnow(pn);
		if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
			printf("got CLK1-27\n");
			printf("clock offset = %d.\n", pn->clk_offset);
		}
	} else {
		if (btbb_piconet_get_flag(pn, BTBB_CLK6_VALID)) {
			btbb_uap_from_header(pkt, pn);
			if (btbb_piconet_get_flag(pn, BTBB_CLK27_VALID)) {
				printf("got CLK1-27\n");
				printf("clock offset = %d.\n", pn->clk_offset);
			}
		} else {
			if (btbb_uap_from_header(pkt, pn)) {
				if (filter_uap == pn->UAP) {
					btbb_init_hop_reversal(0, pn);
					btbb_winnow(pn);
				} else {
					printf("failed to confirm UAP\n");
				}
			}
		}
	}

	if(!btbb_piconet_get_flag(pn, BTBB_UAP_VALID)) {
		btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 1);
		pn->UAP = filter_uap;
	}
}

/* return the observable channel (26-50) for a given channel (0-78) */
/* reset UAP/clock discovery */
static void reset(btbb_piconet *pn)
{
	//printf("no candidates remaining! starting over . . .\n");

	if(btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
		free(pn->clock_candidates);
		pn->clock_candidates = NULL;
		pn->sequence = NULL;
	}
	btbb_piconet_set_flag(pn, BTBB_GOT_FIRST_PACKET, 0);
	btbb_piconet_set_flag(pn, BTBB_HOP_REVERSAL_INIT, 0);
	btbb_piconet_set_flag(pn, BTBB_UAP_VALID, 0);
	btbb_piconet_set_flag(pn, BTBB_CLK6_VALID, 0);
	btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 0);
	pn->packets_observed = 0;

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
		/* In CLKN ticks, which is what clk_offset carries, so shift
		 * both CLK1-27 values up by one. */
		pn->clk_offset = (int) ((pn->clock_candidates[0] << 1) -
					(pn->first_pkt_time << 1));
		printf("\nAcquired CLK1-27 = 0x%07x\n", pn->clock_candidates[0]);
		btbb_piconet_set_flag(pn, BTBB_CLK27_VALID, 1);
	}
	else if (new_count == 0) {
		reset(pn);
	}
	//else {
	//printf("%d CLK1-27 candidates remaining (channel=%d)\n", new_count, channel);
	//}

	return new_count;
}

/* narrow a list of candidate clock values based on all observed hops */
int btbb_winnow(btbb_piconet *pn)
{
	int new_count = pn->num_candidates;
	int index, last_index;
	uint8_t channel, last_channel;

	/*
	 * The AFH map has grown since the candidates were chosen, so they were
	 * chosen from a hop sequence this piconet no longer follows, and the
	 * true clock may be among the ones already dropped. Choose them again
	 * from the map as it stands and re-winnow every hop remembered so
	 * far, rather than carry on narrowing a list built from a map that has
	 * been superseded.
	 */
	if (pn->candidates_stale &&
	    btbb_piconet_get_flag(pn, BTBB_HOP_REVERSAL_INIT)) {
		new_count = btbb_init_hop_reversal(
			btbb_piconet_get_flag(pn, BTBB_IS_ALIASED), pn);
		if (new_count == 0) {
			reset(pn);
			return 0;
		}
	}

	for (; pn->winnowed < pn->packets_observed; pn->winnowed++) {
		index = pn->pattern_indices[pn->winnowed];
		channel = pn->pattern_channels[pn->winnowed];
		new_count = channel_winnow(index, channel, pn);
		if (new_count <= 1)
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
				printf("Hopping pattern appears to be AFH\n");
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
	uint32_t clkn = pkt->clkn;

	if (!btbb_piconet_get_flag(pn, BTBB_GOT_FIRST_PACKET))
		pn->first_pkt_time = clkn;

	// Set afh channel map
	btbb_piconet_set_channel_seen(pn, pkt->channel);

	if (pn->packets_observed < MAX_PATTERN_LENGTH) {
		pn->pattern_indices[pn->packets_observed] = clkn - pn->first_pkt_time;
		pn->pattern_channels[pn->packets_observed] = pkt->channel;
	} else {
		printf("Oops. More hops than we can remember.\n");
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
			int clock = (count + clkn - pn->first_pkt_time) % 64;
			UAP = try_clock(clock, pkt);
			crc_chk = -1;

			/* if this is the first packet: populate the candidate list */
			/* if not: check CRCs if UAPs match */
			if (!btbb_piconet_get_flag(pn, BTBB_GOT_FIRST_PACKET)
				|| UAP == pn->clock6_candidates[count])
				crc_chk = crc_check(clock, pkt);

			if (btbb_piconet_get_flag(pn, BTBB_UAP_VALID) &&
			    (UAP != pn->UAP))
				crc_chk = -1;

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
					printf("Correct CRC! UAP = 0x%x found after %d total packets.\n",
						UAP, pn->total_packets_observed);
				else
					printf("Correct CRC! CLK6 = 0x%x found after %d total packets.\n",
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
			printf("UAP = 0x%x found after %d total packets.\n",
				pn->clock6_candidates[first_clock], pn->total_packets_observed);
		else
			printf("CLK6 = 0x%x found after %d total packets.\n",
				pn->clk6_offset, pn->total_packets_observed);
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

/* Check for existing piconets in survey results */
btbb_piconet *get_piconet(uint32_t lap)
{
	survey_hash *s;
	btbb_piconet *pn;
	HASH_FIND(hh, piconet_survey, &lap, 4, s);

	if (s == NULL) {
		pn = btbb_piconet_new();
		btbb_init_piconet(pn, lap);

		s = malloc(sizeof(survey_hash));
		s->key = lap;
		s->pn = pn;
		HASH_ADD(hh, piconet_survey, key, 4, s);
	} else {
		pn = s->pn;
	}
	return pn;
}

/* Destructively iterate over survey results */
btbb_piconet *btbb_next_survey_result() {
	btbb_piconet *pn = NULL;
	survey_hash *tmp;

	if (piconet_survey != NULL) {
		pn = piconet_survey->pn;
		tmp = piconet_survey;
		piconet_survey = piconet_survey->hh.next;
		free(tmp);
	}
	return pn;
}

int btbb_process_packet(btbb_packet *pkt, btbb_piconet *pn) {
	if (survey_mode) {
		pn = get_piconet(btbb_packet_get_lap(pkt));
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
				printf("Failed to decode packet\n");
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
