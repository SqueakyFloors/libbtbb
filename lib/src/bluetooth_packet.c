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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bluetooth_packet.h"
#include "uthash.h"
#include "sw_check_tables.h"

/* Maximum number of AC errors supported by library. Caller may
 * specify any value <= AC_ERROR_LIMIT in btbb_init(). */
#define AC_ERROR_LIMIT 5

/* maximum number of bit errors for known syncwords */
#define MAX_SYNCWORD_ERRS 5

/* maximum number of bit errors in  */
#define MAX_BARKER_ERRORS 1

/* default codeword modified for PN sequence and barker code */
#define DEFAULT_CODEWORD 0xb0000002c7820e7eULL

/* Default access code, used for calculating syndromes */
#define DEFAULT_AC 0xcc7b7268ff614e1bULL

/*
 * The packet header is 18 bits, and Vol 2, Part B, Section 7.2 runs the
 * whitening generator over the header and the payload without reinitializing
 * it in between, so a payload continues this far into the sequence.
 */
#define HEADER_WHITENING_BITS 18

/* The DV packet's voice field comes before its data field (Vol 2, Part B,
 * Section 6.5.2.4), and Section 7.2 whitens it along with the rest, so the
 * data field continues this much further into the sequence. */
#define DV_VOICE_BITS 80

/* index into whitening data array */
static const uint8_t INDICES[] = {99, 85, 17, 50, 102, 58, 108, 45, 92, 62, 32, 118, 88, 11, 80, 2, 37, 69, 55, 8, 20, 40, 74, 114, 15, 106, 30, 78, 53, 72, 28, 26, 68, 7, 39, 113, 105, 77, 71, 25, 84, 49, 57, 44, 61, 117, 10, 1, 123, 124, 22, 125, 111, 23, 42, 126, 6, 112, 76, 24, 48, 43, 116, 0};

/* whitening data */
static const uint8_t WHITENING_DATA[] = {1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1};

/* lookup table for barker code hamming distance */
static const uint8_t BARKER_DISTANCE[] = {
	3,3,3,2,3,2,2,1,2,3,3,3,3,3,3,2,2,3,3,3,3,3,3,2,1,2,2,3,2,3,3,3,
	3,2,2,1,2,1,1,0,3,3,3,2,3,2,2,1,3,3,3,2,3,2,2,1,2,3,3,3,3,3,3,2,
	2,3,3,3,3,3,3,2,1,2,2,3,2,3,3,3,1,2,2,3,2,3,3,3,0,1,1,2,1,2,2,3,
	3,3,3,2,3,2,2,1,2,3,3,3,3,3,3,2,2,3,3,3,3,3,3,2,1,2,2,3,2,3,3,3};

/* string representations of packet type */
static const char * const TYPE_NAMES[] = {
	"NULL", "POLL", "FHS", "DM1", "DH1/2-DH1", "HV1", "HV2/2-EV3", "HV3/EV3/3-EV3",
	"DV/3-DH1", "AUX1", "DM3/2-DH3", "DH3/3-DH3", "EV4/2-EV5", "EV5/3-EV5", "DM5/2-DH5", "DH5/3-DH5"
};

/*
 * generator matrix for sync word (64,30) linear block code
 * based on polynomial 0260534236651
 * thanks to http://www.ee.unb.ca/cgi-bin/tervo/polygen.pl
 * modified for barker code
 */
static const uint64_t sw_matrix[] = {
	0xfe000002a0d1c014ULL, 0x01000003f0b9201fULL, 0x008000033ae40edbULL, 0x004000035fca99b9ULL,
	0x002000036d5dd208ULL, 0x00100001b6aee904ULL, 0x00080000db577482ULL, 0x000400006dabba41ULL,
	0x00020002f46d43f4ULL, 0x000100017a36a1faULL, 0x00008000bd1b50fdULL, 0x000040029c3536aaULL,
	0x000020014e1a9b55ULL, 0x0000100265b5d37eULL, 0x0000080132dae9bfULL, 0x000004025bd5ea0bULL,
	0x00000203ef526bd1ULL, 0x000001033511ab3cULL, 0x000000819a88d59eULL, 0x00000040cd446acfULL,
	0x00000022a41aabb3ULL, 0x0000001390b5cb0dULL, 0x0000000b0ae27b52ULL, 0x0000000585713da9ULL};

static const uint64_t barker_correct[] = {
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL, 0x4e00000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL,
	0xb000000000000000ULL, 0xb000000000000000ULL, 0xb000000000000000ULL, 0x4e00000000000000ULL};

static const uint64_t pn = 0x83848D96BBCC54FCULL;

static const uint16_t fec23_gen_matrix[] = {
	0x2c01, 0x5802, 0x1c04, 0x3808, 0x7010,
	0x4c20, 0x3440, 0x6880, 0x7d00, 0x5600};

typedef struct {
    uint64_t syndrome; /* key */
    uint64_t error;
    UT_hash_handle hh;
} syndrome_struct;

static syndrome_struct *syndrome_map = NULL;

/* Returns 0 if the entry could not be made. A map with a hole in it is of no
 * use: the sync word search would quietly stop putting right the error
 * patterns the missing entries describe, so the caller throws the map away
 * and says so rather than correcting fewer errors than it was asked for. */
static int add_syndrome(uint64_t syndrome, uint64_t error)
{
	syndrome_struct *s;
	s = malloc(sizeof(syndrome_struct));
	if (s == NULL)
		return 0;
	s->syndrome = syndrome;
	s->error = error;

    HASH_ADD(hh, syndrome_map, syndrome, 8, s);
	return 1;
}

static void free_syndrome_map(void)
{
	syndrome_struct *s, *next;

	HASH_ITER(hh, syndrome_map, s, next) {
		HASH_DEL(syndrome_map, s);
		free(s);
	}
}

static syndrome_struct *find_syndrome(uint64_t syndrome)
{
    syndrome_struct *s;

    HASH_FIND(hh, syndrome_map, &syndrome, 8, s);
    return s;
}

static uint64_t gen_syndrome(uint64_t codeword)
{
	uint64_t syndrome = codeword & 0xffffffff;
	codeword >>= 32;
	syndrome ^= sw_check_table4[codeword & 0xff];
	codeword >>= 8;
	syndrome ^= sw_check_table5[codeword & 0xff];
	codeword >>= 8;
	syndrome ^= sw_check_table6[codeword & 0xff];
	codeword >>= 8;
	syndrome ^= sw_check_table7[codeword & 0xff];
	return syndrome;
}

/* Returns 0 if any entry could not be made; see add_syndrome(). */
static int cycle(uint64_t error, int start, int depth, uint64_t codeword)
{
	uint64_t new_error, syndrome, base;
	int i;
	base = 1;
	depth -= 1;
	for (i = start; i < 58; i++)
	{
		new_error = (base << i);
		new_error |= error;
		if (depth) {
			if (!cycle(new_error, i + 1, depth, codeword))
				return 0;
		}
		else {
			syndrome = gen_syndrome(codeword ^ new_error);
			if (!add_syndrome(syndrome, new_error))
				return 0;
		}
	}
	return 1;
}

/* Returns 0 if the whole map could not be built, having thrown away what it
 * had got: half a map corrects fewer errors than the caller asked for and
 * nothing would say so. */
static int gen_syndrome_map(int bit_errors)
{
	int i;
	for(i = 1; i <= bit_errors; i++) {
		if (!cycle(0, 0, i, DEFAULT_AC)) {
			free_syndrome_map();
			return 0;
		}
	}
	return 1;
}

/* Generate Sync Word from an LAP */
uint64_t btbb_gen_syncword(const int LAP)
{
	int i;
	uint64_t codeword = DEFAULT_CODEWORD;

	/* the sync word generated is in host order, not air order */
	for (i = 0; i < 24; i++)
		if (LAP & (0x800000 >> i))
			codeword ^= sw_matrix[i];

	return codeword;
}

static void init_packet(btbb_packet *pkt, uint32_t lap, uint8_t ac_errors)
{
	pkt->LAP = lap;
	pkt->ac_errors = ac_errors;

	pkt->flags = 0;
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);
}

/* Convert some number of bits of an air order array to a host order integer */
static uint8_t air_to_host8(const char *air_order, const int bits)
{
	int i;
	uint8_t host_order = 0;
	for (i = 0; i < bits; i++)
		host_order |= ((uint8_t)air_order[i] << i);
	return host_order;
}
static uint16_t air_to_host16(const char *air_order, const int bits)
{
	int i;
	uint16_t host_order = 0;
	for (i = 0; i < bits; i++)
		host_order |= ((uint16_t)air_order[i] << i);
	return host_order;
}
static uint32_t air_to_host32(const char *air_order, const int bits)
{
	int i;
	uint32_t host_order = 0;
	for (i = 0; i < bits; i++)
		host_order |= ((uint32_t)air_order[i] << i);
	return host_order;
}
static uint64_t air_to_host64(const char *air_order, const int bits)
{
	int i;
	uint64_t host_order = 0;
	for (i = 0; i < bits; i++)
		host_order |= ((uint64_t)air_order[i] << i);
	return host_order;
}

