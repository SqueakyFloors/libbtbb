/*
 * The FEC 2/3 code, the packet header decode and the sync word syndrome
 * tables, ported from the old tests/ programs that nothing built.
 *
 * These functions are internal to the library and have no external linkage,
 * so the test compiles the translation unit they live in.
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

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bluetooth_packet.c"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/*
 * One FEC 2/3 block: 10 data bits and 5 check bits, air order. The first ten
 * cases carry one data bit each and are error free; the last ten have that
 * data bit cleared, so the check bits disagree and unfec23() has to put it
 * back.
 */
static const char FEC23_INPUT[20][15] = {
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0},
	{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1},
	{0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0},
	{0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
	{0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1},
	{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1},
};

static const char FEC23_EXPECTED[20][10] = {
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 1, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 1, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 1, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 1, 0, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
};

static void test_unfec23(void)
{
	int i, bit;

	for (i = 0; i < 20; ++i) {
		char input[15];
		char *output;
		int corrected = -1;

		memcpy(input, FEC23_INPUT[i], sizeof(input));
		output = unfec23(input, 10, &corrected);
		CHECK(output != NULL);
		if (output == NULL)
			continue;

		/* the first ten cases arrived intact; the last ten each have
		 * one wrong data bit, which a capture file records as one
		 * corrected bit */
		CHECK(corrected == (i < 10 ? 0 : 1));

		for (bit = 0; bit < 10; ++bit) {
			if (output[bit] != FEC23_EXPECTED[i][bit]) {
				printf("FAIL case %d: bit %d is %d, expected %d\n",
				       i, bit, output[bit],
				       FEC23_EXPECTED[i][bit]);
				++failures;
				break;
			}
		}
		free(output);
	}
}

/*
 * The first ten vectors above are the codewords for the ten single-bit data
 * values, so they say what fec23() must produce without asking unfec23(),
 * which computes parity with fec23() itself.
 */
static void test_fec23_against_vectors(void)
{
	int i, bit;

	for (i = 0; i < 10; ++i) {
		uint16_t expected = 0;

		for (bit = 0; bit < 15; ++bit)
			expected |= (uint16_t)
				((FEC23_INPUT[i][bit] & 1) << bit);

		CHECK(fec23((uint16_t) (1 << i)) == expected);
	}
}

/* What fec23() encodes, unfec23() decodes back. */
static void test_fec23_round_trip(void)
{
	static const uint16_t DATA[] = {
		0x000, 0x001, 0x200, 0x155, 0x2aa, 0x3ff, 0x123, 0x2f0,
	};
	size_t i;
	int bit;

	for (i = 0; i < sizeof(DATA) / sizeof(DATA[0]); ++i) {
		uint16_t codeword = fec23(DATA[i]);
		char block[15];
		char *output;
		int corrected = -1;

		for (bit = 0; bit < 15; ++bit)
			block[bit] = (char) ((codeword >> bit) & 1);

		output = unfec23(block, 10, &corrected);
		CHECK(output != NULL);
		if (output == NULL)
			continue;

		CHECK(corrected == 0);
		for (bit = 0; bit < 10; ++bit)
			CHECK(output[bit] == (char) ((DATA[i] >> bit) & 1));
		free(output);
	}
}

/* Two FEC 2/3 blocks at once, and a length that is not a multiple of ten. */
static void test_unfec23_multiple_blocks(void)
{
	static const uint16_t DATA[2] = { 0x155, 0x2aa };
	char blocks[30];
	char *output;
	int block, bit;
	int corrected = -1;

	for (block = 0; block < 2; ++block) {
		uint16_t codeword = fec23(DATA[block]);

		for (bit = 0; bit < 15; ++bit)
			blocks[block * 15 + bit] =
				(char) ((codeword >> bit) & 1);
	}

	output = unfec23(blocks, 20, &corrected);
	CHECK(output != NULL);
	CHECK(corrected == 0);
	if (output != NULL) {
		for (bit = 0; bit < 10; ++bit) {
			CHECK(output[bit] == (char) ((DATA[0] >> bit) & 1));
			CHECK(output[10 + bit] ==
			      (char) ((DATA[1] >> bit) & 1));
		}
		free(output);
	}

	/* 16 bits of data still means two blocks, padded out to 20 */
	output = unfec23(blocks, 16, &corrected);
	CHECK(output != NULL);
	CHECK(corrected == 0);
	if (output != NULL) {
		for (bit = 0; bit < 10; ++bit)
			CHECK(output[bit] == (char) ((DATA[0] >> bit) & 1));
		for (bit = 0; bit < 6; ++bit)
			CHECK(output[10 + bit] ==
			      (char) ((DATA[1] >> bit) & 1));
		free(output);
	}
}

