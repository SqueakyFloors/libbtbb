/*
 * Shared input handling for the fuzz targets: a reader that takes the
 * leading bytes of a fuzzer input as decoder parameters, and the two
 * conversions every target needs - fuzzer bytes into the one-symbol-per-byte
 * stream a BR/EDR decoder reads, and an initialized packet object to decode
 * into.
 */
#ifndef INCLUDED_FUZZ_INPUT_H
#define INCLUDED_FUZZ_INPUT_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btbb.h"
#include "bluetooth_packet.h"
#include "bluetooth_le_packet.h"
#include "bluetooth_piconet.h"

/*
 * How many access code bit errors the targets ask the library to correct.
 * The syndrome table btbb_init() builds grows with it - 5 costs hundreds of
 * megabytes - and 2 is what the rest of the test suite uses.
 */
#define FZ_MAX_AC_ERRORS 2

/* A fuzzer input, read from the front. A read past the end gives zero, so a
 * target decodes the same way whether the fuzzer trimmed the input or not. */
struct fuzz_input {
	const uint8_t *data;
	size_t size;
	size_t pos;
};

static inline void fz_start(struct fuzz_input *in, const uint8_t *data, size_t size)
{
	in->data = data;
	in->size = size;
	in->pos = 0;
}

static inline uint8_t fz_u8(struct fuzz_input *in)
{
	if (in->pos >= in->size)
		return 0;
	return in->data[in->pos++];
}

static inline uint16_t fz_u16(struct fuzz_input *in)
{
	uint16_t low = fz_u8(in);
	return (uint16_t) (low | ((uint16_t) fz_u8(in) << 8));
}

static inline uint32_t fz_u32(struct fuzz_input *in)
{
	uint32_t value = fz_u16(in);
	return value | ((uint32_t) fz_u16(in) << 16);
}

static inline uint64_t fz_u64(struct fuzz_input *in)
{
	uint64_t value = fz_u32(in);
	return value | ((uint64_t) fz_u32(in) << 32);
}

/* Whatever is left, which is where a target puts the packet itself. */
static inline const uint8_t *fz_rest(struct fuzz_input *in, size_t *len)
{
	*len = in->size - (in->pos < in->size ? in->pos : in->size);
	return in->data + (in->pos < in->size ? in->pos : in->size);
}

/*
 * Unpack bytes into the symbols a BR/EDR decoder reads: one per byte, valued
 * 0 or 1, least significant bit of each input byte first. That is what a
 * receiver hands the library, and 8 symbols to an input byte keeps the corpus
 * small and lets one bit flip in the fuzzer's mutation be one symbol flip on
 * air.
 *
 * Returns the number of symbols written, at most 'out_max'.
 */
static inline int fz_symbols(const uint8_t *bytes, size_t len, char *out,
		      size_t out_max)
{
	size_t i;
	size_t count = len * 8 < out_max ? len * 8 : out_max;

	for (i = 0; i < count; i++)
		out[i] = (char) ((bytes[i / 8] >> (i % 8)) & 1);
	return (int) count;
}

/*
 * A packet object holding 'length' symbols, with the address part, clock and
 * whitening a target asks for.
 *
 * btbb_find_ac() is the only way in the public interface to put an address
 * part on a packet, so it is called here on a sync word of its own - built
 * for the LAP, so the search matches it with no errors, as a receiver that
 * found a packet does. No symbol the caller passed is read for it.
 *
 * Returns NULL if the packet could not be allocated.
 */
static inline btbb_packet *fz_packet(char *symbols, int length, uint32_t lap,
			      uint8_t uap, uint8_t channel, uint32_t clkn,
			      int whitened)
{
	/* a sync word's worth of symbols, plus the offset being searched */
	char ac_stream[SYNCWORD_SYMBOLS + 1];
	uint64_t syncword = btbb_gen_syncword((int) lap);
	btbb_packet *pkt = NULL;
	int i;

	for (i = 0; i < SYNCWORD_SYMBOLS; i++)
		ac_stream[i] = (char) ((syncword >> i) & 1);
	ac_stream[SYNCWORD_SYMBOLS] = 0;

	if (btbb_find_ac(ac_stream, 1, lap, 0, &pkt) < 0 || pkt == NULL)
		return NULL;

	btbb_packet_set_data(pkt, symbols, length, channel, clkn);
	btbb_packet_set_uap(pkt, uap);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, whitened);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	return pkt;
}

/*
 * An LE packet decoded from 'bytes'. The length form of the decoder is the
 * one called: it reads no more than the bytes the fuzzer provided, so a
 * short input is a short capture and not a read of whatever follows it.
 *
 * Returns NULL if the packet could not be allocated.
 */
static inline lell_packet *fz_le_packet(const uint8_t *bytes, size_t len,
				 uint16_t phys_channel, uint32_t clk100ns)
{
	lell_packet *pkt = NULL;

	lell_allocate_and_decode_n(bytes, len, phys_channel, clk100ns, &pkt);
	return pkt;
}

/*
 * One file in the temporary directory for the capture writers to write, named
 * once per process so that fuzzers running side by side do not share it. The
 * writers are driven over the same file run after run, which each run starts
 * again from nothing, so a long run does not fill the disk.
 */
#define FZ_CAPTURE_PATH_MAX 1024

static inline char *fz_capture_path(void)
{
	static char path[FZ_CAPTURE_PATH_MAX];

	return path;
}

static inline void fz_capture_file_remove(void)
{
	if (fz_capture_path()[0] != '\0')
		(void) remove(fz_capture_path());
}

static inline void fz_capture_file_init(void)
{
	const char *dir = getenv("TMPDIR");
	int fd;

	if (dir == NULL || dir[0] == '\0')
		dir = "/tmp";
	if ((size_t) snprintf(fz_capture_path(), FZ_CAPTURE_PATH_MAX,
			      "%s/libbtbb-fuzz-XXXXXX", dir) >=
	    FZ_CAPTURE_PATH_MAX)
		abort();	/* no room for the name */

	fd = mkstemp(fz_capture_path());
	if (fd < 0)
		abort();	/* nowhere to write a capture */
	(void) close(fd);
	if (atexit(fz_capture_file_remove) != 0)
		abort();
}

/*
 * The path to write, with nothing at it: one of the two formats refuses to
 * create a file that exists, as it must, so each run clears the way first.
 */
static inline const char *fz_capture_file_path(void)
{
	fz_capture_file_remove();
	return fz_capture_path();
}

#endif /* INCLUDED_FUZZ_INPUT_H */