///* Convert some number of bits in a host order integer to an air order array */
//static void host_to_air(const uint8_t host_order, char *air_order, const int bits)
//{
//    int i;
//    for (i = 0; i < bits; i++)
//        air_order[i] = (host_order >> i) & 0x01;
//}

/* count the number of 1 bits in a uint64_t */
static uint8_t count_bits(uint64_t n)
{
#ifdef __GNUC__
	return (uint8_t) __builtin_popcountll (n);
#else
	uint8_t i = 0;
	for (i = 0; n != 0; i++)
		n &= n - 1;
	return i;
#endif
}

#ifndef RELEASE
#define RELEASE "unknown"
#endif
const char* btbb_get_release(void) {
	return RELEASE;
}

#ifndef VERSION
#define VERSION "unknown"
#endif
const char* btbb_get_version(void) {
	return VERSION;
}

int btbb_init(int max_ac_errors)
{
	/* Sanity check max_ac_errors. */
	if ( (max_ac_errors < 0) || (max_ac_errors > AC_ERROR_LIMIT) ) {
		fprintf(stderr, "%s: max_ac_errors out of range\n",
			__FUNCTION__);
		return -1;
	}

	if ((syndrome_map == NULL) && (max_ac_errors)) {
		if (!gen_syndrome_map(max_ac_errors)) {
			fprintf(stderr, "%s: no memory for the access code "
				"error correction table\n", __FUNCTION__);
			return -1;
		}
	}

	return 0;
}

btbb_packet *
btbb_packet_new(void)
{
	btbb_packet *pkt = (btbb_packet *)calloc(1, sizeof(btbb_packet));
	if(pkt)
		pkt->refcount = 1;
	else
		fprintf(stderr, "Unable to allocate packet");
	return pkt;
}

void
btbb_packet_ref(btbb_packet *pkt)
{
	pkt->refcount++;
}

void
btbb_packet_unref(btbb_packet *pkt)
{
	pkt->refcount--;
	if (pkt->refcount == 0)
		free(pkt);
}

uint32_t btbb_packet_get_lap(const btbb_packet *pkt)
{
	return pkt->LAP;
}

void btbb_packet_set_uap(btbb_packet *pkt, uint8_t uap)
{
	pkt->UAP = uap;
	btbb_packet_set_flag(pkt, BTBB_UAP_VALID, 1);
}

uint8_t btbb_packet_get_uap(const btbb_packet *pkt)
{
	return pkt->UAP;
}

uint16_t btbb_packet_get_nap(const btbb_packet *pkt)
{
	return pkt->NAP;
}

uint32_t btbb_packet_get_clkn(const btbb_packet *pkt) {
	return pkt->clkn;
}

uint8_t btbb_packet_get_channel(const btbb_packet *pkt) {
	return pkt->channel;
}

void btbb_packet_set_modulation(btbb_packet *pkt, uint8_t modulation) {
	pkt->modulation = modulation;
}

uint8_t btbb_packet_get_modulation(const btbb_packet *pkt) {
	return pkt->modulation;
}

void btbb_packet_set_transport(btbb_packet *pkt, uint8_t transport) {
	pkt->transport = transport;
}

uint8_t btbb_packet_get_transport(const btbb_packet *pkt) {
	return pkt->transport;
}

uint8_t btbb_packet_get_ac_errors(const btbb_packet *pkt) {
	return pkt->ac_errors;
}

/*
 * Matching any LAP. 'search_length' offsets are tried, each as the start of a
 * sync word, so this reads search_length + SYNCWORD_SYMBOLS - 1 symbols:
 * btbb_find_ac_n() has already cut it down to the symbols the caller holds.
 */
int promiscuous_packet_search(char *stream, int search_length, uint32_t *lap,
							  int max_ac_errors, uint8_t *ac_errors) {
	uint64_t syncword, codeword, syndrome, corrected_barker;
	syndrome_struct *errors;
	char *symbols;
	int count, offset = -1;

	/* With no offset to search there is no sync word to filter, and the
	 * caller has promised nothing about the symbols the filter below
	 * would read. */
	if (search_length < 1)
		return -1;

	/* Barker code at end of sync word (includes
	 * MSB of LAP) is used as a rough filter.
	 */
	uint8_t barker = air_to_host8(&stream[57], 6);
	barker <<= 1;

	for (count = 0; count < search_length; count++) {
		symbols = &stream[count];
		barker >>= 1;
		barker |= (symbols[SYNCWORD_SYMBOLS - 1] << 6);
		if (BARKER_DISTANCE[barker] <= MAX_BARKER_ERRORS) {
			// Error correction
			syncword = air_to_host64(symbols, SYNCWORD_SYMBOLS);

			/* correct the barker code with a simple comparison */
			corrected_barker = barker_correct[(uint8_t)(syncword >> 57)];
			syncword = (syncword & 0x01ffffffffffffffULL) | corrected_barker;

			codeword = syncword ^ pn;

			/* Zero syndrome -> good codeword. */
			syndrome = gen_syndrome(codeword);
			*ac_errors = 0;

			/* Try to fix errors in bad codeword. */
			if (syndrome) {
				errors = find_syndrome(syndrome);
				if (errors != NULL) {
					syncword ^= errors->error;
					*ac_errors = count_bits(errors->error);
					syndrome = 0;
				}
				else {
					*ac_errors = 0xff;  // fail
				}
			}

			if (*ac_errors <= max_ac_errors) {
				*lap = (syncword >> 34) & 0xffffff;
				offset = count;
				break;
			}
		}
	}
	return offset;
}

/* Matching a specific LAP. Reads as far as the search above, and is called
 * with a 'search_length' bounded the same way. */
int find_known_lap(char *stream, int search_length, uint32_t lap,
				   int max_ac_errors, uint8_t *ac_errors) {
	uint64_t syncword, ac;
	char *symbols;
	int count, offset = -1;

	ac = btbb_gen_syncword(lap);
	for (count = 0; count < search_length; count++) {
		symbols = &stream[count];
		syncword = air_to_host64(symbols, SYNCWORD_SYMBOLS);
		*ac_errors = count_bits(syncword ^ ac);

		if (*ac_errors <= max_ac_errors) {
			offset = count;
			break;
		}
	}
	return offset;
}

/* Looks for an AC in the stream it is given, and no further */
int btbb_find_ac_n(char *stream, size_t stream_len, int search_length,
				   uint32_t lap, int max_ac_errors,
				   btbb_packet **pkt_ptr) {
	int offset;
	uint8_t ac_errors;

	/*
	 * Each offset searched is tried as the start of a sync word, so the
	 * last offset the stream holds one of is stream_len - SYNCWORD_SYMBOLS,
	 * and a stream shorter than a sync word holds none at all. Searching
	 * past that would read symbols the caller never passed.
	 */
	if (search_length < 0)
		search_length = 0;
	if (stream_len < SYNCWORD_SYMBOLS)
		search_length = 0;
	else if ((size_t) search_length > stream_len - (SYNCWORD_SYMBOLS - 1))
		search_length = (int) (stream_len - (SYNCWORD_SYMBOLS - 1));

	/* Matching any LAP */
	if (lap == LAP_ANY)
		offset = promiscuous_packet_search(stream, search_length, &lap,
										   max_ac_errors, &ac_errors);
	else
		offset = find_known_lap(stream, search_length, lap,
								max_ac_errors, &ac_errors);

	if (offset >= 0) {
		if (*pkt_ptr == NULL)
			*pkt_ptr = btbb_packet_new();
		init_packet(*pkt_ptr, lap, ac_errors);
	}

	return offset;
}

/* Looks for an AC in the stream, trusting the caller for its length */
int btbb_find_ac(char *stream, int search_length, uint32_t lap,
				 int max_ac_errors, btbb_packet **pkt_ptr) {
	/* the symbols this form has always read */
	size_t stream_len = 0;

	if (search_length > 0) {
		stream_len = (size_t) search_length;
		/* No sum where size_t is no wider than int. A search length
		 * that close to the maximum is not one any caller makes, and
		 * the largest length there is reads no less than it asked
		 * for. */
		if (stream_len <= SIZE_MAX - (SYNCWORD_SYMBOLS - 1))
			stream_len += SYNCWORD_SYMBOLS - 1;
		else
			stream_len = SIZE_MAX;
	}

	return btbb_find_ac_n(stream, stream_len, search_length, lap,
			      max_ac_errors, pkt_ptr);
}