/* A wrong check bit says nothing about the data, which comes back as sent. */
static void test_unfec23_check_bit_error(void)
{
	uint16_t codeword = fec23(0x155);
	char block[15];
	char *output;
	int bit;
	int corrected = -1;

	for (bit = 0; bit < 15; ++bit)
		block[bit] = (char) ((codeword >> bit) & 1);
	block[12] ^= 1;

	output = unfec23(block, 10, &corrected);
	CHECK(output != NULL);
	/* the wrong bit is still an errored bit the code dealt with */
	CHECK(corrected == 1);
	if (output != NULL) {
		for (bit = 0; bit < 10; ++bit)
			CHECK(output[bit] == (char) ((0x155 >> bit) & 1));
		free(output);
	}
}

/* Two wrong data bits are beyond what the code can correct. */
static void test_unfec23_rejects_two_bit_errors(void)
{
	uint16_t codeword = fec23(0x155);
	char block[15];
	int bit;
	int corrected = -1;

	for (bit = 0; bit < 15; ++bit)
		block[bit] = (char) ((codeword >> bit) & 1);
	block[0] ^= 1;
	block[1] ^= 1;

	CHECK(unfec23(block, 10, &corrected) == NULL);
	/* nothing came out, so there is nothing to report corrected */
	CHECK(corrected == 0);
}

/*
 * Packet headers, from the table test_header.c carried in a comment: the UAP,
 * the 10 header data bits, the HEC, and the 18 bits FEC 1/3 encoded as 54
 * symbols, written in octal. A 7 is three ones and a 0 is three zeros, which
 * is what three like symbols per bit comes to.
 */
static const struct {
	uint8_t uap;
	uint16_t data;
	uint8_t hec;
	const char *header_octal;
} HEADERS[] = {
	{ 0x00, 0x123, 0xe1, "770007" "007070" "000777" },
	{ 0x47, 0x123, 0x06, "770007" "007007" "700000" },
	{ 0x00, 0x124, 0x32, "007007" "007007" "007700" },
	{ 0x47, 0x124, 0xd5, "007007" "007070" "707077" },
	{ 0x00, 0x125, 0x5a, "707007" "007007" "077070" },
	{ 0x47, 0x125, 0xbd, "707007" "007070" "777707" },
	{ 0x00, 0x126, 0xe2, "077007" "007007" "000777" },
	{ 0x47, 0x126, 0x05, "077007" "007070" "700000" },
	{ 0x00, 0x127, 0x8a, "777007" "007007" "070007" },
	{ 0x47, 0x127, 0x6d, "777007" "007070" "770770" },
	{ 0x00, 0x11b, 0x9e, "770770" "007007" "777007" },
	{ 0x47, 0x11b, 0x79, "770770" "007070" "077770" },
	{ 0x00, 0x11c, 0x4d, "007770" "007070" "770070" },
	{ 0x47, 0x11c, 0xaa, "007770" "007007" "070707" },
	{ 0x00, 0x11d, 0x25, "707770" "007070" "700700" },
	{ 0x47, 0x11d, 0xc2, "707770" "007007" "000077" },
	{ 0x00, 0x11e, 0x9d, "077770" "007070" "777007" },
	{ 0x47, 0x11e, 0x7a, "077770" "007007" "077770" },
	{ 0x00, 0x11f, 0xf5, "777770" "007070" "707777" },
	{ 0x47, 0x11f, 0x12, "777770" "007007" "007000" },
};

