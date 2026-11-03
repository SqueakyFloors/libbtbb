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

/*
 * An allocation failure inside a hash table is reported to the caller, which
 * is what HASH_NONFATAL_OOM asks of uthash.
 *
 * Adding an entry allocates the table and its bucket array, and a growing
 * bucket chain allocates a larger one. Those allocations are made inside
 * uthash's macros, which expand in the caller and have no value to return.
 * With this setting uthash puts the table back as it was, frees whatever it
 * had got, and calls uthash_nonfatal_oom() with the entry it did not add;
 * without it a failure ends the process, taking the caller's program with it.
 *
 * The library keeps two hash tables: the access code error correction table
 * btbb_init() builds, which is the larger of the two by far, and the piconet
 * survey's table of the LAPs it has heard. btbb.h records for both what a
 * caller gets when the allocation fails.
 */
#define HASH_NONFATAL_OOM 1
#define uthash_nonfatal_oom(elt)                                          \
	do {                                                              \
		btbb_hash_oom = 1;                                        \
	} while (0)

/* The settings above are in place, which is what uthash.h asks for before it
 * will be included. */
#define BTBB_UTHASH_SETTINGS 1

#include "uthash.h"

/*
 * Put `add` in the hash table `head`, under its `keylen`-byte key field
 * `fieldname`, and set `added` to 1 where the entry went in or to 0 where a
 * hash table allocation failed. On 0 the table is as it was and `add` is
 * still the caller's to free.
 *
 * The flag the handler above sets is this macro's own local variable, so the
 * flag is not shared between threads (the tables themselves are not guarded;
 * btbb.h says which calls belong on one thread), and a HASH_ADD written
 * outside the wrapper does not compile: there would be no such variable in
 * scope.
 */
#define BTBB_HASH_ADD(hh, head, fieldname, keylen, add, added)            \
	do {                                                              \
		int btbb_hash_oom = 0;                                    \
		HASH_ADD(hh, head, fieldname, keylen, add);               \
		(added) = !btbb_hash_oom;                                 \
	} while (0)

#endif /* INCLUDED_BTBB_UTHASH_H */