/* Copy data (symbols) into packet and set rx data. */
void btbb_packet_set_data(btbb_packet *pkt, char *data, int length,
						  uint8_t channel, uint32_t clkn)
{
	int i;

	/* The symbol count is stored unsigned, so a negative one would come
	 * back out as a very large count and tell every decoder there are
	 * symbols here that the caller never provided. A caller that worked
	 * one out by subtraction can land here with one.
	 */
	if (length < 0)
		length = 0;
	if (length > MAX_SYMBOLS)
		length = MAX_SYMBOLS;
	for (i = 0; i < length; i++)
		pkt->symbols[i] = data[i];

	pkt->length = length;
	pkt->channel = channel;
	/* The caller's clock counts 312.5 us ticks (CLK27-0); the packet
	 * keeps CLK1-27, the 625 us slot count, because that is the clock
	 * dewhitening, the CRC and hop reversal all work in. */
	pkt->clkn = clkn >> 1;

	/* Nothing is known about these symbols yet, so drop everything the
	 * decoder worked out about the last ones: what it checked, the header
	 * it read, the payload header, and the payload. A caller hands one
	 * packet object a frame after another - btbb_find_ac() gives out the
	 * same object again - and not every route into the decoder starts by
	 * clearing this: the search for a piconet's address part goes through
	 * try_clock() and crc_check(), neither of which reads the header. So
	 * whatever is left here is reported for a frame it does not describe,
	 * which for a capture file means a record saying another frame's HEC
	 * was checked and holding another frame's header bits.
	 *
	 * What the caller set - the address part, the clock, whether the
	 * symbols are whitened, the modulation - is the caller's and stays. */
	btbb_packet_set_flag(pkt, BTBB_HAS_PAYLOAD, 0);
	btbb_packet_set_flag(pkt, BTBB_HEC_CHECKED, 0);
	btbb_packet_set_flag(pkt, BTBB_HEC_VALID, 0);
	btbb_packet_set_flag(pkt, BTBB_CRC_CHECKED, 0);
	btbb_packet_set_flag(pkt, BTBB_CRC_CORRECT, 0);
	memset(pkt->packet_header, 0, sizeof(pkt->packet_header));
	pkt->packet_lt_addr = 0;
	pkt->packet_type = 0;
	pkt->packet_flags = 0;
	pkt->packet_hec = 0;
	memset(pkt->payload_header, 0, sizeof(pkt->payload_header));
	pkt->payload_llid = 0;
	pkt->payload_flow = 0;
	pkt->payload_length = 0;
	pkt->payload_header_length = 0;
	pkt->corrected_header_bits = 0;
	pkt->corrected_payload_bits = 0;
}

void btbb_packet_set_flag(btbb_packet *pkt, int flag, int val)
{
	uint32_t mask = 1L << flag;
	pkt->flags &= ~mask;
	if (val)
		pkt->flags |= mask;
}

int btbb_packet_get_flag(const btbb_packet *pkt, int flag)
{
	uint32_t mask = 1L << flag;
	return ((pkt->flags & mask) != 0);
}

const char *btbb_get_symbols(const btbb_packet* pkt)
{
	return (const char*) pkt->symbols;
}

int btbb_packet_get_payload_length(const btbb_packet* pkt)
{
	return pkt->payload_length;
}

const char *btbb_get_payload(const btbb_packet* pkt)
{
	return (const char*) pkt->payload;
}

int btbb_get_payload_packed(const btbb_packet* pkt, char *dst)
{
	int i;
	for(i=0;i<pkt->payload_length;i++)
		dst[i] = (char) air_to_host8(&pkt->payload[i*8], 8);
	return pkt->payload_length;
}

void btbb_packet_set_esco_length(btbb_packet *pkt, int length)
{
	pkt->esco_length = length;
}

int btbb_packet_get_esco_length(const btbb_packet *pkt)
{
	return pkt->esco_length;
}

uint8_t btbb_packet_get_type(const btbb_packet* pkt)
{
	return pkt->packet_type;
}

uint8_t btbb_packet_get_lt_addr(const btbb_packet* pkt)
{
	return pkt->packet_lt_addr;
}

uint8_t btbb_packet_get_header_flags(const btbb_packet* pkt)
{
	return pkt->packet_flags;
}

uint8_t btbb_packet_get_hec(const btbb_packet* pkt)
{
	return pkt->packet_hec;
}

uint32_t btbb_packet_get_header_packed(const btbb_packet* pkt)
{
	return air_to_host32(&pkt->packet_header[0], 18);
}

/* Reverse the bits in a byte */
static uint8_t reverse(char byte)
{
	return (byte & 0x80) >> 7 | (byte & 0x40) >> 5 | (byte & 0x20) >> 3 | (byte & 0x10) >> 1 | (byte & 0x08) << 1 | (byte & 0x04) << 3 | (byte & 0x02) << 5 | (byte & 0x01) << 7;
}


/*
 * Decode 1/3 rate FEC, three like symbols in a row.
 *
 * corrected, when it is not NULL, is set to the number of output bits the
 * majority vote had to put right: the bits whose three symbols did not all
 * agree. The capture file writers record that count, so a reader can see how
 * marginal the reception was.
 */
static int unfec13(char *input, char *output, int length, int *corrected)
{
	int a, b, c, i;
	int be = 0; /* bit errors */

	for (i = 0; i < length; i++) {
		a = 3 * i;
		b = a + 1;
		c = a + 2;
		output[i] = ((input[a] & input[b]) | (input[b] & input[c]) |
				(input[c] & input[a]));
		be += ((input[a] ^ input[b]) | (input[b] ^ input[c]) |
				(input[c] ^ input[a]));
	}

	if (corrected != NULL)
		*corrected = be;

	return (be < (length / 4));
}

/* encode 10 bits with 2/3 rate FEC code, a (15,10) shortened Hamming code */
static uint16_t fec23(uint16_t data)
{
	int i;
	uint16_t codeword = 0;

	/* host order, not air order */
	for (i = 0; i < 10; i++)
		if (data & (1 << i))
			codeword ^= fec23_gen_matrix[i];

	return codeword;
}

/*
 * Decode 2/3 rate FEC, a (15,10) shortened Hamming code.
 *
 * corrected, when it is not NULL, is set to the number of bits the code had
 * to deal with: one for each 15-symbol block whose check symbols disagreed
 * with its data, since the code puts right one wrong symbol per block. A
 * block it cannot put right ends the decode, so a count only ever covers
 * blocks that came out. The capture file writers record it.
 */
static char *unfec23(char *input, int length, int *corrected)
{
	/* input points to the input data
	 * length is length in bits of the data
	 * before it was encoded with fec2/3 */
	int iptr, optr, count;
	int errors = 0;
	char* output;
	uint8_t diff, check;
	uint16_t data, codeword;

	if (corrected != NULL)
		*corrected = 0;

	diff = length % 10;
	// padding at end of data
	if(0!=diff)
		length += (10 - diff);

	output = (char *) malloc(length);
	/* Every caller already treats a null return as a block it could not
	 * put right, and reports no payload for it. */
	if (output == NULL)
		return NULL;

	for (iptr = 0, optr = 0; optr<length; iptr += 15, optr += 10) {
		// copy data to output
		for(count=0;count<10;count++)
			output[optr+count] = input[iptr+count];

		// grab data and error check in host format
		data = air_to_host16(input+iptr, 10);
		check = air_to_host8(input+iptr+10, 5);

		// call fec23 on data to generate the codeword
		codeword = fec23(data);
		diff = check ^ (codeword >> 10);

		/* no errors or single bit errors (errors in the parity bit):
		 * (a strong hint it's a real packet)
		 * Otherwise we need to corret the output*/
		if (diff & (diff - 1)) {
			switch (diff) {
			/* comments are the bit that's wrong and the value
			* of diff in air order, from the BT spec */
				// 1000000000 11010
				case 0x0b: output[optr] ^= 1; break;
				// 0100000000 01101
				case 0x16: output[optr+1] ^= 1; break;
				// 0010000000 11100
				case 0x07: output[optr+2] ^= 1; break;
				// 0001000000 01110
				case 0x0e: output[optr+3] ^= 1; break;
				// 0000100000 00111
				case 0x1c: output[optr+4] ^= 1; break;
				// 0000010000 11001
				case 0x13: output[optr+5] ^= 1; break;
				// 0000001000 10110
				case 0x0d: output[optr+6] ^= 1; break;
				// 0000000100 01011
				case 0x1a: output[optr+7] ^= 1; break;
				// 0000000010 11111
				case 0x1f: output[optr+8] ^= 1; break;
				// 0000000001 10101
				case 0x15: output[optr+9] ^= 1; break;
				/* not one of these errors, probably multiple bit errors
				* or maybe not a real packet, safe to drop it? */
				default: free(output); return 0;
			}
		}

		/* A check that agrees with the data is a block that arrived
		 * intact. Anything else is one wrong symbol the code dealt
		 * with: a check symbol, which leaves the data alone, or a
		 * data bit the switch above has just flipped back. */
		if (diff != 0)
			errors++;
	}

	if (corrected != NULL)
		*corrected = errors;

	return output;
}