/* where the packet header sits in a symbol stream, after the access code */
#define HEADER_OFFSET 68
#define HEADER_SYMBOLS 54

static void test_header_decode(void)
{
	size_t row;
	int digit, repeat;

	for (row = 0; row < sizeof(HEADERS) / sizeof(HEADERS[0]); ++row) {
		char stream[HEADER_OFFSET + HEADER_SYMBOLS];
		char header[18];
		btbb_packet *pkt;
		int corrected = -1;

		memset(stream, 0, sizeof(stream));
		for (digit = 0; digit < 18; ++digit) {
			char bit = HEADERS[row].header_octal[digit] == '7';

			for (repeat = 0; repeat < 3; ++repeat)
				stream[HEADER_OFFSET + digit * 3 + repeat] = bit;
		}

		/* the FEC and the HEC, one step at a time */
		CHECK(unfec13(stream + HEADER_OFFSET, header, 18,
			      &corrected));
		CHECK(corrected == 0);
		CHECK(air_to_host16(header, 10) == HEADERS[row].data);
		CHECK(air_to_host8(&header[10], 8) == HEADERS[row].hec);
		CHECK(uap_from_hec(HEADERS[row].data, HEADERS[row].hec) ==
		      HEADERS[row].uap);

		/* and through the public entry point, with whitening off */
		pkt = btbb_packet_new();
		CHECK(pkt != NULL);
		if (pkt == NULL)
			continue;
		btbb_packet_set_data(pkt, stream, (int) sizeof(stream), 0, 0);
		btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
		btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
		btbb_packet_set_uap(pkt, HEADERS[row].uap);

		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(btbb_packet_get_hec(pkt) == HEADERS[row].hec);
		CHECK(btbb_packet_get_lt_addr(pkt) ==
		      (HEADERS[row].data & 0x7));
		CHECK(btbb_packet_get_type(pkt) ==
		      ((HEADERS[row].data >> 3) & 0xf));
		CHECK(btbb_packet_get_header_flags(pkt) ==
		      ((HEADERS[row].data >> 7) & 0x7));
		/* an intact header, so no bit of it was corrected */
		CHECK(pkt->corrected_header_bits == 0);

		/*
		 * One wrong symbol in a triple is outvoted by the other two,
		 * wherever in the triple it is: first, middle and last. Three
		 * triples are wrecked here, one bit each, which is what a
		 * capture file records as three corrected header bits.
		 */
		stream[HEADER_OFFSET + 0] ^= 1;
		stream[HEADER_OFFSET + 4] ^= 1;
		stream[HEADER_OFFSET + 8] ^= 1;
		CHECK(unfec13(stream + HEADER_OFFSET, header, 18,
			      &corrected));
		CHECK(corrected == 3);
		CHECK(air_to_host16(header, 10) == HEADERS[row].data);
		CHECK(air_to_host8(&header[10], 8) == HEADERS[row].hec);

		/* and the packet carries that count for the writers */
		btbb_packet_set_data(pkt, stream, (int) sizeof(stream), 0, 0);
		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(pkt->corrected_header_bits == 3);

		stream[HEADER_OFFSET + 0] ^= 1;
		stream[HEADER_OFFSET + 4] ^= 1;
		stream[HEADER_OFFSET + 8] ^= 1;

		/* the wrong UAP is not accepted */
		btbb_packet_set_uap(pkt, (uint8_t) (HEADERS[row].uap ^ 0xff));
		CHECK(btbb_decode_header(pkt) == 0);

		btbb_packet_unref(pkt);
	}
}

/*
 * A sync word is a (64,30) codeword, so an error free one has a zero
 * syndrome. The second value differs from the default access code in two
 * bits.
 */
