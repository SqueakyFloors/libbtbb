/*
 * The library's settings for uthash, the hash table macros in uthash.h.
 * Include this file instead of uthash.h: uthash reads its settings from
 * whatever is defined before it, so a source that included uthash.h directly
 * would get the defaults.
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
 * btbb.h records this for the piconet survey, the only part of the library a
 * caller reaches a hash table through.
 */
#define uthash_fatal(msg)                                                 \
	do {                                                              \
		/* keep the two streams in order when both are a terminal */ \
		fflush(stdout);                                           \
		fputs("error: libbtbb: out of memory\n", stderr);         \
		fflush(stderr);                                           \
		exit(1);                                                  \
	} while (0)

#include "uthash.h"

#endif /* INCLUDED_BTBB_UTHASH_H */