/*
 * The number of symbols a payload of this many bits arrives in when it is
 * carried by the rate 2/3 FEC code. The code sends 15 symbols for every 10
 * bits and pads the last block out, so the symbols a payload needs are half
 * again as many as its bits, rounded up to a whole block.
 */
static int fec23_symbols(int bits)
{
	return ((bits + 9) / 10) * 15;
}

/* Remove the whitening from an air order array */
static void unwhiten(char* input, char* output, int clock, int length, int skip, btbb_packet* pkt)
{
	int count, index;
	index = INDICES[clock & 0x3f];
	index += skip;
	index %= 127;

	for(count = 0; count < length; count++)
	{
		/* unwhiten if whitened, otherwise just copy input to output */
		output[count] = btbb_packet_get_flag(pkt, BTBB_WHITENED) ?
			input[count] ^ WHITENING_DATA[index] : input[count];
		index += 1;
		index %= 127;
	}
}

/* Pointer to start of packet, length of packet in bits, UAP */
static uint16_t crcgen(char *payload, int length, int UAP)
{
	char bit;
	uint16_t reg, count;

	reg = (reverse(UAP) << 8) & 0xff00;
	for(count = 0; count < length; count++)
	{
		bit = payload[count];

		reg = (reg >> 1) | (((reg & 0x0001) ^ (bit & 0x01))<<15);

		/*Bit 5*/
		reg ^= ((reg & 0x8000)>>5);

		/*Bit 12*/
		reg ^= ((reg & 0x8000)>>12);
	}
	return reg;
}

/* extract UAP by reversing the HEC computation */
static uint8_t uap_from_hec(uint16_t data, uint8_t hec)
{
        int i;

        for (i = 9; i >= 0; i--) {
                /* 0x65 is xor'd if MSB is 1, else 0x00 (which does nothing) */
                if (hec & 0x80)
                        hec ^= 0x65;

                hec = (hec << 1) | (((hec >> 7) ^ (data >> i)) & 0x01);
        }
        return reverse(hec);
}

/* check if the packet's CRC is correct for a given clock (CLK1-6) */
int crc_check(int clock, btbb_packet* pkt)
{
	/*
	 * return value of 1 represents inconclusive result (default)
	 * return value > 1 represents positive result (e.g. CRC match)
	 * return value of 0 represents negative result (e.g. CRC failure without
	 * the possibility that we have assumed the wrong logical transport)
	 */
	int retval = 1;

	switch(pkt->packet_type)
	{
		case PACKET_TYPE_FHS:
			retval = fhs(clock, pkt);
			break;

		case PACKET_TYPE_DV:
		case PACKET_TYPE_DM1:
		case PACKET_TYPE_DM3:
		case PACKET_TYPE_DM5:
			retval = DM(clock, pkt);
			break;

		case PACKET_TYPE_DH1:
		case PACKET_TYPE_DH3:
		case PACKET_TYPE_DH5:
			retval = DH(clock, pkt);
			break;

		case PACKET_TYPE_HV3: /* EV3 */
			retval = EV3(clock, pkt);
			break;
		case PACKET_TYPE_EV4:
			retval = EV4(clock, pkt);
			break;
		case PACKET_TYPE_EV5:
			retval = EV5(clock, pkt);
			break;

		case PACKET_TYPE_HV1:
			retval = HV(clock, pkt);
			break;

		/* some types can't help us */
		default:
			break;
	}
	/*
	 * never return a zero result unless this is a FHS, DM1, or HV1.  any
	 * other type could have actually been something else (another logical
	 * transport)
	 */
	if (retval == 0 && (pkt->packet_type != 2 && pkt->packet_type != 3 &&
			pkt->packet_type != 5))
		retval = 1;

	/* EV3 and EV5 have a relatively high false positive rate */
	if (retval > 1 && (pkt->packet_type == 7 || pkt->packet_type == 13))
		retval = 1;

	/* This is how the library guesses a piconet's upper address part: it
	 * decodes the same packet at up to 64 candidate clocks and keeps the
	 * one whose CRC comes out.
	 *
	 * More than 2 is a CRC that came out, which is the answer the caller
	 * takes as this piconet's address part and clock. It is about this
	 * packet, so leave it on it. Anything else is a trial the caller goes
	 * on from, and the trial has overwritten the payload, its length and
	 * the packet type with a candidate's, so nothing that was known about
	 * the packet before still describes what it now holds: say nothing was
	 * checked. A capture file would otherwise report a CRC failure for a
	 * payload nothing ever decoded. */
	if (retval <= 2) {
		btbb_packet_set_flag(pkt, BTBB_HEC_CHECKED, 0);
		btbb_packet_set_flag(pkt, BTBB_HEC_VALID, 0);
		btbb_packet_set_flag(pkt, BTBB_CRC_CHECKED, 0);
		btbb_packet_set_flag(pkt, BTBB_CRC_CORRECT, 0);
		/* The payload is the trial's too, so it is no longer a
		 * payload this packet decoded. A caller that decoded the
		 * packet before starting the search would otherwise have the
		 * last trial clock's bytes reported as the packet's. */
		btbb_packet_set_flag(pkt, BTBB_HAS_PAYLOAD, 0);
		pkt->payload_length = 0;
		pkt->corrected_payload_bits = 0;
	}
	else {
		/* The CRC came out at this clock, so this trial is the
		 * packet's own decode: the payload in the field is the one
		 * the CRC flags above describe. Report it as a payload this
		 * packet decoded, the same as btbb_decode_payload() does, or
		 * the record of the packet that settled the address part -
		 * the first evidence the address part was derived from -
		 * would carry a correct CRC over a payload a reader cannot
		 * see. */
		btbb_packet_set_flag(pkt, BTBB_HAS_PAYLOAD,
				     pkt->payload_length > 0);
	}

	return retval;
}

/* verify the payload CRC */
static int payload_crc(btbb_packet* pkt)
{
	uint16_t crc;   /* CRC calculated from payload data */
	uint16_t check; /* CRC supplied by packet */

	/* A payload cannot be shorter than the two byte CRC it carries plus
	 * the byte the CRC covers, so below that there is no CRC to check and
	 * reading one would read from before the payload. */
	if (pkt->payload_length < 3)
		return 0;

	crc = crcgen(pkt->payload, (pkt->payload_length - 2) * 8, pkt->UAP);
	check = air_to_host16(&pkt->payload[(pkt->payload_length - 2) * 8], 16);

	/* Record the answer on the packet, so a capture file can say whether
	   the CRC was checked and whether it passed. A type whose payload has
	   no CRC only reaches here when the decoder is trying that type out,
	   and clears both flags again if it settles on the type without
	   one. */
	btbb_packet_set_flag(pkt, BTBB_CRC_CHECKED, 1);
	btbb_packet_set_flag(pkt, BTBB_CRC_CORRECT, crc == check);

	return (crc == check);
}

int fhs(int clock, btbb_packet* pkt)
{
	/* skip the access code and packet header */
	char *stream = pkt->symbols + 122;
	/* number of symbols remaining after access code and packet header */
	int size = pkt->length - 122;
	int fec_corrections = 0;

	pkt->payload_length = 20;

	if (size < fec23_symbols(pkt->payload_length * 8)) {
		/* the capture ends before the payload does, so there is
		 * nothing decoded to report */
		pkt->payload_length = 0;
		return 1; //FIXME should throw exception
	}

	char *corrected = unfec23(stream, pkt->payload_length * 8,
				  &fec_corrections);
	if (!corrected) {
		/* a block the error correction could not put right, so
		 * nothing is decoded to report */
		pkt->payload_length = 0;
		return 0;
	}
	pkt->corrected_payload_bits = (int16_t) fec_corrections;

	/* try to unwhiten with known clock bits */
	unwhiten(corrected, pkt->payload, clock, pkt->payload_length * 8,
		 HEADER_WHITENING_BITS, pkt);
	if (payload_crc(pkt)) {
		free(corrected);
		return 1000;
	}

	/* try all 32 possible X-input values instead */
	for (clock = 32; clock < 64; clock++) {
		unwhiten(corrected, pkt->payload, clock,
			 pkt->payload_length * 8, HEADER_WHITENING_BITS, pkt);
		if (payload_crc(pkt)) {
			free(corrected);
			return 1000;
		}
	}

	/* No whitening input made the CRC come out, so what is left in the
	 * payload is the last one tried and known to be wrong: report none,
	 * as the two give-up paths above do, and nothing corrected in it
	 * either. */
	pkt->payload_length = 0;
	pkt->corrected_payload_bits = 0;
	free(corrected);
	return 0;
}