static void test_gen_syndrome(void)
{
	CHECK(gen_syndrome(0xcc7b7268ff614e1bULL) == 0);
	CHECK(gen_syndrome(0xcc7d7268ff614e1bULL) == 0x0000000299c6f9b5ULL);
}

/* The syndrome map turns a syndrome back into the bits that are wrong. */
static void test_syndrome_map(void)
{
	static const uint64_t ERRORS[] = {
		1ULL,			/* one bit */
		1ULL << 57,
		(1ULL << 3) | (1ULL << 40),	/* two bits */
	};
	size_t i;

	gen_syndrome_map(2);

	for (i = 0; i < sizeof(ERRORS) / sizeof(ERRORS[0]); ++i) {
		uint64_t syndrome = gen_syndrome(DEFAULT_AC ^ ERRORS[i]);
		syndrome_struct *found = find_syndrome(syndrome);

		CHECK(found != NULL);
		if (found != NULL)
			CHECK(found->error == ERRORS[i]);
	}

	/* three wrong bits are past the map built above */
	CHECK(find_syndrome(gen_syndrome(DEFAULT_AC ^ 0x7ULL)) == NULL);
}

/*
 * The access code search. These cases run it against a stream that ends at a
 * page the process may not read, so a search that goes past the symbols it
 * was given does not merely read someone else's memory, it stops the test.
 */
#define TEST_LAP 0x345678U

/* Write the first 'symbols' symbols of a sync word into a symbol stream, air
 * order: symbol i is bit i. */
static void plant_syncword(char *at, uint64_t syncword, int symbols)
{
	int i;

	for (i = 0; i < symbols; i++)
		at[i] = (char) ((syncword >> i) & 1);
}

/*
 * A stream of 'len' symbols whose last symbol is the last byte of a readable
 * page, with an unreadable page after it. Returns NULL where the mapping
 * cannot be made, which leaves the caller nothing to test rather than a
 * failure.
 */
