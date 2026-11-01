/*
 * What the library does when one of its hash tables cannot be allocated: the
 * piconet survey's table of LAPs, and the access code error correction table
 * btbb_init() builds. A table is grown inside uthash's macros, which expand
 * in the caller and have no error path to return, so the library ends the
 * process there - and says why first. These cases check the message, the
 * stream it goes to and the exit status, for each of the three allocations
 * uthash makes: the table, its bucket array, and a larger bucket array when a
 * bucket chain grows.
 *
 * The failure is produced by an allocator of this test's own under
 * uthash_malloc, so only uthash's own allocations fail; the survey's own two
 * allocations for a new LAP, which are checked and drop the packet, are left
 * working. Each case runs in a child process, because the library is expected
 * to exit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

/* calls since this process started, which a forked child continues */
static int uthash_allocations;
/* fail this call onward, 0 to fail none */
static int fail_from;

static void *failing_malloc(size_t size)
{
	uthash_allocations++;
	if ((fail_from != 0) && (uthash_allocations >= fail_from))
		return NULL;
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

/* The message the library is expected to end with. */
#define OUT_OF_MEMORY "error: libbtbb: out of memory\n"

/* A LAP the survey has not heard, so that reading it starts an entry. */
#define TEST_LAP 0x6587cd

/*
 * The exit status a child uses to report that the library came back from the
 * call instead of ending the process. Any value that is not 1 would do; this
 * one is far enough from the statuses the library itself uses to be
 * recognizable in a failure message.
 */
#define CHILD_RETURNED 42

/* Whatever the child wrote to one of the streams, as a string. */
static void read_stream(FILE *file, char *text, size_t text_size)
{
	size_t length;

	rewind(file);
	length = fread(text, 1, text_size - 1, file);
	text[length] = '\0';
}

/*
 * Run `body` in a child process with the child's two streams in temporary
 * files, and hand back the child's wait status and what it wrote on each
 * stream. Returns 0 once the child has been waited for, and -1 where the
 * child could not be started, which leaves the caller with nothing to check
 * rather than a failure.
 */
static int run_in_child(void (*body)(void), int *status,
			char *out, size_t out_size,
			char *err, size_t err_size)
{
	FILE *out_file = tmpfile();
	FILE *err_file = tmpfile();
	pid_t child;
	int started = -1;

	if ((out_file == NULL) || (err_file == NULL))
		goto done;

	/* nothing of this process's own buffered output may be inherited:
	 * the child would write the same bytes out a second time */
	fflush(NULL);

	child = fork();
	if (child < 0)
		goto done;
	if (child == 0) {
		/* the child's copy of the two streams, so that what the
		 * library writes lands in the files read below */
		if ((dup2(fileno(out_file), STDOUT_FILENO) < 0) ||
		    (dup2(fileno(err_file), STDERR_FILENO) < 0))
			_exit(CHILD_RETURNED);
		body();
		/* body() is expected to end the process; reaching this line
		 * is what the status reports */
		fflush(NULL);
		_exit(CHILD_RETURNED);
	}
	if (waitpid(child, status, 0) != child)
		goto done;

	read_stream(out_file, out, out_size);
	read_stream(err_file, err, err_size);
	started = 0;

done:
	if (out_file != NULL)
		fclose(out_file);
	if (err_file != NULL)
		fclose(err_file);
	return started;
}

/* The exit status and the message, for a child that failed the allocation
 * named in `what`. */
static void check_child(void (*body)(void), const char *what)
{
	char out[256], err[256];
	int status = 0;

	/* Each case fails an allocation by its number, counted from the first
	 * one this process makes, so a case that ran after something else had
	 * allocated would fail an allocation other than the one it means to
	 * and cover nothing. Said here rather than left to the order main()
	 * happens to call them in. */
	if (uthash_allocations != 0) {
		printf("FAIL %s: ran after %d hash table allocation(s) had "
		       "been made\n", what, uthash_allocations);
		++failures;
		return;
	}

	if (run_in_child(body, &status, out, sizeof(out),
			 err, sizeof(err)) != 0) {
		printf("FAIL %s: the child could not be started\n", what);
		++failures;
		return;
	}

	if (!WIFEXITED(status)) {
		printf("FAIL %s: the child did not exit (status %d)\n",
		       what, status);
		++failures;
		return;
	}
	if (WEXITSTATUS(status) == CHILD_RETURNED) {
		printf("FAIL %s: the library returned instead of exiting\n",
		       what);
		++failures;
		return;
	}
	/* the status every other failure in the library's tools uses */
	if (WEXITSTATUS(status) != 1) {
		printf("FAIL %s: exit status %d, expected 1\n",
		       what, WEXITSTATUS(status));
		++failures;
	}
	/* said once, on standard error, so that a tool writing capture data
	 * to standard output holds nothing but the data */
	if (strcmp(err, OUT_OF_MEMORY) != 0) {
		printf("FAIL %s: standard error held \"%s\"\n", what, err);
		++failures;
	}
	if (strlen(out) != 0) {
		printf("FAIL %s: standard output held \"%s\"\n", what, out);
		++failures;
	}
}

/* The first allocation the survey makes through uthash is the table itself. */
static void fail_the_table(void)
{
	fail_from = 1;
	get_piconet(TEST_LAP);
}

/* The second is the table's bucket array, which is allocated in the same
 * call, so letting the first through reaches it. */
static void fail_the_bucket_array(void)
{
	fail_from = 2;
	get_piconet(TEST_LAP);
}

/*
 * The third, and every one after it, is a larger bucket array for a table
 * whose chains have grown. uthash expands when a bucket's chain reaches
 * HASH_BKT_CAPACITY_THRESH entries, and the table starts with
 * HASH_INITIAL_NUM_BUCKETS buckets, so filling one entry more than the
 * initial buckets hold at that chain length reaches an expansion whatever the
 * LAPs below hash to.
 */
static void fail_an_expansion(void)
{
	uint32_t lap;
	uint32_t laps = HASH_INITIAL_NUM_BUCKETS * HASH_BKT_CAPACITY_THRESH;

	fail_from = 3;
	for (lap = 0; lap < laps; lap++)
		get_piconet(lap);
}

/*
 * The library's other hash table, and the larger of the two: the access code
 * error correction table btbb_init() builds. Its first entry allocates the
 * table, as the survey's does.
 */
static void fail_the_error_correction_table(void)
{
	fail_from = 1;
	btbb_init(2);
}

/*
 * The failures above are the allocator's doing and nothing else's: with it
 * serving every request, the same call keeps the piconet it started in the
 * survey, in this process.
 */
static void test_a_survey_that_can_allocate_keeps_its_piconet(void)
{
	btbb_piconet *pn;

	fail_from = 0;
	pn = get_piconet(TEST_LAP);
	CHECK(pn != NULL);
	CHECK(btbb_survey_piconet(TEST_LAP) == pn);
	CHECK(HASH_COUNT(piconet_survey) == 1);

	/* and the survey is left empty for anything that runs after this */
	while ((pn = btbb_next_survey_result()) != NULL)
		btbb_piconet_unref(pn);
	CHECK(HASH_COUNT(piconet_survey) == 0);
}

int main(void)
{
	/* each of these runs before this process has a survey table of its
	 * own, so the child makes the table and reaches the allocation its
	 * case is about */
	check_child(fail_the_table, "the survey's table");
	check_child(fail_the_bucket_array, "the bucket array");
	check_child(fail_an_expansion, "a larger bucket array");
	check_child(fail_the_error_correction_table,
		    "the error correction table");

	test_a_survey_that_can_allocate_keeps_its_piconet();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