/*
 * Decode the payload header, return value indicates success.
 *
 * skip is how many whitening bits the packet has already used up. The
 * whitening generator runs once over the header and the payload with no
 * reinitialization in between (Bluetooth Core v5.4, Vol 2, Part B, Section
 * 7.2), so for most packets the payload starts 18 bits in, after the header,
 * and for a DV packet it starts after the voice field as well.
 */
static int decode_payload_header(char *stream, int clock, int header_bytes, int size, int fec, int skip, btbb_packet* pkt)
{
	/* nothing is decoded yet, so a caller that goes on to report a
	 * payload after this fails does not report a length from whatever the
	 * packet held before */
	pkt->payload_length = 0;

	if(header_bytes == 2)
	{
		if(size < 16)
			return 0; //FIXME should throw exception
		if(fec) {
			if(size < fec23_symbols(16))
				return 0; //FIXME should throw exception
			/* The corrections here are not counted: the caller
			 * goes on to decode the whole payload, this header
			 * included, and counts them there. */
			char *corrected = unfec23(stream, 16, NULL);
			if (!corrected)
				return 0;
			unwhiten(corrected, pkt->payload_header, clock, 16, skip, pkt);
			free(corrected);
		} else {
			unwhiten(stream, pkt->payload_header, clock, 16, skip, pkt);
		}
		/* payload length is payload body length + 2 bytes payload header + 2 bytes CRC */
		pkt->payload_length = air_to_host16(&pkt->payload_header[3], 10) + 4;
	} else {
		if(size < 8)
			return 0; //FIXME should throw exception
		if(fec) {
			if(size < fec23_symbols(8))
				return 0; //FIXME should throw exception
			/* as above, counted by the decode of the whole
			 * payload */
			char *corrected = unfec23(stream, 8, NULL);
			if (!corrected)
				return 0;
			unwhiten(corrected, pkt->payload_header, clock, 8, skip, pkt);
			free(corrected);
		} else {
			unwhiten(stream, pkt->payload_header, clock, 8, skip, pkt);
		}
		/* payload length is payload body length + 1 byte payload
		 * header + 2 bytes CRC, except for the AUX1 packet, which has
		 * no CRC (Vol 2, Part B, Section 6.5.4.7) */
		pkt->payload_length = air_to_host8(&pkt->payload_header[3], 5) + 1;
		if (pkt->packet_type != PACKET_TYPE_AUX1)
			pkt->payload_length += 2;
	}
	/* Try to set the max payload length to a sensible value,
	 * especially when using strange data
	 */
	int max_length = 0;
	switch(pkt->packet_type) {
		case PACKET_TYPE_DM1:
			max_length = 20;
			break;
		case PACKET_TYPE_DH1:
			/* assuming DH1 but could be 2-DH1 (58) */
			max_length = 30;
			break;
		case PACKET_TYPE_DV:
			/* assuming DV but could be 3-DH1 (87) */
			max_length = 12; /* + 10bytes of voice data */
			break;
		case PACKET_TYPE_AUX1:
			/* 30 bytes of payload header and body, no CRC */
			max_length = 30;
			break;
		case PACKET_TYPE_DM3:
			/* assuming DM3 but could be 2-DH3 (371) */
			max_length = 125;
			break;
		case PACKET_TYPE_DH3:
			/* assuming DH3 but could be 3-DH3 (556) */
			max_length = 187;
			break;
		case PACKET_TYPE_DM5:
			/* assuming DM5 but could be 2-DH5 (683) */
			max_length = 228;
			break;
		case PACKET_TYPE_DH5:
			/* assuming DH5 but could be 3-DH5 (1025) */
			max_length = 343;
			break;
	}
	if (pkt->payload_length > max_length)
		pkt->payload_length = max_length;
	pkt->payload_llid = air_to_host8(&pkt->payload_header[0], 2);
	pkt->payload_flow = air_to_host8(&pkt->payload_header[2], 1);
	pkt->payload_header_length = header_bytes;
	return 1;
}

/* DM 1/3/5 packet (and DV)*/
int DM(int clock, btbb_packet* pkt)
{
	int bitlength;
	/* number of bytes in the payload header */
	int header_bytes = 2;
	/* maximum payload length */
	int max_length;
	/* skip the access code and packet header */
	char *stream = pkt->symbols + 122;
	/* number of symbols remaining after access code and packet header */
	int size = pkt->length - 122;
	/* whitening bits the packet header has already used up */
	int skip = HEADER_WHITENING_BITS;
	int fec_corrections = 0;

	switch(pkt->packet_type)
	{
		case PACKET_TYPE_DV:
			/* skip the voice bits, then treat the rest like a DM1 */
			stream += DV_VOICE_BITS;
			size -= DV_VOICE_BITS;
			header_bytes = 1;
			/* the voice field is whitened too, so the data field
			 * continues that much further into the sequence */
			skip += DV_VOICE_BITS;
			/* I don't think the length of the voice field ("synchronous data
			 * field") is included in the length indicated by the payload
			 * header in the data field ("asynchronous data field"), but I
			 * could be wrong.
			 */
			max_length = 12;
			break;
		case PACKET_TYPE_DM1:
			header_bytes = 1;
			max_length = 20;
			break;
		case PACKET_TYPE_DM3:
			max_length = 125;
			break;
		case PACKET_TYPE_DM5:
			max_length = 228;
			break;
		default: /* not a DM1/3/5 or DV */
			return 0;
	}
	if(!decode_payload_header(stream, clock, header_bytes, size, 1, skip, pkt))
		return 0;
	/* Check that the length indicated in the payload header is within
	 * spec. Nothing below here has decoded a payload yet, so a length the
	 * type cannot carry leaves none to report. decode_payload_header()
	 * already clamps the claimed length to the type's maximum, so this is
	 * the backstop for the two maximums ever disagreeing. */
	if(pkt->payload_length > max_length) {
		pkt->payload_length = 0;
		/* could be encrypted */
		return 1;
	}
	bitlength = pkt->payload_length*8;
	if(fec23_symbols(bitlength) > size) {
		/* the capture ends before the payload does: the rate 2/3 FEC
		 * this type carries needs half again as many symbols as the
		 * payload has bits */
		pkt->payload_length = 0;
		return 1; //FIXME should throw exception
	}

	char *corrected = unfec23(stream, bitlength, &fec_corrections);
	if (!corrected) {
		/* a block the error correction could not put right, so
		 * nothing is decoded to report */
		pkt->payload_length = 0;
		return 0;
	}
	pkt->corrected_payload_bits = (int16_t) fec_corrections;
	unwhiten(corrected, pkt->payload, clock, bitlength, skip, pkt);
	free(corrected);

	if (payload_crc(pkt))
		return 10;

	/* could be encrypted */
	return 2;
}

/* DH 1/3/5 packet (and AUX1) */
/* similar to DM 1/3/5 but without FEC */
int DH(int clock, btbb_packet* pkt)
{
	int bitlength;
	/* number of bytes in the payload header */
	int header_bytes = 2;
	/* maximum payload length */
	int max_length;
	/* skip the access code and packet header */
	char *stream = pkt->symbols + 122;
	/* number of symbols remaining after access code and packet header */
	int size = pkt->length - 122;

	switch(pkt->packet_type)
	{
		case PACKET_TYPE_AUX1:
		case PACKET_TYPE_DH1:
			header_bytes = 1;
			max_length = 30;
			break;
		case PACKET_TYPE_DH3:
			max_length = 187;
			break;
		case PACKET_TYPE_DH5:
			max_length = 343;
			break;
		default: /* not a DH1/3/5 */
			return 0;
	}
	/* These types carry their payload with no forward error correction,
	 * so nothing in it is corrected. The other decoders report the count
	 * they made; this one reports none, rather than leaving behind a
	 * count from whatever decoded this packet before. */
	pkt->corrected_payload_bits = 0;
	if(!decode_payload_header(stream, clock, header_bytes, size, 0,
				  HEADER_WHITENING_BITS, pkt))
		return 0;
	/* As in DM() above: nothing is decoded yet, so a length the type
	 * cannot carry leaves no payload to report, and the clamp in
	 * decode_payload_header() means this is only a backstop. */
	if(pkt->payload_length > max_length) {
		pkt->payload_length = 0;
		/* could be encrypted */
		return 1;
	}
	bitlength = pkt->payload_length*8;
	if(bitlength > size) {
		/* the capture ends before the payload does, so there is
		 * nothing decoded to report */
		pkt->payload_length = 0;
		return 1; //FIXME should throw exception
	}

	unwhiten(stream, pkt->payload, clock, bitlength,
		 HEADER_WHITENING_BITS, pkt);

	/* AUX1 has no CRC */
	if (pkt->packet_type == PACKET_TYPE_AUX1)
		return 2;

	if (payload_crc(pkt))
		return 10;

	/* could be encrypted */
	return 2;
}

