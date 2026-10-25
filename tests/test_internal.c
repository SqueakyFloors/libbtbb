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
		output = unfec23(input, 10);
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

/* Two FEC 2/3 blocks at once, and a length that is not a multiple of ten. */
static void test_unfec23_multiple_blocks(void)
{
	static const uint16_t DATA[2] = { 0x155, 0x2aa };
	char blocks[30];
	char *output;
	int block, bit;

	for (block = 0; block < 2; ++block) {
		uint16_t codeword = fec23(DATA[block]);

		for (bit = 0; bit < 15; ++bit)
			blocks[block * 15 + bit] =
				(char) ((codeword >> bit) & 1);
	}

	output = unfec23(blocks, 20);
	CHECK(output != NULL);
	if (output != NULL) {
		for (bit = 0; bit < 10; ++bit) {
			CHECK(output[bit] == (char) ((DATA[0] >> bit) & 1));
			CHECK(output[10 + bit] ==
			      (char) ((DATA[1] >> bit) & 1));
		}
		free(output);
	}

	/* 16 bits of data still means two blocks, padded out to 20 */
	output = unfec23(blocks, 16);
	CHECK(output != NULL);
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

	for (bit = 0; bit < 15; ++bit)
		block[bit] = (char) ((codeword >> bit) & 1);
	block[12] ^= 1;

	output = unfec23(block, 10);
	CHECK(output != NULL);
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

	for (bit = 0; bit < 15; ++bit)
		block[bit] = (char) ((codeword >> bit) & 1);
	block[0] ^= 1;
	block[1] ^= 1;

	CHECK(unfec23(block, 10) == NULL);
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

		memset(stream, 0, sizeof(stream));
		for (digit = 0; digit < 18; ++digit) {
			char bit = HEADERS[row].header_octal[digit] == '7';

			for (repeat = 0; repeat < 3; ++repeat)
				stream[HEADER_OFFSET + digit * 3 + repeat] = bit;
		}

		/* the FEC and the HEC, one step at a time */
		CHECK(unfec13(stream + HEADER_OFFSET, header, 18));
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

		/*
		 * One wrong symbol in a triple is outvoted by the other two,
		 * wherever in the triple it is: first, middle and last.
		 */
		stream[HEADER_OFFSET + 0] ^= 1;
		stream[HEADER_OFFSET + 4] ^= 1;
		stream[HEADER_OFFSET + 8] ^= 1;
		CHECK(unfec13(stream + HEADER_OFFSET, header, 18));
		CHECK(air_to_host16(header, 10) == HEADERS[row].data);
		CHECK(air_to_host8(&header[10], 8) == HEADERS[row].hec);
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

int main(void)
{
	test_unfec23();
	test_unfec23_multiple_blocks();
	test_unfec23_check_bit_error();
	test_unfec23_rejects_two_bit_errors();
	test_fec23_round_trip();
	test_header_decode();
	test_gen_syndrome();
	test_syndrome_map();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
