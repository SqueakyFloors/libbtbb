/*
 * The hop permutation table btbb_init() builds.
 *
 * fast_perm() reads a 512 KB table of every perm5() answer. btbb_init()
 * fills it so that it is whole before a caller starts a second thread; two
 * threads filling it together would let a third read half-written entries and
 * predict hops wrongly with nothing to say so. This test pins that the table
 * is complete when btbb_init() returns, with no fast_perm() call to fill it,
 * and that it is complete however that call comes out: with no syndrome table
 * asked for, and with an argument the library refuses.
 *
 * The table and the flag are internal to the library, so the test compiles
 * the two translation units they live in rather than linking the library.
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

#include "bluetooth_packet.c"
#include "bluetooth_piconet.c"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/* How many of the 524,288 entries are not the permutation they stand for. A
   half-filled table reads as zero where it was not reached, and perm5()
   answers zero for only a few of its inputs, so a gap shows up here. */
static int entries_wrong(void)
{
	int z, p_high, p_low, wrong = 0;

	for (z = 0; z < 0x20; z++)
		for (p_high = 0; p_high < 0x20; p_high++)
			for (p_low = 0; p_low < 0x200; p_low++)
				if (perm_table[z][p_high][p_low] !=
				    perm5(z, p_high, p_low))
					++wrong;
	return wrong;
}

/* How many entries are not zero, for the state before anything has filled
   the table. */
static int entries_set(void)
{
	int z, p_high, p_low, set = 0;

	for (z = 0; z < 0x20; z++)
		for (p_high = 0; p_high < 0x20; p_high++)
			for (p_low = 0; p_low < 0x200; p_low++)
				if (perm_table[z][p_high][p_low] != 0)
					++set;
	return set;
}

/* Back to the state a process starts in, so the next case fills the table
   itself rather than reading what the last one left. */
static void forget_the_table(void)
{
	perm_table_initialized = 0;
	memset(perm_table, 0, sizeof(perm_table));
}

int main(void)
{
	/* Nothing has asked for a permutation yet, so the flag says so and
	   every byte of the table is still zero. */
	CHECK(perm_table_initialized == 0);
	CHECK(entries_set() == 0);

	/* btbb_init() fills it, so fast_perm() answers without filling
	   anything itself, and every entry is the permutation it stands
	   for. */
	CHECK(btbb_init(2) == 0);
	CHECK(perm_table_initialized == 1);
	CHECK(entries_wrong() == 0);

	/* The same answers through the reader. */
	CHECK(fast_perm(0, 0, 0) == perm5(0, 0, 0));
	CHECK(fast_perm(0x1f, 0x1f, 0x1ff) == perm5(0x1f, 0x1f, 0x1ff));
	CHECK(fast_perm(0x15, 0x0a, 0x123) == perm5(0x15, 0x0a, 0x123));

	/* A second btbb_init() leaves the table alone, as it leaves the
	   syndrome map alone. */
	CHECK(btbb_init(2) == 0);
	CHECK(entries_wrong() == 0);

	/* A call that asks for no access code error correction fills the
	   table all the same: the two tables do not depend on each other.
	   Whole, not one entry: a table filled part way would answer for the
	   entry a case happened to read. */
	forget_the_table();
	CHECK(btbb_init(0) == 0);
	CHECK(perm_table_initialized == 1);
	CHECK(entries_wrong() == 0);

	/* And so does a call whose argument the library refuses, because the
	   table is filled before the argument is looked at. */
	forget_the_table();
	CHECK(btbb_init(-1) == -1);
	CHECK(perm_table_initialized == 1);
	CHECK(entries_wrong() == 0);

	/* The same for a value above what the library accepts, which is the
	   other way the argument check refuses one. */
	forget_the_table();
	CHECK(btbb_init(AC_ERROR_LIMIT + 1) == -1);
	CHECK(perm_table_initialized == 1);
	CHECK(entries_wrong() == 0);

	if (failures) {
		printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("all permutation table checks passed\n");
	return 0;
}
