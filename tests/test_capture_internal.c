/*
 * The two record assemblers, called directly so the test owns the memory they
 * write into. tests/test_capture.c goes through the public writers and reads
 * the file back, which cannot say what a record's padding bytes held before
 * the record was built; here they are set to a known pattern first.
 *
 * The assemblers are internal to the library, so this compiles the two
 * translation units they live in, as tests/test_internal.c does, and links
 * the library for the rest.
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
#include <string.h>

#include "pcapng.c"
#include "pcapng-bt.c"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/* The pattern the record is filled with before it is built. Any of it left in
   the block would reach a capture file. */
#define SCRIBBLE 0xaa

/* Every byte in [from, to) of the record is zero. */
static int all_zero(const void *record, size_t from, size_t to)
{
	const uint8_t *bytes = (const uint8_t *) record;
	size_t i;

	for (i = from; i < to; i++) {
		if (bytes[i] != 0)
			return 0;
	}
	return 1;
}

/*
 * A BR/EDR block is 28 bytes of block header, a 22-byte pseudoheader and the
 * payload, rounded up to a 4-byte boundary, then the options word and the
 * trailing length. Whatever the payload length, the bytes between the payload
 * and the options word must be zero.
 */
static void test_bredr_padding_is_zero(void)
{
	static const uint8_t PAYLOAD[400];
	uint32_t caplen;

	for (caplen = 0; caplen <= 8; caplen++) {
		pcapng_bredr_packet rec;
		uint32_t pcapng_caplen = 22 + caplen;
		uint32_t block_length =
			PCAPNG_BLOCK_LENGTH(pcapng_caplen);
		size_t data_end = 28 + pcapng_caplen;

		memset(&rec, SCRIBBLE, sizeof(rec));
		assemble_pcapng_bredr_packet(&rec, 0, 7, caplen, 0, -20, -80,
					     0, 0, 0, 0, 0, 0x9e8b33, 0x9e8b33,
					     0x47, 0x1923, BREDR_DEWHITENED,
					     (const char *) PAYLOAD);

		CHECK(rec.blk_header.block_total_length == block_length);
		CHECK(rec.blk_header.captured_len == pcapng_caplen);
		/* the padding, the options word and the trailing length */
		CHECK(all_zero(&rec, data_end, block_length - 4));
		CHECK(((const uint32_t *) &rec)[block_length / 4 - 1] ==
		      block_length);
	}
}

/* The same for an LE record: 28 bytes of block header, a 10-byte
   pseudoheader and the captured frame. */
static void test_le_padding_is_zero(void)
{
	static const uint8_t FRAME[LE_MAX_PAYLOAD];
	uint32_t caplen;

	for (caplen = 9; caplen <= 16; caplen++) {
		pcapng_le_packet rec;
		uint32_t pcapng_caplen = 10 + caplen;
		uint32_t block_length =
			PCAPNG_BLOCK_LENGTH(pcapng_caplen);
		size_t data_end = 28 + pcapng_caplen;

		memset(&rec, SCRIBBLE, sizeof(rec));
		assemble_pcapng_le_packet(&rec, 0, 7, caplen, 0, -20, -80, 0,
					  LE_ADV_AA, LE_DEWHITENED, FRAME);

		CHECK(rec.blk_header.block_total_length == block_length);
		CHECK(rec.blk_header.captured_len == pcapng_caplen);
		CHECK(all_zero(&rec, data_end, block_length - 4));
		CHECK(((const uint32_t *) &rec)[block_length / 4 - 1] ==
		      block_length);
	}
}

/*
 * The longest block either record can build has to fit inside it. A capture
 * cannot reach the longest BR/EDR one, because the decoder clamps a payload
 * to the 343 bytes a DH5 carries, but the record is sized for the maximum the
 * format allows and the writers must stay inside it either way.
 */
static void test_longest_block_fits(void)
{
	static const uint8_t PAYLOAD[BREDR_MAX_PAYLOAD];
	static const uint8_t FRAME[LE_MAX_PAYLOAD];
	pcapng_bredr_packet bredr;
	pcapng_le_packet le;

	memset(&bredr, SCRIBBLE, sizeof(bredr));
	assemble_pcapng_bredr_packet(&bredr, 0, 7, BREDR_MAX_PAYLOAD, 0, -20,
				     -80, 0, 0, 0, 0, 0, 0x9e8b33, 0x9e8b33,
				     0x47, 0x1923, BREDR_DEWHITENED,
				     (const char *) PAYLOAD);
	CHECK(bredr.blk_header.block_total_length == sizeof(bredr));
	CHECK(bredr.blk_header.captured_len == 22 + BREDR_MAX_PAYLOAD);
	CHECK(bredr.options == 0);
	CHECK(bredr.blk_tot_length == sizeof(bredr));

	memset(&le, SCRIBBLE, sizeof(le));
	assemble_pcapng_le_packet(&le, 0, 7, LE_MAX_PAYLOAD, 0, -20, -80, 0,
				  LE_ADV_AA, LE_DEWHITENED, FRAME);
	CHECK(le.blk_header.block_total_length == sizeof(le));
	CHECK(le.blk_header.captured_len == 10 + LE_MAX_PAYLOAD);
	CHECK(le.options == 0);
	CHECK(le.blk_tot_length == sizeof(le));

	/* the longest payload the decoder can produce, a DH5's */
	memset(&bredr, SCRIBBLE, sizeof(bredr));
	assemble_pcapng_bredr_packet(&bredr, 0, 7, MAX_PAYLOAD_LENGTH / 8, 0,
				     -20, -80, 0, 0, 0, 0, 0, 0x9e8b33,
				     0x9e8b33, 0x47, 0x1923, BREDR_DEWHITENED,
				     (const char *) PAYLOAD);
	CHECK(bredr.blk_header.block_total_length == 404);
	CHECK(all_zero(&bredr, 28 + 22 + 343, 400));
}

int main(void)
{
	test_bredr_padding_is_zero();
	test_le_padding_is_zero();
	test_longest_block_fits();

	if (failures) {
		printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("all record assembler checks passed\n");
	return 0;
}