static char *guarded_stream(size_t len, char **mapping, size_t *mapping_len)
{
	long page = sysconf(_SC_PAGESIZE);
	size_t pages;
	char *base;

	if (page <= 0 || len > (size_t) page)
		return NULL;
	pages = 2 * (size_t) page;
	base = mmap(NULL, pages, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		return NULL;
	if (mprotect(base + (size_t) page, (size_t) page, PROT_NONE) != 0) {
		munmap(base, pages);
		return NULL;
	}
	*mapping = base;
	*mapping_len = pages;
	memset(base, 0, (size_t) page);
	return base + (size_t) page - len;
}

static void test_find_ac_reads_only_the_stream_it_is_given(void)
{
	const size_t len = 200;
	char *mapping = NULL;
	size_t mapping_len = 0;
	char *stream = guarded_stream(len, &mapping, &mapping_len);
	const uint64_t syncword = btbb_gen_syncword((int) TEST_LAP);
	/* the last offset the stream holds a whole sync word at */
	const int last = (int) (len - SYNCWORD_SYMBOLS);
	btbb_packet *pkt = NULL;

	CHECK(stream != NULL);
	if (stream == NULL)
		return;

	/*
	 * A search length far beyond the stream, which is what the caller
	 * cannot be checked on: the offsets past 'last' hold no whole sync
	 * word, and trying them would read the guarded page.
	 */
	plant_syncword(stream + last, syncword, SYNCWORD_SYMBOLS);
	CHECK(btbb_find_ac_n(stream, len, 100000, TEST_LAP, 0, &pkt) == last);
	CHECK(pkt != NULL);
	if (pkt != NULL) {
		CHECK(btbb_packet_get_lap(pkt) == TEST_LAP);
		CHECK(btbb_packet_get_ac_errors(pkt) == 0);
		btbb_packet_unref(pkt);
		pkt = NULL;
	}
	/* the same offset, found with no help from the LAP */
	CHECK(btbb_find_ac_n(stream, len, 100000, LAP_ANY, 0, &pkt) == last);
	if (pkt != NULL) {
		CHECK(btbb_packet_get_lap(pkt) == TEST_LAP);
		btbb_packet_unref(pkt);
		pkt = NULL;
	}

	/*
	 * A sync word whose first symbols are in the stream and whose last
	 * are not. It is not there to be found: the symbols that would decide
	 * it are not the caller's.
	 */
	memset(stream + last, 0, SYNCWORD_SYMBOLS);
	plant_syncword(stream + last + 1, syncword, SYNCWORD_SYMBOLS - 1);
	CHECK(btbb_find_ac_n(stream, len, 100000, TEST_LAP, 0, &pkt) < 0);
	CHECK(btbb_find_ac_n(stream, len, 100000, LAP_ANY, 0, &pkt) < 0);
	CHECK(pkt == NULL);

	/*
	 * A stream shorter than one sync word, and an empty one, hold no
	 * offset to search at all. Here the stream starts at the guarded
	 * page, so any symbol read would stop the test.
	 */
	stream = mapping + mapping_len / 2;
	CHECK(btbb_find_ac_n(stream, 0, 100000, TEST_LAP, 0, &pkt) < 0);
	CHECK(btbb_find_ac_n(stream, 0, 100000, LAP_ANY, 0, &pkt) < 0);
	CHECK(btbb_find_ac_n(stream, SYNCWORD_SYMBOLS - 1, 100000, TEST_LAP, 0,
			     &pkt) < 0);
	CHECK(btbb_find_ac_n(stream, SYNCWORD_SYMBOLS - 1, 100000, LAP_ANY, 0,
			     &pkt) < 0);
	CHECK(pkt == NULL);

	munmap(mapping, mapping_len);
}

/*
 * The form without a length reads what its search length asks for, which is
 * what it has always read, so the two forms search the same offsets when the
 * length is the one that form implies.
 */
static void test_find_ac_without_a_length_searches_the_same_offsets(void)
{
	char stream[300];
	const uint64_t syncword = btbb_gen_syncword((int) TEST_LAP);
	btbb_packet *pkt = NULL;
	int search_length;

	memset(stream, 0, sizeof(stream));
	plant_syncword(stream + 120, syncword, SYNCWORD_SYMBOLS);

	/* reaching the sync word, and stopping one offset short of it */
	for (search_length = 121; search_length >= 120; search_length--) {
		const size_t len = (size_t) search_length +
			(SYNCWORD_SYMBOLS - 1);
		const int expected = search_length > 120 ? 120 : -1;

		CHECK(btbb_find_ac(stream, search_length, TEST_LAP, 0, &pkt) ==
		      expected);
		CHECK(btbb_find_ac_n(stream, len, search_length, TEST_LAP, 0,
				     &pkt) == expected);
		CHECK(btbb_find_ac(stream, search_length, LAP_ANY, 0, &pkt) ==
		      expected);
		CHECK(btbb_find_ac_n(stream, len, search_length, LAP_ANY, 0,
				     &pkt) == expected);
	}
	if (pkt != NULL)
		btbb_packet_unref(pkt);

	/* nothing is searched for, and nothing read, at either entry point */
	pkt = NULL;
	CHECK(btbb_find_ac(NULL, 0, TEST_LAP, 0, &pkt) < 0);
	CHECK(btbb_find_ac(NULL, -1, LAP_ANY, 0, &pkt) < 0);
	CHECK(btbb_find_ac_n(NULL, 0, 0, TEST_LAP, 0, &pkt) < 0);
	CHECK(btbb_find_ac_n(NULL, 0, -1, LAP_ANY, 0, &pkt) < 0);
	CHECK(pkt == NULL);
}

int main(void)
{
	test_unfec23();
	test_unfec23_multiple_blocks();
	test_unfec23_check_bit_error();
	test_unfec23_rejects_two_bit_errors();
	test_fec23_against_vectors();
	test_fec23_round_trip();
	test_header_decode();
	test_gen_syndrome();
	test_syndrome_map();
	test_find_ac_reads_only_the_stream_it_is_given();
	test_find_ac_without_a_length_searches_the_same_offsets();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
