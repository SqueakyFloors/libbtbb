/*
 * The company identifier table: that an identifier assigned since the table
 * was last taken from BlueZ decodes by name, that the names the earlier table
 * carried are still there, and that an unassigned identifier says so.
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

#include "companies.c"

static int failures = 0;

#define CHECK_NAME(compid, want)                                           \
	do {                                                               \
		const char *got = bt_compidtostr((uint16_t)(compid));      \
		if (strcmp(got, (want)) != 0) {                            \
			printf("FAIL %s:%d: 0x%04X is \"%s\", wanted \"%s\"\n", \
			       __FILE__, __LINE__, (unsigned)(compid),     \
			       got, (want));                               \
			++failures;                                        \
		}                                                          \
	} while (0)

/* The highest identifier the table names, and the last one in the Bluetooth
 * SIG list it was regenerated from. */
#define HIGHEST_ASSIGNED 0x113D

/* How many of 0x0000..HIGHEST_ASSIGNED the table names. The count the
 * regenerated table reaches is 4133; a lower floor than that leaves room for
 * the SIG to withdraw a few identifiers at the next refresh without the test
 * needing a new number. */
#define NAMED_AT_LEAST 4000

/* Identifiers the Bluetooth SIG assigned after the table was last taken from
 * BlueZ, which earlier releases decoded as "not assigned". */
static void test_recently_assigned_identifiers_decode_by_name(void)
{
	CHECK_NAME(0x113D, "Venndelta Pty Ltd");
	CHECK_NAME(0x113B, "ONiO AS");
	CHECK_NAME(0x1101, "Nearfaces UG (haftungsbeschr\xc3\xa4nkt)");
	CHECK_NAME(0x0BB8, "INNOVAG PTY. LTD.");
	CHECK_NAME(0x05DC, "Petronics Inc.");
	CHECK_NAME(0x040E, "SKF (U.K.) Limited");
}

/* The SIG's current name wins where it has changed. */
static void test_renamed_identifiers_carry_the_current_name(void)
{
	CHECK_NAME(0x0000, "Ericsson AB");
	CHECK_NAME(0x000A, "Qualcomm Technologies International, Ltd. (QTIL)");
	CHECK_NAME(0x003A, "Panasonic Holdings Corporation");
}

/* Identifiers the earlier table named that the SIG's current list leaves out.
 * A capture of one of those older devices still decodes by name. */
static void test_identifiers_dropped_by_the_sig_are_kept(void)
{
	CHECK_NAME(0x00F9, "StickNFind");
	CHECK_NAME(0x0035, "Eclipse (HQ Espana) S.L.");
	CHECK_NAME(0x040C, "Senix Corporation");
}

/* A name the SIG writes with double quotes in it, which the table has to
 * escape. */
static void test_a_name_with_quotes_in_it_comes_back_whole(void)
{
	CHECK_NAME(0x0C08, "limited liability company \"Red\"");
	CHECK_NAME(0x0380, "LLC \"MEGA-F service\"");
}

/* A name outside ASCII stays as the SIG writes it. */
static void test_a_name_outside_ascii_stays_intact(void)
{
	CHECK_NAME(0x0225, "Nestl\xc3\xa9 Nespresso S.A.");
	CHECK_NAME(0x037F, "Soci\xc3\xa9t\xc3\xa9 des Produits Nestl\xc3\xa9 S.A.");
}

static void test_unassigned_and_reserved_identifiers(void)
{
	/* Gaps inside the assigned range, and everything above it. */
	CHECK_NAME(0x0414, "not assigned");
	CHECK_NAME(0x1036, "not assigned");
	CHECK_NAME(HIGHEST_ASSIGNED + 1, "not assigned");
	CHECK_NAME(0xFFFE, "not assigned");

	CHECK_NAME(0xFFFF, "internal use");
}

/* Every identifier answers without reading off the end of the table, and the
 * assigned range is as full as the SIG list it came from. */
static void test_the_whole_identifier_range(void)
{
	unsigned compid;
	unsigned named = 0;
	/* One message per kind of anomaly, so a table that is wrong for a
	 * whole range does not bury the count check below in output. */
	int said_empty = 0;
	int said_above = 0;

	for (compid = 0; compid <= 0xFFFF; ++compid) {
		const char *name = bt_compidtostr((uint16_t)compid);

		if (name == NULL || name[0] == '\0') {
			if (!said_empty) {
				printf("FAIL %s:%d: 0x%04X has no name at "
				       "all\n",
				       __FILE__, __LINE__, compid);
				++failures;
				said_empty = 1;
			}
			continue;
		}
		if (compid > HIGHEST_ASSIGNED && compid != 0xFFFF) {
			if (strcmp(name, "not assigned") != 0 && !said_above) {
				printf("FAIL %s:%d: 0x%04X is above the "
				       "assigned range but names \"%s\"\n",
				       __FILE__, __LINE__, compid, name);
				++failures;
				said_above = 1;
			}
		} else if (compid <= HIGHEST_ASSIGNED &&
			   strcmp(name, "not assigned") != 0) {
			++named;
		}
	}

	if (named < NAMED_AT_LEAST) {
		printf("FAIL %s:%d: the table names %u identifiers, wanted at "
		       "least %u\n",
		       __FILE__, __LINE__, named, (unsigned)NAMED_AT_LEAST);
		++failures;
	}
}

int main(void)
{
	test_recently_assigned_identifiers_decode_by_name();
	test_renamed_identifiers_carry_the_current_name();
	test_identifiers_dropped_by_the_sig_are_kept();
	test_a_name_with_quotes_in_it_comes_back_whole();
	test_a_name_outside_ascii_stays_intact();
	test_unassigned_and_reserved_identifiers();
	test_the_whole_identifier_range();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