/*
 * Whether the payload bytes from first up to, but not including, last are all
 * zero.
 */
static int payload_bytes_zero(const btbb_packet* pkt, int first, int last)
{
	int byte;

	for (byte = first; byte < last; byte++)
		if (air_to_host8(&pkt->payload[byte * 8], 8) != 0)
			return 0;

	return 1;
}

/*
 * Whether a length whose CRC agrees is a better answer than one that already
 * agreed, for a search that has no length to go on. extended says the length
 * that already agreed was itself reached by the rule below.
 *
 * A length agrees when the CRC register comes out zero over the bytes it
 * covers, and zero bytes leave a zero register where it is. So lengths that
 * differ only by zero bytes all agree together, and a run of them says
 * nothing about where the payload ends. Both ends of a run are one byte in
 * 256, and the packet holds nothing that tells them apart:
 *
 *   - the payload ends at the start of the run, and the byte after it, which
 *     is whatever the receiver recorded once the packet was over, is zero;
 *   - the payload ends one byte later, and its own CRC ends in a zero byte,
 *     which leaves the register at zero a byte early and makes the length one
 *     short of the payload agree as well.
 *
 * This takes the second, one byte past the start of the run and no further,
 * which keeps every information byte the payload carried. The cost is a
 * payload reported one byte long when it was the first: its CRC's low byte is
 * reported as the last information byte. Nothing beyond that byte is taken,
 * so a capture of zeros after the packet does not run the length up to the
 * longest the type allows, which is where "the longest length that agrees"
 * on its own would end up. A caller that knows the link's length does not
 * have the guess made for it at all; see btbb_packet_set_esco_length().
 */
static int better_length(const btbb_packet* pkt, int match, int extended,
			 int length)
{
	if (match == 0)
		return 1;

	/* a length that adds a byte of its own is evidence on its own */
	if (!payload_bytes_zero(pkt, match, length))
		return 1;

	return (length == match + 1) && !extended;
}

/*
 * The length of an eSCO payload the caller has told us about, in bytes
 * including the CRC, or 0 if there is none to go on.
 *
 * maxlength is the longest payload the packet type allows, also including the
 * CRC, so a length this type cannot carry is no use and is ignored.
 */
static int known_esco_length(const btbb_packet* pkt, int maxlength)
{
	/* the caller counts information bytes; the two byte CRC is part of
	 * the length this library reports */
	if (pkt->esco_length < 1 || pkt->esco_length > maxlength - 2)
		return 0;

	return pkt->esco_length + 2;
}

/*
 * Find the length of an eSCO payload that carries no FEC, and decode it.
 *
 * The length is agreed when the link is set up and is not carried in the
 * packet (Vol 2, Part B, Section 6.5.3), so unless the caller has told us the
 * link's length the only way to find it is to unwhiten one byte at a time and
 * see which length the CRC agrees with. A length is worth trying as soon as
 * that many bytes have been decoded, so the byte decoded in each pass is the
 * last one its length needs.
 *
 * More than one length can agree, so the search keeps going to the end and
 * better_length() picks between them. Stopping at the first one dropped the
 * rest of a payload whose CRC ends in a zero byte, which is one payload in
 * 256, and the rest of one a shorter length agreed with by chance.
 *
 * maxlength is the longest payload the packet type allows, in bytes including
 * the CRC. The shortest is one information byte and the two byte CRC, which
 * payload_crc() enforces, so no length below three is tried here either.
 */
static int esco_payload(int clock, btbb_packet* pkt, int maxlength)
{
	/* skip the access code and packet header */
	char *stream = pkt->symbols + 122;

	/* number of symbols remaining after access code and packet header */
	int size = pkt->length - 122;

	/* the length the link was set up with, when the caller knows it: then
	 * that length is the answer and there is nothing to search for */
	int known = known_esco_length(pkt, maxlength);
	int last = known ? known : maxlength;

	/* the first length worth checking the CRC at: the one the caller
	 * knows, or the shortest this type carries */
	int first = known ? known : 3;

	/* the best length whose CRC agreed, or 0 if none has, and whether it
	 * was reached by better_length()'s zero byte rule */
	int match = 0;
	int extended = 0;

	/* number of bits we have decoded */
	int bits;

	for (pkt->payload_length = 1;
			pkt->payload_length <= last; pkt->payload_length++) {

		bits = (pkt->payload_length - 1) * 8;

		/* unwhiten next byte */
		if ((bits + 8) > size) {
			/*
			 * The capture ran out. A length that agreed before it
			 * did is still the payload, so report that one;
			 * otherwise the bytes decoded so far are all the
			 * payload there is to report. They are reported even
			 * below the shortest length this type carries, unlike
			 * a payload given up on: this says only that the
			 * capture stopped, so the bytes are real and the rest
			 * of the packet was simply never recorded.
			 */
			if (match)
				break;
			pkt->payload_length = bits / 8;
			return 1; //FIXME should throw exception
		}
		unwhiten(stream + bits, pkt->payload + bits, clock, 8,
			 HEADER_WHITENING_BITS + bits, pkt);

		if (pkt->payload_length >= first && payload_crc(pkt) &&
		    better_length(pkt, match, extended, pkt->payload_length)) {
			extended = match &&
				payload_bytes_zero(pkt, match,
						   pkt->payload_length);
			match = pkt->payload_length;
		}
	}

	if (match) {
		/* report the length that agreed, and check it again so that
		 * the flags payload_crc() records are about that length and
		 * not about the last one tried */
		pkt->payload_length = match;
		payload_crc(pkt);
		return 10;
	}

	if (known) {
		/* the caller's length stands whether the CRC agreed or not:
		 * an encrypted payload's CRC never does */
		pkt->payload_length = known;
		return 2;
	}

	/* no length matched, so report the longest, which is as far as the
	 * search decoded; the length the loop ended on is one byte past it */
	pkt->payload_length = maxlength;
	return 2;
}

int EV3(int clock, btbb_packet* pkt)
{
	return esco_payload(clock, pkt, EV3_MAX_PAYLOAD);
}

/*
 * The body of EV4(), which decodes a block at a time and so counts the bits
 * the error correction put right a block at a time. The running total goes in
 * corrected_total rather than on the packet: crc_check() calls this decoder
 * straight, once per candidate clock, and a count added to the packet's each
 * time would come out as the sum over every clock tried.
 */
static int ev4_payload(int clock, btbb_packet* pkt, int *corrected_total)
{
	char *corrected;

	/* skip the access code and packet header */
	char *stream = pkt->symbols + 122;

	/* number of symbols remaining after access code and packet header */
	int size = pkt->length - 122;

	/* the longest payload, which rate 2/3 FEC turns into 98 blocks of 15
	 * symbols */
	int maxbytes = EV4_MAX_PAYLOAD;
	int maxsymbols = fec23_symbols(maxbytes * 8);

	/* the shortest payload is one information byte and the two byte CRC,
	 * which comes to 45 symbols */
	int minbytes = 3;
	int minsymbols = fec23_symbols(minbytes * 8);

	/* the length the link was set up with, when the caller knows it: then
	 * that length is the answer and there is nothing to search for */
	int known = known_esco_length(pkt, maxbytes);
	int last = known ? known : maxbytes;

	int syms = 0; /* number of symbols we have decoded */
	int bits = 0; /* number of payload bits we have decoded */
	int fec_corrections = 0;

	/* the next length worth checking the CRC at */
	int candidate = known ? known : minbytes;

	/* the best length whose CRC agreed, or 0 if none has, whether it was
	 * reached by better_length()'s zero byte rule, and the corrections
	 * the error correction had made by the end of it */
	int match = 0;
	int extended = 0;
	int match_corrections = 0;

	*corrected_total = 0;

	pkt->payload_length = candidate;

	while (syms < maxsymbols) {

		/* unfec/unwhiten next block (15 symbols -> 10 bits) */
		if (syms + 15 > size) {
			/* The capture ran out partway through a block. A
			 * length that agreed before it did is still the
			 * payload; otherwise only whole bytes before it are a
			 * payload to report. */
			if (match)
				break;
			pkt->payload_length = bits / 8;
			return 1; //FIXME should throw exception
		}
		corrected = unfec23(stream + syms, 10, &fec_corrections);
		if (!corrected) {
			if (syms < minsymbols) {
				/* what is here is shorter than the shortest
				 * payload this type carries, so it is not one
				 * of these packets and there is no payload to
				 * report */
				pkt->payload_length = 0;
				*corrected_total = 0;
				return 0;
			}
			/*
			 * This block is lost, so the payload ends before it.
			 * The count stays positive: the published record
			 * layout gives a negative count the meaning "this
			 * many corrected and uncorrectable bits together",
			 * and the block that could not be put right is not
			 * part of the payload being reported.
			 *
			 * A block is 10 bits and the length reported is whole
			 * bytes, so up to two counted bits can belong to bits
			 * past the end of the payload. That is as close as a
			 * count made a block at a time can get.
			 */
			if (match)
				break;
			pkt->payload_length = bits / 8;
			return 1;
		}
		unwhiten(corrected, pkt->payload + bits, clock, 10,
			 HEADER_WHITENING_BITS + bits, pkt);
		free(corrected);

		*corrected_total += fec_corrections;

		syms += 15;
		bits += 10;

		/*
		 * Same search as esco_payload(), a block at a time: try the
		 * CRC at every length the bits decoded so far can hold, this
		 * block's included, and keep the longest that agrees.
		 */
		while (candidate <= last && candidate * 8 <= bits) {
			pkt->payload_length = candidate;
			if (payload_crc(pkt) &&
			    better_length(pkt, match, extended, candidate)) {
				extended = match &&
					payload_bytes_zero(pkt, match,
							   candidate);
				match = candidate;
				match_corrections = *corrected_total;
			}
			candidate++;
		}

		/* nothing longer left to try */
		if (candidate > last)
			break;
	}

	if (match) {
		/* Report the length that agreed, with the corrections the
		 * error correction had made by the end of the block that
		 * completed it, which is up to 9 bits past the length itself:
		 * the blocks after it are no part of the payload reported.
		 * Check the CRC again too, so that the flags payload_crc()
		 * records are about that length and not the last one tried. */
		pkt->payload_length = match;
		payload_crc(pkt);
		*corrected_total = match_corrections;
		return 10;
	}

	if (known) {
		/* the caller's length stands whether the CRC agreed or not:
		 * an encrypted payload's CRC never does */
		pkt->payload_length = known;
		return 2;
	}

	/* no length matched, so report the longest, which is as far as the
	 * search decoded; the length the loop ended on is one byte past it */
	pkt->payload_length = maxbytes;
	return 2;
}

