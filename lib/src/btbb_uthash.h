/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * The library's settings for uthash, the hash table macros in uthash.h.
 * Include this file instead of uthash.h: uthash reads its settings from
 * whatever is defined before it, so a source that included uthash.h directly
 * would get the defaults.
 */
/* uthash reads its settings when it is included, so a source that has already
 * included it is running on the defaults and the settings below would come too
 * late to have any effect. */
#if defined(UTHASH_VERSION) && !defined(INCLUDED_BTBB_UTHASH_H)
#error "include btbb_uthash.h rather than uthash.h, and before it"
#endif

#ifndef INCLUDED_BTBB_UTHASH_H
#define INCLUDED_BTBB_UTHASH_H

#include <stdio.h>
#include <stdlib.h>

/* An allocation failure inside a hash table ends the process, and this is
 * what it says first.
 *
 * Adding an entry allocates the table and its bucket array, and a growing
 * bucket chain allocates a larger one. Those allocations are made inside
 * uthash's macros, which expand in the caller and have no error path: a
 * HASH_ADD that carried on after one failed would work through a table that
 * is not there. So uthash calls uthash_fatal(), and the library ends the
 * process rather than write through nothing. The message goes to standard
 * error with the exit status every other failure uses, where uthash's own
 * default prints nothing and exits 255.
 *
 * The library keeps two hash tables: the access code error correction table
 * btbb_init() builds, which is the larger of the two by far, and the piconet
 * survey's table of the LAPs it has heard. btbb.h records this for both.
 */
#define uthash_fatal(msg)                                                 \
	do {                                                              \
		/* keep the two streams in order when both are a terminal */ \
		fflush(stdout);                                           \
		fputs("error: libbtbb: out of memory\n", stderr);         \
		fflush(stderr);                                           \
		exit(1);                                                  \
	} while (0)

/* The settings above are in place, which is what uthash.h asks for before it
 * will be included. */
#define BTBB_UTHASH_SETTINGS 1

#include "uthash.h"

#endif /* INCLUDED_BTBB_UTHASH_H */
