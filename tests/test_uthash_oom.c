/*
 * What the library does when one of its hash tables cannot be allocated: the
 * piconet survey's table of LAPs, and the access code error correction table
 * btbb_init() builds. A table is grown inside uthash's macros, which expand
 * in the caller, and the library asks uthash to put the table back as it was
 * and report the failure rather than end the process. These cases check that
 * the caller is told, that the entries already in the table are untouched,
 * and that the table works again once the memory is there, for each of the
 * three allocations uthash makes: the table, its bucket array, and a larger
 * bucket array when a bucket chain grows.
 *
 * That the process survives is itself the check: a library that ended the
 * process would take this test with it, and no case after it would run.
 *
 * The failure is produced by an allocator of this test's own under
 * uthash_malloc, so only uthash's own allocations fail; the survey's own two
 * allocations for a new LAP, which are checked and drop the packet, are left
 * working.
 */

#include <stdio.h>
#include <stdlib.h>

/*
 * Every allocation uthash makes goes through this, and nothing else does. The
 * system headers are included first, so that the name below is declared
 * before the library's hash table macros expand it.
 */
static void *failing_malloc(size_t size);
#define uthash_malloc(sz) failing_malloc(sz)

/* The functions under test are internal, so this compiles the translation
 * units they live in, as tests/test_piconet.c does. */
#include "bluetooth_packet.c"
#include "bluetooth_piconet.c"

/*
 * How many more allocations uthash may have before they start failing, or -1
 * for no limit. A case sets it to the number of allocations it wants to let
 * through, counted from where it is set, so a case need not know what has
 * been allocated before it.
 */
static int allocations_left = -1;

static void *failing_malloc(size_t size)
{
	if (allocations_left == 0)
		return NULL;
	if (allocations_left > 0)
		allocations_left--;
	return malloc(size);
}

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/* LAPs the survey has not heard, so that reading one starts an entry. */
#define FIRST_LAP 0x6587cd
#define SECOND_LAP 0x6587ce

/* Empty the survey, so that the next case starts without a table. */
static void empty_the_survey(void)
{
	btbb_piconet *pn;

	allocations_left = -1;
	while ((pn = btbb_next_survey_result()) != NULL)
		btbb_piconet_unref(pn);
	CHECK(HASH_COUNT(piconet_survey) == 0);
	CHECK(piconet_survey == NULL);
}

/* The first allocation the survey makes through uthash is the table itself. */
static void test_a_table_that_cannot_be_allocated(void)
{
	allocations_left = 0;
	CHECK(get_piconet(FIRST_LAP) == NULL);
	/* nothing was left half added */
	CHECK(piconet_survey == NULL);
	CHECK(btbb_survey_piconet(FIRST_LAP) == NULL);

	/* and the survey takes the same LAP once the memory is there */
	allocations_left = -1;
	CHECK(get_piconet(FIRST_LAP) != NULL);
	CHECK(HASH_COUNT(piconet_survey) == 1);

	empty_the_survey();
}

/* The second is the table's bucket array, which is allocated in the same
 * call, so letting the first through reaches it. */
static void test_a_bucket_array_that_cannot_be_allocated(void)
{
	allocations_left = 1;
	CHECK(get_piconet(FIRST_LAP) == NULL);
	CHECK(piconet_survey == NULL);
	CHECK(btbb_survey_piconet(FIRST_LAP) == NULL);

	allocations_left = -1;
	CHECK(get_piconet(FIRST_LAP) != NULL);
	CHECK(HASH_COUNT(piconet_survey) == 1);

	empty_the_survey();
}

/*
 * A larger bucket array for a table whose chains have grown. uthash expands
 * when a bucket's chain reaches HASH_BKT_CAPACITY_THRESH entries, and the
 * table starts with HASH_INITIAL_NUM_BUCKETS buckets, so filling one entry
 * more than the initial buckets hold at that chain length reaches an
 * expansion whatever the LAPs below hash to.
 */
static void test_a_bucket_array_that_cannot_be_grown(void)
{
	uint32_t lap;
	uint32_t laps = HASH_INITIAL_NUM_BUCKETS * HASH_BKT_CAPACITY_THRESH;
	uint32_t added = 0;
	int refused = 0;

	/* the table and its first bucket array, and nothing after them */
	allocations_left = 2;
	for (lap = 0; lap < laps; lap++) {
		if (get_piconet(lap) == NULL) {
			refused = 1;
			break;
		}
		added++;
	}
	CHECK(refused);
	/* the LAPs taken before the expansion are still in the survey, and
	 * the one refused is not */
	CHECK(HASH_COUNT(piconet_survey) == added);
	CHECK(btbb_survey_piconet(lap) == NULL);
	if (added > 0)
		CHECK(btbb_survey_piconet(added - 1) != NULL);

	/* and the same LAP goes in once the memory is there */
	allocations_left = -1;
	CHECK(get_piconet(lap) != NULL);
	CHECK(HASH_COUNT(piconet_survey) == added + 1);

	empty_the_survey();
}

/*
 * The library's other hash table, and the larger of the two: the access code
 * error correction table btbb_init() builds. Its first entry allocates the
 * table, as the survey's does, and a failure there is the -1 btbb.h
 * describes rather than the end of the process.
 */
static void test_an_error_correction_table_that_cannot_be_allocated(void)
{
	allocations_left = 0;
	CHECK(btbb_init(2) == -1);
	/* no half-built table was left behind */
	CHECK(syndrome_map == NULL);

	/* and a table that was being built when an allocation failed is
	 * thrown away whole: half a map would correct fewer access code
	 * errors than the caller asked for and nothing would say so. Two
	 * allocations through is the table and its first bucket array, so the
	 * failure here is a bucket array it could not grow, partway in. */
	allocations_left = 2;
	CHECK(btbb_init(2) == -1);
	CHECK(syndrome_map == NULL);

	/* so a later call builds it */
	allocations_left = -1;
	CHECK(btbb_init(2) == 0);
	CHECK(syndrome_map != NULL);
	CHECK(find_syndrome(gen_syndrome(DEFAULT_AC ^ 1ULL)) != NULL);
}

/*
 * The failures above are the allocator's doing and nothing else's: with it
 * serving every request, the same call keeps the piconet it started in the
 * survey.
 */
static void test_a_survey_that_can_allocate_keeps_its_piconet(void)
{
	btbb_piconet *pn;

	allocations_left = -1;
	pn = get_piconet(FIRST_LAP);
	CHECK(pn != NULL);
	CHECK(btbb_survey_piconet(FIRST_LAP) == pn);
	CHECK(get_piconet(SECOND_LAP) != NULL);
	CHECK(HASH_COUNT(piconet_survey) == 2);

	empty_the_survey();
}

int main(void)
{
	test_a_table_that_cannot_be_allocated();
	test_a_bucket_array_that_cannot_be_allocated();
	test_a_bucket_array_that_cannot_be_grown();
	test_a_survey_that_can_allocate_keeps_its_piconet();
	/* last: it builds a table of several hundred thousand entries that
	 * nothing frees */
	test_an_error_correction_table_that_cannot_be_allocated();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