int EV4(int clock, btbb_packet* pkt)
{
	int corrected = 0;
	int rv = ev4_payload(clock, pkt, &corrected);

	pkt->corrected_payload_bits = (int16_t) corrected;
	return rv;
}

int EV5(int clock, btbb_packet* pkt)
{
	return esco_payload(clock, pkt, EV5_MAX_PAYLOAD);
}

/* HV packet type payload parser */
int HV(int clock, btbb_packet* pkt)
{
	/* skip the access code and packet header */
	char *stream = pkt->symbols + 122;
	/* number of symbols remaining after access code and packet header */
	int size = pkt->length - 122;
	int fec_corrections = 0;

	pkt->payload_header_length = 0;
	/* nothing is decoded yet, so each case below sets the length once it
	 * has decoded a payload and the failure paths report none */
	pkt->payload_length = 0;
	if(size < 240)
		return 1; //FIXME should throw exception

	switch (pkt->packet_type) {
		case PACKET_TYPE_HV1:
			{
			char corrected[80];
			if (!unfec13(stream, corrected, 80, &fec_corrections))
				return 0;
			pkt->payload_length = 10;
			pkt->corrected_payload_bits = (int16_t) fec_corrections;
			unwhiten(corrected, pkt->payload, clock,
				 pkt->payload_length*8,
				 HEADER_WHITENING_BITS, pkt);
			}
			break;
		case PACKET_TYPE_HV2:
			{
			char *corrected = unfec23(stream, 160,
						  &fec_corrections);
			if (!corrected)
				return 0;
			pkt->payload_length = 20;
			pkt->corrected_payload_bits = (int16_t) fec_corrections;
			unwhiten(corrected, pkt->payload, clock,
				 pkt->payload_length*8,
				 HEADER_WHITENING_BITS, pkt);
			free(corrected);
			}
			break;
		case PACKET_TYPE_HV3:
			/* no FEC on this one, so nothing to correct */
			pkt->payload_length = 30;
			unwhiten(stream, pkt->payload, clock,
				 pkt->payload_length*8,
				 HEADER_WHITENING_BITS, pkt);
			break;
	}

	return 2;
}
/* try a clock value (CLK1-6) to unwhiten packet header,
 * sets resultant p->packet_type and p->UAP, returns UAP.
 */
uint8_t try_clock(int clock, btbb_packet* pkt)
{
	/* skip 72 bit access code */
	char *stream = pkt->symbols + 68;
	/* 18 bit packet header */
	char header[18];
	char unwhitened[18];

	/* check that we have enough symbols */
	if (pkt->length < 122)
		return 0;

	/* A trial clock, not the packet's own decode, so the corrections this
	 * one makes are not the packet's to report. */
	if (!unfec13(stream, header, 18, NULL))
		return 0;
	unwhiten(header, unwhitened, clock, 18, 0, pkt);
	uint16_t hdr_data = air_to_host16(unwhitened, 10);
	uint8_t hec = air_to_host8(&unwhitened[10], 8);
	pkt->UAP = uap_from_hec(hdr_data, hec);
	pkt->packet_type = air_to_host8(&unwhitened[3], 4);

	return pkt->UAP;
}

/* decode the packet header */
int btbb_decode_header(btbb_packet* pkt)
{
	/* skip 72 bit access code */
	char *stream = pkt->symbols + 68;
	/* 18 bit packet header */
	char header[18];
	uint8_t UAP;
	int header_corrections = 0;

	/* Nothing is known about this packet until it decodes, and a packet
	 * can be handed new symbols without its decoded state being reset, so
	 * drop every answer and the payload before the first return below.
	 * Without this a short capture after a decoded one is recorded with
	 * the earlier packet's payload and the checks that passed on it. */
	btbb_packet_set_flag(pkt, BTBB_HEC_CHECKED, 0);
	btbb_packet_set_flag(pkt, BTBB_HEC_VALID, 0);
	btbb_packet_set_flag(pkt, BTBB_CRC_CHECKED, 0);
	btbb_packet_set_flag(pkt, BTBB_CRC_CORRECT, 0);
	pkt->payload_length = 0;
	pkt->payload_header_length = 0;
	pkt->corrected_header_bits = 0;
	pkt->corrected_payload_bits = 0;

	/* check that we have enough symbols */
	if (pkt->length < 122)
		return 0;

	if (btbb_packet_get_flag(pkt, BTBB_CLK6_VALID) &&
	    unfec13(stream, header, 18, &header_corrections)) {
		pkt->corrected_header_bits = (uint8_t) header_corrections;
		unwhiten(header, pkt->packet_header, pkt->clkn, 18, 0, pkt);
		uint16_t hdr_data = air_to_host16(pkt->packet_header, 10);
		uint8_t hec = air_to_host8(&pkt->packet_header[10], 8);
		UAP = uap_from_hec(hdr_data, hec);

		/* Record the answer on the packet, so a capture file can say
		   whether the HEC was checked and whether it passed. */
		btbb_packet_set_flag(pkt, BTBB_HEC_CHECKED, 1);
		btbb_packet_set_flag(pkt, BTBB_HEC_VALID, UAP == pkt->UAP);

		if (UAP == pkt->UAP) {
			pkt->packet_lt_addr = air_to_host8(&pkt->packet_header[0], 3);
			pkt->packet_type = air_to_host8(&pkt->packet_header[3], 4);
			pkt->packet_flags = air_to_host8(&pkt->packet_header[7], 3);
			pkt->packet_hec = hec;
			return 1;
		}
	}

	return 0;
}

