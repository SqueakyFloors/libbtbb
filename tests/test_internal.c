/*
 * The FEC 2/3 code and the sync word syndrome tables, ported from the old
 * tests/ programs that nothing built.
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

		memcpy(input, FEC23_INPUT[i], sizeof(input));
		output = unfec23(input, 1);
		CHECK(output != NULL);
		if (output == NULL)
			continue;

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

		for (bit = 0; bit < 15; ++bit)
			block[bit] = (char) ((codeword >> bit) & 1);

		output = unfec23(block, 10);
		CHECK(output != NULL);
		if (output == NULL)
			continue;

		for (bit = 0; bit < 10; ++bit)
			CHECK(output[bit] == (char) ((DATA[i] >> bit) & 1));
		free(output);
	}
}

/* Two wrong data bits are beyond what the code can correct. */
static void test_unfec23_rejects_two_bit_errors(void)
{
	uint16_t codeword = fec23(0x155);
	char block[15];
	int bit;

	for (bit = 0; bit < 15; ++bit)
		block[bit] = (char) ((codeword >> bit) & 1);
	block[0] ^= 1;
	block[1] ^= 1;

	CHECK(unfec23(block, 10) == NULL);
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

int main(void)
{
	test_unfec23();
	test_fec23_round_trip();
	test_unfec23_rejects_two_bit_errors();
	test_gen_syndrome();
	test_syndrome_map();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
