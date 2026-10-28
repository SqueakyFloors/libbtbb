/*
 * stdout and stderr into temporary files for as long as a test needs, so it
 * can say which stream the library wrote a line on.
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
#ifndef INCLUDED_STREAM_CAPTURE_H
#define INCLUDED_STREAM_CAPTURE_H

#include <stdio.h>
#include <unistd.h>

static FILE *capture_out, *capture_err;
static int capture_saved_out = -1, capture_saved_err = -1;

/* Returns 0 once both streams are captured. On failure nothing is
 * redirected, and the caller has nothing to test rather than a failure. */
static int capture_start(void)
{
	capture_out = tmpfile();
	capture_err = tmpfile();
	fflush(stdout);
	fflush(stderr);
	capture_saved_out = dup(fileno(stdout));
	capture_saved_err = dup(fileno(stderr));
	if (capture_out != NULL && capture_err != NULL &&
	    capture_saved_out >= 0 && capture_saved_err >= 0 &&
	    dup2(fileno(capture_out), fileno(stdout)) >= 0 &&
	    dup2(fileno(capture_err), fileno(stderr)) >= 0)
		return 0;

	/* put back whatever was redirected before giving up */
	if (capture_saved_out >= 0) {
		dup2(capture_saved_out, fileno(stdout));
		close(capture_saved_out);
		capture_saved_out = -1;
	}
	if (capture_saved_err >= 0) {
		dup2(capture_saved_err, fileno(stderr));
		close(capture_saved_err);
		capture_saved_err = -1;
	}
	if (capture_out != NULL)
		fclose(capture_out);
	if (capture_err != NULL)
		fclose(capture_err);
	capture_out = capture_err = NULL;
	return -1;
}

static void capture_read(FILE *capture, char *into, size_t len)
{
	size_t read;

	rewind(capture);
	read = fread(into, 1, len - 1, capture);
	into[read] = '\0';
	fclose(capture);
}

/* Put both streams back and hand the test what was written to each. */
static void capture_stop(char *out, size_t outlen, char *err, size_t errlen)
{
	fflush(stdout);
	fflush(stderr);
	dup2(capture_saved_out, fileno(stdout));
	dup2(capture_saved_err, fileno(stderr));
	close(capture_saved_out);
	close(capture_saved_err);
	capture_saved_out = capture_saved_err = -1;
	capture_read(capture_out, out, outlen);
	capture_read(capture_err, err, errlen);
	capture_out = capture_err = NULL;
}

#endif /* INCLUDED_STREAM_CAPTURE_H */