int btbb_decode_payload(btbb_packet* pkt)
{
	int rv = 0;
	pkt->payload_header_length = 0;

	/* Nothing is known about this payload's CRC until one of the type
	 * decoders below checks it, and the types whose payload has no CRC
	 * never will. btbb_decode_header() has already cleared these in
	 * every flow the library itself takes; this is for a caller that
	 * changes the packet type between two calls here. */
	btbb_packet_set_flag(pkt, BTBB_CRC_CHECKED, 0);
	btbb_packet_set_flag(pkt, BTBB_CRC_CORRECT, 0);

	/* and nothing has been corrected yet either: the count belongs to the
	 * payload the decoder below settles on */
	pkt->corrected_payload_bits = 0;

	switch(pkt->packet_type)
	{
		case PACKET_TYPE_NULL:
			/* no payload to decode */
			pkt->payload_length = 0;
			rv = 1;
			break;
		case PACKET_TYPE_POLL:
			/* no payload to decode */
			pkt->payload_length = 0;
			rv = 1;
			break;
		case PACKET_TYPE_FHS:
			rv = fhs(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_DM1:
			rv = DM(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_DH1:
			/* assuming DH1 but could be 2-DH1 */
			rv = DH(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_HV1:
			rv = HV(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_HV2:
			rv = HV(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_HV3: /* HV3/EV3/3-EV3 */
			/*
			 * An HV3 and an EV3 share this type code, so what
			 * each can carry has to tell them apart. An EV3
			 * payload ends in a CRC; an HV3 is 30 voice bytes
			 * with no CRC at all (Vol 2, Part B, Section
			 * 6.5.2.3). So a CRC that agrees at some length says
			 * EV3, and anything else - including a search that
			 * found no length it agreed at - leaves the HV3 this
			 * is the right size to be.
			 *
			 * A caller that knows the link carries eSCO payloads
			 * has said so, and then this is an EV3 of that length
			 * whether its CRC agreed or not: an encrypted
			 * payload's CRC never does.
			 */
			rv = EV3(pkt->clkn, pkt);
			if (rv < 10 && !known_esco_length(pkt, EV3_MAX_PAYLOAD))
				rv = HV(pkt->clkn, pkt);
			/* don't know how to decode 3-EV3 */
			break;
		case PACKET_TYPE_DV:
			/* assuming DV but could be 3-DH1 */
			rv = DM(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_AUX1:
			rv = DH(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_DM3:
			/* assuming DM3 but could be 2-DH3 */
			rv = DM(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_DH3:
			/* assuming DH3 but could be 3-DH3 */
			rv = DH(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_EV4:
			/* assuming EV4 but could be 2-EV5 */
			rv = EV4(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_EV5:
			/* assuming EV5 but could be 3-EV5 */
			rv = EV5(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_DM5:
			/* assuming DM5 but could be 2-DH5 */
			rv = DM(pkt->clkn, pkt);
			break;
		case PACKET_TYPE_DH5:
			/* assuming DH5 but could be 3-DH5 */
			rv = DH(pkt->clkn, pkt);
			break;
	}

	/* These types search for their payload: the eSCO ones try every
	 * candidate length and an FHS tries all 32 whitening inputs, checking
	 * the CRC at each. Only a CRC that came out describes what the decoder
	 * settled on; any other answer is about an attempt it abandoned, or
	 * about a voice payload with no CRC at all. Drop it, or a capture file
	 * reports a CRC failure the packet never had. */
	if ((rv < 10) && ((pkt->packet_type == PACKET_TYPE_FHS) ||
			  (pkt->packet_type == PACKET_TYPE_HV3) ||
			  (pkt->packet_type == PACKET_TYPE_EV4) ||
			  (pkt->packet_type == PACKET_TYPE_EV5))) {
		btbb_packet_set_flag(pkt, BTBB_CRC_CHECKED, 0);
		btbb_packet_set_flag(pkt, BTBB_CRC_CORRECT, 0);
	}

	/* The flag says the payload field holds a payload this packet
	 * decoded, which is what btbb_print_packet() and the capture file
	 * writers report. Each decoder above reports the length it decoded,
	 * and none where it gave up: a payload header claiming more than the
	 * type can carry, a capture that ended before the payload did, a
	 * block the error correction could not put right, or no whitening
	 * input whose CRC came out. A NULL or POLL has no payload to decode
	 * and so reports none either.
	 *
	 * Without this the flag said every packet had a payload, and the
	 * bytes reported for one that decoded none were whatever the last
	 * packet this object decoded left in the field: a packet object is
	 * reused across packets by btbb_find_ac(). The field is cleared here
	 * so a caller that reads it past the flag cannot see them. */
	if (pkt->payload_length > 0) {
		btbb_packet_set_flag(pkt, BTBB_HAS_PAYLOAD, 1);
	}
	else {
		btbb_packet_set_flag(pkt, BTBB_HAS_PAYLOAD, 0);
		memset(pkt->payload, 0, sizeof(pkt->payload));
	}

	return rv;
}

/* decode the whole packet */
int btbb_decode(btbb_packet* pkt)
{
	int rv = 0;

	btbb_packet_set_flag(pkt, BTBB_HAS_PAYLOAD, 0);

	if (btbb_decode_header(pkt)) {
		rv =  btbb_decode_payload(pkt);
	}

	/* Decoding prints nothing. A caller that wants the packet reported
	 * calls btbb_print_packet(), which btbb_process_packet() does for a
	 * packet of a piconet it is following. This used to print the packet
	 * itself whenever one decoded, so a followed packet was printed
	 * twice, and any other caller got a dump on stdout it had not asked
	 * for. */
	return rv;
}

/* print packet information */
void btbb_print_packet(const btbb_packet* pkt)
{
	/* The type, and the payload header's fields where one was read, come
	 * from the decoded packet header, which is what a valid HEC says the
	 * library has. A packet whose payload did not decode still has a type
	 * worth printing; what it does not have is data. */
	if (btbb_packet_get_flag(pkt, BTBB_HEC_VALID)) {
		printf("  Type: %s\n", TYPE_NAMES[pkt->packet_type]);
		if (pkt->payload_header_length > 0) {
			printf("  LT_ADDR: %d\n", pkt->packet_lt_addr);
			printf("  LLID: %d\n", pkt->payload_llid);
			printf("  flow: %d\n", pkt->payload_flow);
			printf("  payload length: %d\n", pkt->payload_length);
		}
		if (btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD)) {
			printf("  Data: ");
			int i;
			for(i=0; i<pkt->payload_length; i++)
				printf(" %02x", air_to_host8(pkt->payload + 8*i, 8));
			printf("\n");
		}
	}
}

char *tun_format(btbb_packet* pkt)
{
	/* include 6 bytes for meta data, 3 bytes for packet header */
	int length = 9 + pkt->payload_length;
	char *tun_format = (char *) malloc(length);
	int i;

	if (tun_format == NULL)
		return NULL;

	/* meta data */
	tun_format[0] = pkt->clkn & 0xff;
	tun_format[1] = (pkt->clkn >> 8) & 0xff;
	tun_format[2] = (pkt->clkn >> 16) & 0xff;
	tun_format[3] = (pkt->clkn >> 24) & 0xff;
	tun_format[4] = pkt->channel;
	tun_format[5] = btbb_packet_get_flag(pkt, BTBB_CLK27_VALID) |
		(btbb_packet_get_flag(pkt, BTBB_NAP_VALID) << 1);

	/* packet header modified to fit byte boundaries */
	/* lt_addr and type */
	tun_format[6] = (char) air_to_host8(&pkt->packet_header[0], 7);
	/* flags */
	tun_format[7] = (char) air_to_host8(&pkt->packet_header[7], 3);
	/* HEC */
	tun_format[8] = (char) air_to_host8(&pkt->packet_header[10], 8);

	for(i=0;i<pkt->payload_length;i++)
		tun_format[i+9] = (char) air_to_host8(&pkt->payload[i*8], 8);

	return tun_format;
}

/* check to see if the packet has a header */
int btbb_header_present(const btbb_packet* pkt)
{
	/* skip to last bit of sync word */
	const char *stream = pkt->symbols + 63;
	int be = 0; /* bit errors */
	char msb;   /* most significant (last) bit of sync word */
	int a, b, c;

	/* check that we have enough symbols */
	if (pkt->length < 122)
		return 0;

	/* check that the AC trailer is correct */
	msb = stream[0];
	be += stream[1] ^ !msb;
	be += stream[2] ^ msb;
	be += stream[3] ^ !msb;
	be += stream[4] ^ msb;

	/*
	 * Each bit of the 18 bit header is repeated three times.  Without
	 * checking the correctness of any particular bit, just count the
	 * number of times three symbols in a row don't all agree.
	 */
	stream += 5;
	for (a = 0; a < 54; a += 3) {
		b = a + 1;
		c = a + 2;
		be += ((stream[a] ^ stream[b]) |
			(stream[b] ^ stream[c]) | (stream[c] ^ stream[a]));
	}

	/*
	 * Few bit errors indicates presence of a header.  Many bit errors
	 * indicates no header is present (i.e. it is an ID packet).
	 */
	return (be < ID_THRESHOLD);
}

/* extract LAP from FHS payload */
uint32_t lap_from_fhs(btbb_packet* pkt)
{
	/* caller should check got_payload() and get_type() */
	return air_to_host32(&pkt->payload[34], 24);
}

/* extract UAP from FHS payload */
uint8_t uap_from_fhs(btbb_packet* pkt)
{
	/* caller should check got_payload() and get_type() */
	return air_to_host8(&pkt->payload[64], 8);
}

/* extract NAP from FHS payload */
uint16_t nap_from_fhs(btbb_packet* pkt)
{
	/* caller should check got_payload() and get_type() */
	return air_to_host16(&pkt->payload[72], 16);
}

/* extract clock from FHS payload */
uint32_t clock_from_fhs(btbb_packet* pkt)
{
	/*
	 * caller should check got_payload() and get_type()
	 *
	 * This is CLK2-27 (units of 1.25 ms).
	 * CLK0 and CLK1 are implicitly zero.
	 */
	return air_to_host32(&pkt->payload[115], 26);
}
