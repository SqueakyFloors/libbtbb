/*
 * BR/EDR baseband conformance: the Bluetooth Core specification's own sample
 * data, driven through libbtbb.
 *
 * Every vector here is transcribed from Bluetooth Core Specification v5.4,
 * Vol 2, Part G "Sample Data", with the definitions it complements in
 * Vol 2, Part B "Baseband Specification". Each test says which section its
 * vectors come from.
 *
 * Where a vector set is not published for something the library has to get
 * right (a payload at the maximum length its packet type allows, for
 * instance), the test builds the packet with its own implementation of the
 * spec's definition: spec_hec(), spec_whiten() and spec_fec23(). Each of
 * those is itself pinned by published sample data before it is used, so a
 * built packet is still the specification's answer and not libbtbb's.
 *
 * The functions under test are internal to the library, so this compiles the
 * translation unit they live in, as tests/test_internal.c does.
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

#include <string.h>

#include "bluetooth_packet.c"

static int failures = 0;

#define CHECK(cond)                                                        \
	do {                                                               \
		if (!(cond)) {                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
			       #cond);                                     \
			++failures;                                        \
		}                                                          \
	} while (0)

/* ------------------------------------------------------------------ *
 * The specification's own implementations, used to build test packets
 * ------------------------------------------------------------------ */

/*
 * The HEC generating LFSR of Vol 2, Part B, Section 7.1.1: the generator
 * polynomial is g(D) = D^8 + D^7 + D^5 + D^2 + D + 1, the register is
 * preloaded with the UAP least significant bit first, and the 10 header data
 * bits are clocked in starting with the bit sent first on air. It is written
 * the way crcgen() writes the CRC register: shift towards the least
 * significant bit, feed the new bit in at the top, then apply the taps.
 *
 * test_hec_sample_data() pins it against the 20 published vectors before
 * anything here builds a header with it.
 */
static uint8_t spec_hec(uint16_t header_data, uint8_t uap)
{
	uint8_t reg = reverse((char) uap);
	int i;

	for (i = 0; i < 10; i++) {
		uint8_t bit = (uint8_t) ((header_data >> i) & 1);

		reg = (uint8_t) ((reg >> 1) | (((reg & 1) ^ bit) << 7));
		reg ^= (uint8_t) ((reg & 0x80) >> 1);
		reg ^= (uint8_t) ((reg & 0x80) >> 2);
		reg ^= (uint8_t) ((reg & 0x80) >> 5);
		reg ^= (uint8_t) ((reg & 0x80) >> 7);
	}
	return reg;
}

/*
 * The data whitening LFSR of Vol 2, Part B, Section 7.2: g(D) = D^7 + D^4 + 1,
 * initialized with CLK6-1 and a most significant bit of one, output taken
 * from the top of the register. test_whitening_sample_data() pins it against
 * the published sequence.
 *
 * clock is CLK1-27, as everything in this library calls it; skip is how many
 * whitening bits the packet has already consumed, so that the payload
 * continues from where the header left off rather than starting over.
 */
static void spec_whiten(char *bits, int length, int clock, int skip)
{
	uint8_t reg = (uint8_t) (0x40 | (clock & 0x3f));
	int i;

	for (i = 0; i < skip + length; i++) {
		uint8_t out = (uint8_t) ((reg >> 6) & 1);

		if (i >= skip)
			bits[i - skip] ^= (char) out;
		reg = (uint8_t) (((reg << 1) | out) & 0x7f);
		reg ^= (uint8_t) (out << 4);
	}
}

/*
 * One rate 2/3 FEC block: 10 data bits in air order become 15 symbols, the 10
 * data bits followed by 5 parity bits. The code is linear, so the 10 published
 * codewords of Vol 2, Part G, Section 9 (one per data bit) are all it takes;
 * test_fec23_sample_data() checks them against the library's encoder.
 */
static const uint16_t FEC23_CODEWORDS[10] = {
	/* 0x001 -> 1000000000 11010, and so on down the published table,
	 * read as bit 0 first */
	0x2c01, 0x5802, 0x1c04, 0x3808, 0x7010,
	0x4c20, 0x3440, 0x6880, 0x7d00, 0x5600
};

static void spec_fec23(const char *data, char *out, int data_bits)
{
	int block;

	for (block = 0; block * 10 < data_bits; block++) {
		uint16_t codeword = 0;
		int i;

		for (i = 0; i < 10; i++)
			if (block * 10 + i < data_bits && data[block * 10 + i])
				codeword ^= FEC23_CODEWORDS[i];
		for (i = 0; i < 15; i++)
			out[block * 15 + i] = (char) ((codeword >> i) & 1);
	}
}

/* ------------------------------------------------------------------ *
 * Section 3: access code sample data
 * ------------------------------------------------------------------ */

/*
 * Vol 2, Part G, Section 3. The sync word is printed in transmit order, first
 * symbol leftmost, so the most significant bit of the value below is the first
 * symbol on air. btbb_gen_syncword() returns the same 64 symbols the other way
 * round, first symbol in bit 0, which is the order air_to_host64() reads a
 * captured stream in.
 *
 * The preamble and trailer are the published four-symbol patterns, again first
 * symbol leftmost, so in bit 3 of the value.
 */
static const struct {
	uint32_t lap;
	uint64_t sync_word;
	uint8_t preamble;
	uint8_t trailer;
} ACCESS_CODES[] = {
	{ 0x000000, 0x7e7041e34000000dULL, 0x5, 0x5 },
	{ 0xffffff, 0xe758b5227ffffff2ULL, 0xa, 0xa },
	{ 0x9e8b33, 0x475c58cc73345e72ULL, 0x5, 0xa },	/* GIAC */
	{ 0x9e8b34, 0x28ed3c34cb345e72ULL, 0x5, 0xa },	/* a DIAC */
	{ 0x9e8b36, 0x62337b641b345e72ULL, 0x5, 0xa },
	{ 0x9e8b39, 0xc05747b9e7345e72ULL, 0xa, 0xa },
	{ 0x9e8b3d, 0x7084eab02f345e72ULL, 0x5, 0xa },
	{ 0x9e8b42, 0x64c86d2b90b45e72ULL, 0x5, 0xa },
	{ 0x9e8b48, 0xe3c3725e04b45e72ULL, 0xa, 0xa },
	{ 0x9e8b4f, 0x8c7216a6bcb45e72ULL, 0xa, 0xa },
	{ 0x9e8b57, 0xb2f16c30fab45e72ULL, 0xa, 0xa },
	{ 0x616cec, 0x586a491f0dcda18dULL, 0x5, 0x5 },
	{ 0x616c05, 0xceab4c99680da18dULL, 0xa, 0x5 },
	{ 0x6165c6, 0xdc44b49bd8e9a18dULL, 0xa, 0x5 }
};

#define N_ACCESS_CODES ((int) (sizeof(ACCESS_CODES) / sizeof(ACCESS_CODES[0])))

static uint64_t reverse64(uint64_t v)
{
	uint64_t out = 0;
	int i;

	for (i = 0; i < 64; i++)
		if (v & (1ULL << i))
			out |= 1ULL << (63 - i);
	return out;
}

static void test_access_code_sample_data(void)
{
	int row;

	for (row = 0; row < N_ACCESS_CODES; row++) {
		uint64_t expected = reverse64(ACCESS_CODES[row].sync_word);

		CHECK(btbb_gen_syncword((int) ACCESS_CODES[row].lap) ==
		      expected);

		/*
		 * The preamble and trailer rules of Sections 6.3.2 and 6.3.4:
		 * the preamble is 1010 when the first symbol of the sync word
		 * is a one and 0101 when it is a zero, and the trailer is 0101
		 * when the last symbol is a one and 1010 when it is a zero.
		 */
		CHECK(ACCESS_CODES[row].preamble ==
		      ((expected & 1) ? 0xa : 0x5));
		CHECK(ACCESS_CODES[row].trailer ==
		      ((expected >> 63) ? 0x5 : 0xa));
	}
}

/*
 * The sync word is a (64,30) codeword with a pseudo-random overlay, so a
 * search for a known LAP finds it with no bit errors, and the promiscuous
 * search recovers the LAP from it.
 */
static void test_access_code_search(void)
{
	int row;

	CHECK(btbb_init(2) == 0);

	for (row = 0; row < N_ACCESS_CODES; row++) {
		uint64_t syncword = reverse64(ACCESS_CODES[row].sync_word);
		char stream[64 + 1];
		btbb_packet *pkt = NULL;
		int i;

		for (i = 0; i < 64; i++)
			stream[i] = (char) ((syncword >> i) & 1);
		stream[64] = 0;

		CHECK(btbb_find_ac(stream, 1, ACCESS_CODES[row].lap, 0,
				   &pkt) == 0);
		if (pkt != NULL) {
			CHECK(btbb_packet_get_ac_errors(pkt) == 0);
			btbb_packet_unref(pkt);
			pkt = NULL;
		}

		CHECK(btbb_find_ac(stream, 1, LAP_ANY, 0, &pkt) == 0);
		if (pkt != NULL) {
			CHECK(btbb_packet_get_lap(pkt) ==
			      ACCESS_CODES[row].lap);
			btbb_packet_unref(pkt);
		}
	}
}

/*
 * btbb.h promises that the search reads search_length + 63 symbols and no
 * more: each of the first search_length symbols is tried as the start of the
 * 64 symbol sync word. These streams are allocated at exactly the length the
 * header promises is enough, so in a sanitizer build a read past it stops the
 * test.
 */
static void test_find_ac_reads_only_what_it_promises(void)
{
	uint64_t syncword = reverse64(ACCESS_CODES[2].sync_word); /* GIAC */
	uint32_t lap = ACCESS_CODES[2].lap;
	btbb_packet *pkt = NULL;
	char *stream;
	int i;

	/* one offset to try: the sync word and nothing else */
	stream = (char *) malloc(64);
	CHECK(stream != NULL);
	if (stream == NULL)
		return;
	for (i = 0; i < 64; i++)
		stream[i] = (char) ((syncword >> i) & 1);

	CHECK(btbb_find_ac(stream, 1, lap, 0, &pkt) == 0);
	if (pkt != NULL) {
		btbb_packet_unref(pkt);
		pkt = NULL;
	}
	CHECK(btbb_find_ac(stream, 1, LAP_ANY, 0, &pkt) == 0);
	if (pkt != NULL) {
		btbb_packet_unref(pkt);
		pkt = NULL;
	}
	free(stream);

	/*
	 * Several offsets to try, with the sync word at the last of them,
	 * which is the offset that reaches the end of the stream.
	 */
	stream = (char *) malloc(8 + 63);
	CHECK(stream != NULL);
	if (stream == NULL)
		return;
	memset(stream, 0, 8 + 63);
	for (i = 0; i < 64; i++)
		stream[7 + i] = (char) ((syncword >> i) & 1);

	CHECK(btbb_find_ac(stream, 8, lap, 0, &pkt) == 7);
	if (pkt != NULL) {
		btbb_packet_unref(pkt);
		pkt = NULL;
	}
	CHECK(btbb_find_ac(stream, 8, LAP_ANY, 0, &pkt) == 7);
	if (pkt != NULL) {
		btbb_packet_unref(pkt);
		pkt = NULL;
	}
	free(stream);

	/*
	 * No offset to try: there is no sync word to look at, so none of the
	 * stream may be read, not even the symbols the rough filter on the
	 * end of the sync word starts from.
	 */
	stream = (char *) malloc(1);
	CHECK(stream != NULL);
	if (stream == NULL)
		return;
	stream[0] = 0;
	CHECK(btbb_find_ac(stream, 0, lap, 0, &pkt) < 0);
	CHECK(btbb_find_ac(stream, 0, LAP_ANY, 0, &pkt) < 0);
	CHECK(pkt == NULL);
	free(stream);
}

/* ------------------------------------------------------------------ *
 * Section 4: HEC and packet header sample data
 * ------------------------------------------------------------------ */

/*
 * Vol 2, Part G, Section 4: the HEC computed over 10 header data bits for a
 * given UAP. The same table appears in tests/test_internal.c, which checks
 * the FEC 1/3 encoded headers and the inverse, uap_from_hec(); here it pins
 * spec_hec(), the forward direction, which the packets further down are built
 * with.
 */
static const struct {
	uint8_t uap;
	uint16_t data;
	uint8_t hec;
} HEC_VECTORS[] = {
	{ 0x00, 0x123, 0xe1 }, { 0x47, 0x123, 0x06 },
	{ 0x00, 0x124, 0x32 }, { 0x47, 0x124, 0xd5 },
	{ 0x00, 0x125, 0x5a }, { 0x47, 0x125, 0xbd },
	{ 0x00, 0x126, 0xe2 }, { 0x47, 0x126, 0x05 },
	{ 0x00, 0x127, 0x8a }, { 0x47, 0x127, 0x6d },
	{ 0x00, 0x11b, 0x9e }, { 0x47, 0x11b, 0x79 },
	{ 0x00, 0x11c, 0x4d }, { 0x47, 0x11c, 0xaa },
	{ 0x00, 0x11d, 0x25 }, { 0x47, 0x11d, 0xc2 },
	{ 0x00, 0x11e, 0x9d }, { 0x47, 0x11e, 0x7a },
	{ 0x00, 0x11f, 0xf5 }, { 0x47, 0x11f, 0x12 }
};

static void test_hec_sample_data(void)
{
	size_t row;

	for (row = 0; row < sizeof(HEC_VECTORS) / sizeof(HEC_VECTORS[0]);
	     row++) {
		CHECK(spec_hec(HEC_VECTORS[row].data, HEC_VECTORS[row].uap) ==
		      HEC_VECTORS[row].hec);
		CHECK(uap_from_hec(HEC_VECTORS[row].data,
				   HEC_VECTORS[row].hec) ==
		      HEC_VECTORS[row].uap);
	}
}

/* ------------------------------------------------------------------ *
 * Section 5: CRC sample data
 * ------------------------------------------------------------------ */

/*
 * Vol 2, Part G, Section 5: the CRC over a ten byte payload with a UAP of
 * 0x47 is 6d d2, and each byte goes out least significant bit first. crcgen()
 * returns the register, which air_to_host16() of those two CRC bytes equals.
 */
static void test_crc_sample_data(void)
{
	static const uint8_t DATA[10] = {
		0x4e, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09
	};
	static const uint8_t CRC[2] = { 0x6d, 0xd2 };
	char bits[(10 + 2) * 8];
	int byte, bit;

	for (byte = 0; byte < 10; byte++)
		for (bit = 0; bit < 8; bit++)
			bits[byte * 8 + bit] =
				(char) ((DATA[byte] >> bit) & 1);
	for (byte = 0; byte < 2; byte++)
		for (bit = 0; bit < 8; bit++)
			bits[(10 + byte) * 8 + bit] =
				(char) ((CRC[byte] >> bit) & 1);

	CHECK(crcgen(bits, 10 * 8, 0x47) == air_to_host16(&bits[10 * 8], 16));
	CHECK(crcgen(bits, 10 * 8, 0x47) == 0xd26d);

	/* a wrong UAP does not produce the published CRC */
	CHECK(crcgen(bits, 10 * 8, 0x46) != 0xd26d);
}

/* ------------------------------------------------------------------ *
 * Section 8: whitening sequence sample data
 * ------------------------------------------------------------------ */

/*
 * Vol 2, Part G, Section 8 prints the whitening generator's output. The
 * register starts all ones there, which is CLK6-1 = 0x3f, and the sequence
 * repeats after 127 bits.
 */
static const uint8_t WHITENING_SEQUENCE[127] = {
	1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1,
	0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,
	1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0,
	1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0,
	0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0,
	1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1
};

static void test_whitening_sample_data(void)
{
	char bits[127];
	int clock, i;

	/*
	 * spec_whiten() XORs, so whitening a run of zeros prints the
	 * generator's output. CLK6-1 = 0x3f is the published starting state.
	 */
	memset(bits, 0, sizeof(bits));
	spec_whiten(bits, 127, 0x3f, 0);
	for (i = 0; i < 127; i++)
		CHECK(bits[i] == (char) WHITENING_SEQUENCE[i]);

	/* and the library's table is the same sequence at the same phase */
	for (i = 0; i < 127; i++)
		CHECK(WHITENING_DATA[(INDICES[0x3f] + i) % 127] ==
		      WHITENING_SEQUENCE[i]);

	/*
	 * Section 7.2 initializes the register with CLK1 in position 0, CLK2
	 * in position 1 and so on, so every one of the 64 values of CLK6-1
	 * picks a phase of the same 127-bit sequence. unwhiten() has to agree
	 * with the generator for all of them, not just the published one.
	 */
	for (clock = 0; clock < 64; clock++) {
		char expected[127];
		char got[127];
		btbb_packet *pkt = btbb_packet_new();

		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);

		memset(expected, 0, sizeof(expected));
		spec_whiten(expected, 127, clock, 0);
		memset(bits, 0, sizeof(bits));
		unwhiten(bits, got, clock, 127, 0, pkt);

		for (i = 0; i < 127; i++)
			CHECK(got[i] == expected[i]);

		/*
		 * The payload continues from the state the register had at
		 * the end of the HEC, which is the skip argument: 18 header
		 * bits already consumed.
		 */
		memset(expected, 0, sizeof(expected));
		spec_whiten(expected, 100, clock, 18);
		memset(bits, 0, sizeof(bits));
		unwhiten(bits, got, clock, 100, 18, pkt);
		for (i = 0; i < 100; i++)
			CHECK(got[i] == expected[i]);

		btbb_packet_unref(pkt);
	}
}

/* ------------------------------------------------------------------ *
 * Section 9: FEC sample data
 * ------------------------------------------------------------------ */

/*
 * Vol 2, Part G, Section 9 prints the rate 2/3 codeword of each single-bit
 * data value. tests/test_internal.c drives unfec23() with them; here they pin
 * fec23() and spec_fec23(), which build the FEC encoded payloads below.
 */
static void test_fec23_sample_data(void)
{
	int i, bit;

	for (i = 0; i < 10; i++) {
		char data[10];
		char encoded[15];

		CHECK(fec23((uint16_t) (1 << i)) == FEC23_CODEWORDS[i]);

		memset(data, 0, sizeof(data));
		data[i] = 1;
		spec_fec23(data, encoded, 10);
		for (bit = 0; bit < 15; bit++)
			CHECK(encoded[bit] ==
			      (char) ((FEC23_CODEWORDS[i] >> bit) & 1));
	}
}

/*
 * Section 7.4: the header's rate 1/3 FEC is each bit sent three times, and the
 * decoder takes the majority of each triple, so it corrects any single wrong
 * symbol.
 */
static void test_fec13_single_symbol_correction(void)
{
	static const uint32_t DATA[] = { 0x00000, 0x3ffff, 0x12345, 0x2aaaa };
	size_t row;
	int bit, position;

	for (row = 0; row < sizeof(DATA) / sizeof(DATA[0]); row++) {
		char encoded[54];
		char decoded[18];

		for (bit = 0; bit < 18; bit++) {
			char value = (char) ((DATA[row] >> bit) & 1);

			encoded[bit * 3] = value;
			encoded[bit * 3 + 1] = value;
			encoded[bit * 3 + 2] = value;
		}

		CHECK(unfec13(encoded, decoded, 18));
		CHECK(air_to_host32(decoded, 18) == (DATA[row] & 0x3ffff));

		for (position = 0; position < 54; position++) {
			encoded[position] ^= 1;
			CHECK(unfec13(encoded, decoded, 18));
			CHECK(air_to_host32(decoded, 18) ==
			      (DATA[row] & 0x3ffff));
			encoded[position] ^= 1;
		}
	}
}

/* ------------------------------------------------------------------ *
 * Section 6: complete sample packets
 * ------------------------------------------------------------------ */

/*
 * Put an access code, a trailer and a rate 1/3 encoded packet header in front
 * of a payload, which is what the decoder expects: symbols 0 to 63 are the
 * sync word, 64 to 67 the trailer, 68 to 121 the header, and the payload
 * starts at 122.
 */
static int build_packet(char *stream, uint32_t lap, uint16_t header_data,
			uint8_t hec, const char *payload, int payload_symbols,
			int clock, int whitened)
{
	uint64_t syncword = btbb_gen_syncword((int) lap);
	uint32_t header = header_data | ((uint32_t) hec << 10);
	char header_bits[18];
	int i;

	for (i = 0; i < 64; i++)
		stream[i] = (char) ((syncword >> i) & 1);
	/* the trailer alternates, starting opposite the last sync word symbol */
	for (i = 0; i < 4; i++)
		stream[64 + i] = (char) (((syncword >> 63) ^ (i + 1)) & 1);

	for (i = 0; i < 18; i++)
		header_bits[i] = (char) ((header >> i) & 1);
	/* Section 7.2: the header is the start of the whitening run, so the
	 * payload the caller built continues from bit 18. */
	if (whitened)
		spec_whiten(header_bits, 18, clock, 0);
	for (i = 0; i < 18; i++) {
		stream[68 + i * 3] = header_bits[i];
		stream[68 + i * 3 + 1] = header_bits[i];
		stream[68 + i * 3 + 2] = header_bits[i];
	}

	if (payload_symbols > 0)
		memcpy(stream + 122, payload, (size_t) payload_symbols);

	return 122 + payload_symbols;
}

/* Hand a built packet to the decoder with the clock and UAP it was built for. */
static btbb_packet *packet_for(const char *stream, int length, uint8_t uap,
			       int clock, int whitened)
{
	btbb_packet *pkt = btbb_packet_new();

	if (pkt == NULL)
		return NULL;
	btbb_packet_set_data(pkt, (char *) stream, length, 0,
			     (uint32_t) clock << 1);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, whitened);
	btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
	btbb_packet_set_uap(pkt, uap);
	return pkt;
}

/* Read a string of '0' and '1' into one symbol per char. */
static int symbols_from_string(const char *text, char *out)
{
	int i;

	for (i = 0; text[i] != '\0'; i++)
		out[i] = (char) (text[i] - '0');
	return i;
}

/*
 * Vol 2, Part G, Section 6.1: a complete DH1 packet, written as the symbols
 * that go on air, least significant bit first, with no whitening. The header
 * is LT_ADDR 011, TYPE 0100 (DH1), FLOW 0, ARQN 1, SEQN 0 with a UAP of 0x47;
 * the payload is a one byte payload header (logical channel 10, flow 1,
 * length 5), five data bytes 01 to 05, and the CRC.
 */
static void test_dh1_sample_packet(void)
{
	static const char HEADER[] =
		"111111000" "000000111000" "000111000"
		"000111111000000000000000";
	static const char PAYLOAD[] =
		"01110100"	/* payload header */
		"10000000" "01000000" "11000000" "00100000" "10100000"
		"1110110000110110";	/* CRC */
	char payload[sizeof(PAYLOAD)];
	char header_symbols[sizeof(HEADER)];
	char stream[MAX_SYMBOLS];
	char packed[8];
	btbb_packet *pkt;
	int payload_symbols, length, i;

	/* the published header is the rate 1/3 encoding of 0x123 and HEC 0x06,
	 * which is one of the Section 4 rows for a UAP of 0x47 */
	symbols_from_string(HEADER, header_symbols);
	{
		char decoded[18];

		CHECK(unfec13(header_symbols, decoded, 18));
		CHECK(air_to_host16(decoded, 10) == 0x123);
		CHECK(air_to_host8(&decoded[10], 8) == 0x06);
		CHECK(spec_hec(0x123, 0x47) == 0x06);
	}

	payload_symbols = symbols_from_string(PAYLOAD, payload);
	CHECK(payload_symbols == 8 * 8);
	length = build_packet(stream, 0x9e8b33, 0x123, 0x06, payload,
			      payload_symbols, 0, 0);

	pkt = packet_for(stream, length, 0x47, 0, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	CHECK(btbb_header_present(pkt));
	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_packet_get_lt_addr(pkt) == 3);
	CHECK(btbb_packet_get_type(pkt) == PACKET_TYPE_DH1);
	/* FLOW 0, ARQN 1, SEQN 0, in that bit order */
	CHECK(btbb_packet_get_header_flags(pkt) == 2);
	CHECK(btbb_packet_get_hec(pkt) == 0x06);

	/* 10 is a CRC match, the strongest result btbb_decode_payload() gives */
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == 8);
	CHECK(pkt->payload_header_length == 1);
	CHECK(pkt->payload_llid == 2);
	CHECK(pkt->payload_flow == 1);

	CHECK(btbb_get_payload_packed(pkt, packed) == 8);
	CHECK((uint8_t) packed[0] == 0x2e);
	for (i = 0; i < 5; i++)
		CHECK((uint8_t) packed[1 + i] == (uint8_t) (i + 1));
	CHECK((uint8_t) packed[6] == 0x37);
	CHECK((uint8_t) packed[7] == 0x6c);

	btbb_packet_unref(pkt);
}

/*
 * Vol 2, Part G, Section 6.2: the same payload as a DM1 packet, so the payload
 * is rate 2/3 encoded. The spaces in the published codewords separate the ten
 * data bits from the five parity bits of each block; the last block carries
 * the six zero tail bits Section 7.5 requires.
 */
static void test_dm1_sample_packet(void)
{
	static const char HEADER[] =
		"111111000" "111111000000" "000111000"
		"111000000111111111111000";
	static const char PAYLOAD[] =
		"0111010010" "11001"
		"0000000100" "01011"
		"0000110000" "11110"
		"0000100000" "00111"
		"1010000011" "01100"
		"1011000011" "00010"
		"0110000000" "10001";
	char payload[sizeof(PAYLOAD)];
	char header_symbols[sizeof(HEADER)];
	char stream[MAX_SYMBOLS];
	char packed[8];
	btbb_packet *pkt;
	int payload_symbols, length, i;

	symbols_from_string(HEADER, header_symbols);
	{
		char decoded[18];

		CHECK(unfec13(header_symbols, decoded, 18));
		CHECK(air_to_host16(decoded, 10) == 0x11b);
		CHECK(air_to_host8(&decoded[10], 8) == 0x79);
		CHECK(spec_hec(0x11b, 0x47) == 0x79);
	}

	payload_symbols = symbols_from_string(PAYLOAD, payload);
	CHECK(payload_symbols == 7 * 15);

	/* the published codewords are what spec_fec23() produces */
	{
		char data[70];
		char encoded[105];

		memcpy(data, payload, 0);
		for (i = 0; i < 7; i++)
			memcpy(data + i * 10, payload + i * 15, 10);
		spec_fec23(data, encoded, 70);
		for (i = 0; i < 105; i++)
			CHECK(encoded[i] == payload[i]);
	}

	length = build_packet(stream, 0x9e8b33, 0x11b, 0x79, payload,
			      payload_symbols, 0, 0);
	pkt = packet_for(stream, length, 0x47, 0, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_packet_get_type(pkt) == PACKET_TYPE_DM1);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == 8);
	CHECK(pkt->payload_llid == 2);
	CHECK(pkt->payload_flow == 1);

	CHECK(btbb_get_payload_packed(pkt, packed) == 8);
	CHECK((uint8_t) packed[0] == 0x2e);
	for (i = 0; i < 5; i++)
		CHECK((uint8_t) packed[1 + i] == (uint8_t) (i + 1));

	btbb_packet_unref(pkt);
}

/* ------------------------------------------------------------------ *
 * Payload lengths per packet type (Vol 2, Part B, Section 6.5)
 * ------------------------------------------------------------------ */

/*
 * The length each type allows, from Vol 2, Part B, Sections 6.5.2 to 6.5.4.
 * body is the largest number of data bytes after the payload header, and
 * payload_length is what btbb_packet_get_payload_length() should then report:
 * the payload header, the data and the CRC where there is one.
 */
static const struct {
	const char *name;
	uint8_t type;
	int header_bytes;
	int body;		/* largest data field, bytes */
	int has_crc;
	int fec23;
	int payload_length;	/* expected btbb payload length */
} ACL_TYPES[] = {
	{ "DM1",  PACKET_TYPE_DM1,  1,  17, 1, 1,  20 },
	{ "DH1",  PACKET_TYPE_DH1,  1,  27, 1, 0,  30 },
	{ "DM3",  PACKET_TYPE_DM3,  2, 121, 1, 1, 125 },
	{ "DH3",  PACKET_TYPE_DH3,  2, 183, 1, 0, 187 },
	{ "DM5",  PACKET_TYPE_DM5,  2, 224, 1, 1, 228 },
	{ "DH5",  PACKET_TYPE_DH5,  2, 339, 1, 0, 343 },
	{ "AUX1", PACKET_TYPE_AUX1, 1,  29, 0, 0,  30 },
	/* a short AUX1 too: at the longest body the clamp would hide a length
	 * that wrongly counted a CRC this type does not carry */
	{ "AUX1", PACKET_TYPE_AUX1, 1,   9, 0, 0,  10 }
};

#define N_ACL_TYPES ((int) (sizeof(ACL_TYPES) / sizeof(ACL_TYPES[0])))

/*
 * Build the payload of an ACL packet: the payload header with the given
 * length, body bytes counting up from one, the CRC where the type has one,
 * then rate 2/3 FEC where the type uses it, all whitened for the given clock
 * from bit 18, where the header left the whitening register.
 */
static int build_acl_payload(char *out, int header_bytes, int body,
			     int has_crc, int fec23_coded, uint8_t uap,
			     int clock)
{
	char bits[(343 + 1) * 8];
	int total_bytes = header_bytes + body + (has_crc ? 2 : 0);
	int data_bits = total_bytes * 8;
	int i, bit;
	uint16_t crc;

	memset(bits, 0, sizeof(bits));

	/* logical channel 10 (an L2CAP start), flow 1, then the length */
	bits[0] = 0;
	bits[1] = 1;
	bits[2] = 1;
	for (bit = 0; bit < (header_bytes == 1 ? 5 : 10); bit++)
		bits[3 + bit] = (char) ((body >> bit) & 1);

	for (i = 0; i < body; i++) {
		uint8_t value = (uint8_t) (i + 1);

		for (bit = 0; bit < 8; bit++)
			bits[(header_bytes + i) * 8 + bit] =
				(char) ((value >> bit) & 1);
	}

	if (has_crc) {
		crc = crcgen(bits, (header_bytes + body) * 8, uap);
		for (bit = 0; bit < 16; bit++)
			bits[(header_bytes + body) * 8 + bit] =
				(char) ((crc >> bit) & 1);
	}

	spec_whiten(bits, data_bits, clock, 18);

	if (!fec23_coded) {
		memcpy(out, bits, (size_t) data_bits);
		return data_bits;
	}

	/* Section 7.5: zero tail bits bring the data up to a multiple of ten */
	{
		int blocks = (data_bits + 9) / 10;

		for (i = data_bits; i < blocks * 10; i++)
			bits[i] = 0;
		spec_fec23(bits, out, blocks * 10);
		return blocks * 15;
	}
}

static void test_acl_payload_lengths(void)
{
	int row;

	for (row = 0; row < N_ACL_TYPES; row++) {
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		char packed[MAX_PAYLOAD_LENGTH / 8];
		uint16_t header_data;
		uint8_t hec;
		btbb_packet *pkt;
		int payload_symbols, length, rv, i;
		const int clock = 0x2a;

		payload_symbols = build_acl_payload(payload,
						    ACL_TYPES[row].header_bytes,
						    ACL_TYPES[row].body,
						    ACL_TYPES[row].has_crc,
						    ACL_TYPES[row].fec23,
						    0x47, clock);

		/* LT_ADDR 1, the type under test, FLOW 1 ARQN 1 SEQN 0 */
		header_data = (uint16_t) (1 | (ACL_TYPES[row].type << 3) |
					  (3 << 7));
		hec = spec_hec(header_data, 0x47);
		length = build_packet(stream, 0x9e8b33, header_data, hec,
				      payload, payload_symbols, clock, 1);

		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(btbb_packet_get_type(pkt) == ACL_TYPES[row].type);

		rv = btbb_decode_payload(pkt);
		printf("  %s: %d byte body, payload length %d\n",
		       ACL_TYPES[row].name, ACL_TYPES[row].body,
		       btbb_packet_get_payload_length(pkt));
		/* 10 is a CRC match; AUX1 has no CRC, so 2 is as good as it
		 * gets for it */
		CHECK(rv == (ACL_TYPES[row].has_crc ? 10 : 2));
		CHECK(btbb_packet_get_payload_length(pkt) ==
		      ACL_TYPES[row].payload_length);
		CHECK(btbb_get_payload_packed(pkt, packed) ==
		      ACL_TYPES[row].payload_length);
		for (i = 0; i < ACL_TYPES[row].body; i++)
			CHECK((uint8_t) packed[ACL_TYPES[row].header_bytes + i]
			      == (uint8_t) (i + 1));

		btbb_packet_unref(pkt);
	}
}

/*
 * A payload header claiming more than its type allows is clamped to the
 * largest the type can carry, so that the length never sends the payload
 * reader past the buffer. Section 6.5.4 gives the limits.
 */
static void test_acl_payload_length_clamped(void)
{
	static const struct {
		uint8_t type;
		int header_bytes;
		int claimed;
		int clamped;
	} CASES[] = {
		{ PACKET_TYPE_DM1, 1,  31,  20 },
		{ PACKET_TYPE_DH1, 1,  31,  30 },
		{ PACKET_TYPE_DM3, 2, 999, 125 },
		{ PACKET_TYPE_DH3, 2, 999, 187 },
		{ PACKET_TYPE_DM5, 2, 999, 228 },
		{ PACKET_TYPE_DH5, 2, 999, 343 },
		{ PACKET_TYPE_AUX1, 1, 31,  30 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		uint16_t header_data;
		btbb_packet *pkt;
		int length, bit;

		memset(payload, 0, sizeof(payload));
		payload[1] = 1;
		payload[2] = 1;
		for (bit = 0; bit < (CASES[row].header_bytes == 1 ? 5 : 10);
		     bit++)
			payload[3 + bit] =
				(char) ((CASES[row].claimed >> bit) & 1);

		header_data = (uint16_t) (1 | (CASES[row].type << 3) |
					  (3 << 7));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      MAX_SYMBOLS - 122, 0, 0);

		pkt = packet_for(stream, length, 0x47, 0, 0);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_header(pkt) == 1);
		btbb_decode_payload(pkt);
		CHECK(btbb_packet_get_payload_length(pkt) <=
		      CASES[row].clamped);
		btbb_packet_unref(pkt);
	}
}

/*
 * A capture that stops partway through an ACL payload holds no payload the
 * decoder can read, so it must report none: btbb_decode_payload() marks the
 * packet as having a payload whatever it returns, so a length left over from
 * the payload header reaches the dump and the capture files as bytes nothing
 * decoded.
 *
 * The rate 2/3 types are where the bound is easy to get wrong. Their payload
 * arrives in half again as many symbols as it has bits, so a capture can hold
 * as many symbols as the payload has bits and still be short of it.
 */
static void test_acl_truncated_capture(void)
{
	static const struct {
		const char *name;
		uint8_t type;
		int header_bytes;
		int body;
		int fec23;
	} CASES[] = {
		{ "DM1", PACKET_TYPE_DM1, 1,  17, 1 },
		{ "DH1", PACKET_TYPE_DH1, 1,  27, 0 },
		{ "DM3", PACKET_TYPE_DM3, 2, 121, 1 },
		{ "DH5", PACKET_TYPE_DH5, 2, 339, 0 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		uint16_t header_data;
		int payload_symbols, full_length, cut;
		const int clock = 0x2a;

		payload_symbols = build_acl_payload(payload,
						    CASES[row].header_bytes,
						    CASES[row].body, 1,
						    CASES[row].fec23, 0x47,
						    clock);
		header_data = (uint16_t) (1 | (CASES[row].type << 3) |
					  (3 << 7));
		full_length = build_packet(stream, 0x9e8b33, header_data,
					   spec_hec(header_data, 0x47),
					   payload, payload_symbols, clock, 1);

		/* every length from no payload at all to one symbol short */
		for (cut = 122; cut < full_length; cut++) {
			btbb_packet *pkt = packet_for(stream, cut, 0x47,
						      clock, 1);
			int rv;

			CHECK(pkt != NULL);
			if (pkt == NULL)
				return;
			CHECK(btbb_decode_header(pkt) == 1);

			rv = btbb_decode_payload(pkt);
			if (rv == 10 ||
			    btbb_packet_get_payload_length(pkt) != 0) {
				printf("FAIL %s:%d: %s cut to %d of %d "
				       "symbols decodes to %d and reports "
				       "%d bytes\n", __FILE__, __LINE__,
				       CASES[row].name, cut, full_length, rv,
				       btbb_packet_get_payload_length(pkt));
				++failures;
			}
			btbb_packet_unref(pkt);
		}
	}
}

/*
 * Make one rate 2/3 FEC block uncorrectable. Two of a block's check symbols
 * disagree with its data, which is neither the no-error case nor one of the
 * single bit errors the code can put right, so unfec23() gives the block up.
 */
static void wreck_fec23_block(char *payload, int block)
{
	payload[block * 15 + 10] ^= 1;
	payload[block * 15 + 11] ^= 1;
}

/*
 * A payload block the error correction cannot recover leaves nothing
 * decoded, and the decoder has to report nothing rather than the length the
 * payload header claimed. Noise does this far more often than a capture
 * ending early, and during clock recovery crc_check() is called for each
 * candidate clock in turn, so what is left in the payload buffer is the
 * previous candidate's decode.
 */
static void test_uncorrectable_payload_fec(void)
{
	char payload[MAX_SYMBOLS];
	char stream[MAX_SYMBOLS];
	uint16_t header_data;
	btbb_packet *pkt;
	int payload_symbols, length, i;
	const int clock = 0x2a;

	/*
	 * A DM1, whose payload header and body are both rate 2/3 coded. The
	 * header is the first block, so wrecking that block and wrecking a
	 * body block fail in different places.
	 */
	for (i = 0; i < 2; i++) {
		const int block = i == 0 ? 0 : 3;

		payload_symbols = build_acl_payload(payload, 1, 17, 1, 1,
						    0x47, clock);
		wreck_fec23_block(payload, block);

		header_data = (uint16_t) (1 | (PACKET_TYPE_DM1 << 3) |
					  (3 << 7));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      payload_symbols, clock, 1);
		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(btbb_decode_payload(pkt) == 0);
		CHECK(btbb_packet_get_payload_length(pkt) == 0);
		btbb_packet_unref(pkt);
	}

	/* an FHS, 20 bytes under rate 2/3 FEC with no payload header */
	{
		char bits[20 * 8 + 10];
		uint16_t crc;
		int bit;

		memset(bits, 0, sizeof(bits));
		crc = crcgen(bits, 18 * 8, 0x47);
		for (bit = 0; bit < 16; bit++)
			bits[18 * 8 + bit] = (char) ((crc >> bit) & 1);
		spec_whiten(bits, 20 * 8, clock, 18);
		spec_fec23(bits, payload, 160);
		wreck_fec23_block(payload, 5);

		header_data = (uint16_t) (1 | (PACKET_TYPE_FHS << 3));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      240, clock, 1);
		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(btbb_decode_payload(pkt) == 0);
		CHECK(btbb_packet_get_payload_length(pkt) == 0);
		btbb_packet_unref(pkt);
	}

	/*
	 * The synchronous types: HV2 under rate 2/3, and HV1 under rate 1/3,
	 * where the three symbols of a bit have to disagree often enough that
	 * the decode gives up rather than take the majority.
	 */
	{
		char bits[240];
		int row;

		for (row = 0; row < 2; row++) {
			uint8_t type = row == 0 ? PACKET_TYPE_HV2
						: PACKET_TYPE_HV1;
			int bytes = row == 0 ? 20 : 10;

			memset(bits, 0, sizeof(bits));
			spec_whiten(bits, bytes * 8, clock, 18);
			if (type == PACKET_TYPE_HV2) {
				spec_fec23(bits, payload, 160);
				wreck_fec23_block(payload, 5);
			} else {
				for (i = 0; i < 80; i++) {
					payload[i * 3] = bits[i];
					payload[i * 3 + 1] = bits[i];
					payload[i * 3 + 2] = bits[i];
				}
				/* 20 of the 80 bits is where unfec13() gives
				 * up, so disagree about 25 of them */
				for (i = 0; i < 25; i++)
					payload[i * 3] ^= 1;
			}

			header_data = (uint16_t) (1 | (type << 3));
			length = build_packet(stream, 0x9e8b33, header_data,
					      spec_hec(header_data, 0x47),
					      payload, 240, clock, 1);
			pkt = packet_for(stream, length, 0x47, clock, 1);
			CHECK(pkt != NULL);
			if (pkt == NULL)
				return;
			CHECK(btbb_decode_header(pkt) == 1);
			CHECK(HV(clock, pkt) == 0);
			CHECK(btbb_packet_get_payload_length(pkt) == 0);
			btbb_packet_unref(pkt);
		}
	}

	/*
	 * An EV4 decodes block by block and has no payload header to tell it
	 * the length, so a lost block ends the payload: the bytes before it
	 * are decoded and are all there is to report.
	 */
	{
		char bits[(120 + 2) * 8 + 10];
		int bit;

		memset(bits, 0, sizeof(bits));
		spec_whiten(bits, 122 * 8, clock, 18);
		for (bit = 122 * 8; bit < 98 * 10; bit++)
			bits[bit] = 0;
		spec_fec23(bits, payload, 98 * 10);
		wreck_fec23_block(payload, 3);

		header_data = (uint16_t) (1 | (PACKET_TYPE_EV4 << 3));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      98 * 15, clock, 1);
		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(EV4(clock, pkt) == 1);
		/* three blocks decoded, 30 bits, so three whole bytes */
		CHECK(btbb_packet_get_payload_length(pkt) == 3);
		btbb_packet_unref(pkt);
	}
}

/*
 * When no length agrees with the CRC, an eSCO decoder reports the longest its
 * type allows, every byte of which it did decode. It must not report the one
 * more than that its search ended on.
 */
static void test_esco_no_length_matches(void)
{
	static const struct {
		const char *name;
		uint8_t type;
		int bytes;
		int fec23;
	} CASES[] = {
		{ "EV3", PACKET_TYPE_HV3,  32, 0 },
		{ "EV4", PACKET_TYPE_EV4, 122, 1 },
		{ "EV5", PACKET_TYPE_EV5, 182, 0 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		char bits[182 * 8 + 10];
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		uint16_t header_data;
		btbb_packet *pkt;
		int data_bits = CASES[row].bytes * 8;
		int payload_symbols, length, i, rv;
		const int clock = 0x33;

		/* every byte 0xff, so no length's last two bytes are a CRC
		 * over the bytes before them */
		memset(bits, 0, sizeof(bits));
		for (i = 0; i < data_bits; i++)
			bits[i] = 1;
		spec_whiten(bits, data_bits, clock, 18);

		if (CASES[row].fec23) {
			int blocks = (data_bits + 9) / 10;

			for (i = data_bits; i < blocks * 10; i++)
				bits[i] = 0;
			spec_fec23(bits, payload, blocks * 10);
			payload_symbols = blocks * 15;
		} else {
			memcpy(payload, bits, (size_t) data_bits);
			payload_symbols = data_bits;
		}

		header_data = (uint16_t) (1 | (CASES[row].type << 3));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      payload_symbols, clock, 1);
		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_header(pkt) == 1);

		if (CASES[row].type == PACKET_TYPE_EV4)
			rv = EV4(clock, pkt);
		else if (CASES[row].type == PACKET_TYPE_EV5)
			rv = EV5(clock, pkt);
		else
			rv = EV3(clock, pkt);

		CHECK(rv == 2);
		CHECK(btbb_packet_get_payload_length(pkt) ==
		      CASES[row].bytes);
		btbb_packet_unref(pkt);
	}
}

/*
 * The access code and the packet header are the first 122 symbols of a
 * packet, so a capture that stops inside them holds no header to decode.
 * Without a length check the decoder reports one anyway: each header bit is
 * sent three times, and the error correction takes the majority, so two
 * symbols of the last bit still give the bit the sender set.
 */
static void test_header_needs_its_symbols(void)
{
	char stream[MAX_SYMBOLS];
	uint16_t header_data = (uint16_t) (1 | (PACKET_TYPE_DM1 << 3) |
					   (3 << 7));
	const int clock = 0x2a;
	int cut;

	build_packet(stream, 0x9e8b33, header_data,
		     spec_hec(header_data, 0x47), NULL, 0, clock, 1);

	for (cut = 0; cut < 122; cut++) {
		btbb_packet *pkt = packet_for(stream, cut, 0x47, clock, 1);

		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_header_present(pkt) == 0);
		CHECK(btbb_decode_header(pkt) == 0);
		CHECK(try_clock(clock, pkt) == 0);
		btbb_packet_unref(pkt);
	}
}

/*
 * The number of symbols a packet was given is stored unsigned, so a count
 * below zero, which a caller reaches by subtracting an offset from a
 * capture's length, must not come back out as a very large one: every decoder
 * would then work from symbols the caller never provided.
 */
static void test_negative_symbol_count(void)
{
	char stream[MAX_SYMBOLS];
	btbb_packet *pkt = btbb_packet_new();

	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	memset(stream, 0, sizeof(stream));
	btbb_packet_set_data(pkt, stream, -1, 0, 0);
	CHECK(pkt->length == 0);
	CHECK(btbb_header_present(pkt) == 0);
	CHECK(btbb_decode_header(pkt) == 0);
	CHECK(try_clock(0, pkt) == 0);

	btbb_packet_unref(pkt);
}

/*
 * The synchronous types carry a fixed length payload with no payload header:
 * HV1 10 bytes under rate 1/3 FEC, HV2 20 under rate 2/3, HV3 30 with none
 * (Vol 2, Part B, Section 6.5.2).
 */
static void test_sco_payload_lengths(void)
{
	static const struct {
		uint8_t type;
		int bytes;
	} CASES[] = {
		{ PACKET_TYPE_HV1, 10 },
		{ PACKET_TYPE_HV2, 20 },
		{ PACKET_TYPE_HV3, 30 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		char bits[240];
		char payload[240];
		char stream[MAX_SYMBOLS];
		char packed[30];
		uint16_t header_data;
		btbb_packet *pkt;
		int length, i, bit, payload_symbols;
		const int clock = 0x15;

		for (i = 0; i < CASES[row].bytes; i++) {
			uint8_t value = (uint8_t) (0xa0 + i);

			for (bit = 0; bit < 8; bit++)
				bits[i * 8 + bit] =
					(char) ((value >> bit) & 1);
		}
		spec_whiten(bits, CASES[row].bytes * 8, clock, 18);

		if (CASES[row].type == PACKET_TYPE_HV1) {
			for (i = 0; i < 80; i++) {
				payload[i * 3] = bits[i];
				payload[i * 3 + 1] = bits[i];
				payload[i * 3 + 2] = bits[i];
			}
		} else if (CASES[row].type == PACKET_TYPE_HV2) {
			spec_fec23(bits, payload, 160);
		} else {
			memcpy(payload, bits, 240);
		}
		payload_symbols = 240;

		header_data = (uint16_t) (1 | (CASES[row].type << 3));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      payload_symbols, clock, 1);

		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;

		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(HV(clock, pkt) == 2);
		CHECK(btbb_packet_get_payload_length(pkt) == CASES[row].bytes);
		CHECK(btbb_get_payload_packed(pkt, packed) ==
		      CASES[row].bytes);
		for (i = 0; i < CASES[row].bytes; i++)
			CHECK((uint8_t) packed[i] == (uint8_t) (0xa0 + i));

		btbb_packet_unref(pkt);
	}
}

/*
 * The eSCO types carry 1 to 30 (EV3), 1 to 120 (EV4) or 1 to 180 (EV5)
 * information bytes plus a CRC, with no payload header, so the decoder has to
 * find the length by trying the CRC at each one (Vol 2, Part B, Section 6.5.3).
 */
static void test_esco_payload_lengths(void)
{
	static const struct {
		const char *name;
		uint8_t type;
		int body;
		int fec23;
	} CASES[] = {
		{ "EV3",  PACKET_TYPE_HV3,   1, 0 },
		{ "EV3",  PACKET_TYPE_HV3,  30, 0 },
		{ "EV4",  PACKET_TYPE_EV4,   1, 1 },
		{ "EV4",  PACKET_TYPE_EV4, 120, 1 },
		{ "EV5",  PACKET_TYPE_EV5,   1, 0 },
		{ "EV5",  PACKET_TYPE_EV5, 180, 0 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		char bits[(180 + 2) * 8 + 10];
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		char packed[MAX_PAYLOAD_LENGTH / 8];
		uint16_t header_data;
		btbb_packet *pkt;
		int data_bits = (CASES[row].body + 2) * 8;
		int length, i, bit, payload_symbols;
		uint16_t crc;
		const int clock = 0x31;

		memset(bits, 0, sizeof(bits));
		for (i = 0; i < CASES[row].body; i++) {
			uint8_t value = (uint8_t) (0x10 + i);

			for (bit = 0; bit < 8; bit++)
				bits[i * 8 + bit] =
					(char) ((value >> bit) & 1);
		}
		crc = crcgen(bits, CASES[row].body * 8, 0x47);
		for (bit = 0; bit < 16; bit++)
			bits[CASES[row].body * 8 + bit] =
				(char) ((crc >> bit) & 1);
		spec_whiten(bits, data_bits, clock, 18);

		if (CASES[row].fec23) {
			int blocks = (data_bits + 9) / 10;

			spec_fec23(bits, payload, blocks * 10);
			payload_symbols = blocks * 15;
		} else {
			memcpy(payload, bits, (size_t) data_bits);
			payload_symbols = data_bits;
		}

		header_data = (uint16_t) (1 | (CASES[row].type << 3));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), payload,
				      payload_symbols, clock, 1);

		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_header(pkt) == 1);

		printf("  %s, %d byte body\n", CASES[row].name,
		       CASES[row].body);
		if (CASES[row].type == PACKET_TYPE_EV4)
			CHECK(EV4(clock, pkt) == 10);
		else if (CASES[row].type == PACKET_TYPE_EV5)
			CHECK(EV5(clock, pkt) == 10);
		else
			CHECK(EV3(clock, pkt) == 10);
		CHECK(btbb_packet_get_payload_length(pkt) ==
		      CASES[row].body + 2);
		CHECK(btbb_get_payload_packed(pkt, packed) ==
		      CASES[row].body + 2);
		for (i = 0; i < CASES[row].body; i++)
			CHECK((uint8_t) packed[i] == (uint8_t) (0x10 + i));

		btbb_packet_unref(pkt);
	}
}

/*
 * A capture that stops partway through an eSCO payload has no length the CRC
 * agrees with, so the decoder reports what it managed to decode. It must not
 * report a byte the capture did not hold: btbb_decode_payload() marks the
 * packet as having a payload whatever the result, so a length past the
 * decoded bytes reaches the dump and the capture files.
 */
static void test_esco_truncated_capture(void)
{
	static const struct {
		const char *name;
		uint8_t type;
		int fec23;
	} CASES[] = {
		{ "EV3", PACKET_TYPE_HV3, 0 },
		{ "EV4", PACKET_TYPE_EV4, 1 },
		{ "EV5", PACKET_TYPE_EV5, 0 }
	};
	size_t row;

	for (row = 0; row < sizeof(CASES) / sizeof(CASES[0]); row++) {
		/*
		 * A body of this length was chosen because no shorter length
		 * of it happens to agree with the CRC, so every truncation
		 * below really has no length to find. Another body may not,
		 * and the check that nothing claims a CRC match would then
		 * fail for a reason that is not a bug.
		 */
		const int body = 24;
		char bits[(24 + 2) * 8 + 10];
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		uint16_t header_data;
		int data_bits = (body + 2) * 8;
		int payload_symbols, full_length, cut, i, bit;
		uint16_t crc;
		const int clock = 0x2d;

		memset(bits, 0, sizeof(bits));
		for (i = 0; i < body; i++)
			for (bit = 0; bit < 8; bit++)
				bits[i * 8 + bit] =
					(char) (((0x10 + i) >> bit) & 1);
		crc = crcgen(bits, body * 8, 0x47);
		for (bit = 0; bit < 16; bit++)
			bits[body * 8 + bit] = (char) ((crc >> bit) & 1);
		spec_whiten(bits, data_bits, clock, 18);

		if (CASES[row].fec23) {
			int blocks = (data_bits + 9) / 10;

			spec_fec23(bits, payload, blocks * 10);
			payload_symbols = blocks * 15;
		} else {
			memcpy(payload, bits, (size_t) data_bits);
			payload_symbols = data_bits;
		}

		header_data = (uint16_t) (1 | (CASES[row].type << 3));
		full_length = build_packet(stream, 0x9e8b33, header_data,
					   spec_hec(header_data, 0x47),
					   payload, payload_symbols, clock, 1);

		/* every length from no payload at all to one symbol short */
		for (cut = 122; cut < full_length; cut++) {
			btbb_packet *pkt = packet_for(stream, cut, 0x47,
						      clock, 1);
			int available = cut - 122;
			int rv;

			CHECK(pkt != NULL);
			if (pkt == NULL)
				return;
			CHECK(btbb_decode_header(pkt) == 1);

			if (CASES[row].type == PACKET_TYPE_EV4) {
				rv = EV4(clock, pkt);
				/* whole FEC blocks, 15 symbols for 10 bits */
				available = (available / 15) * 10;
			} else if (CASES[row].type == PACKET_TYPE_EV5) {
				rv = EV5(clock, pkt);
			} else {
				rv = EV3(clock, pkt);
			}

			/* no length can match, so nothing may claim one */
			CHECK(rv != 10);
			if (btbb_packet_get_payload_length(pkt) * 8 >
			    available) {
				printf("FAIL %s:%d: %s cut to %d symbols "
				       "reports %d bytes, %d bits decoded\n",
				       __FILE__, __LINE__, CASES[row].name,
				       cut,
				       btbb_packet_get_payload_length(pkt),
				       available);
				++failures;
			}
			btbb_packet_unref(pkt);
		}
	}
}

/*
 * The DV packet's payload is an 80 bit voice field followed by a data field of
 * 1 to 10 bytes plus a CRC (Vol 2, Part B, Section 6.5.2). Section 7.2 whitens
 * the whole payload in one run without reinitializing the register, so the
 * data field continues from bit 98: 18 header bits and 80 voice bits.
 */
static void test_dv_payload_at_clock(int clock)
{
	char bits[12 * 8 + 10];
	char payload[80 + 15 * 10];
	char stream[MAX_SYMBOLS];
	char packed[MAX_PAYLOAD_LENGTH / 8];
	uint16_t header_data;
	btbb_packet *pkt;
	const int body = 9;
	int data_bits = (1 + body + 2) * 8;
	int blocks = (data_bits + 9) / 10;
	int length, i, bit;
	uint16_t crc;

	memset(bits, 0, sizeof(bits));
	/* a one byte payload header: logical channel 10, flow 1, length 9 */
	bits[1] = 1;
	bits[2] = 1;
	for (bit = 0; bit < 5; bit++)
		bits[3 + bit] = (char) ((body >> bit) & 1);
	for (i = 0; i < body; i++) {
		uint8_t value = (uint8_t) (0x50 + i);

		for (bit = 0; bit < 8; bit++)
			bits[(1 + i) * 8 + bit] = (char) ((value >> bit) & 1);
	}
	crc = crcgen(bits, (1 + body) * 8, 0x47);
	for (bit = 0; bit < 16; bit++)
		bits[(1 + body) * 8 + bit] = (char) ((crc >> bit) & 1);

	/* the voice field comes first and is whitened too, so it is 80 bits of
	 * whitening the data field must not reuse */
	spec_whiten(bits, data_bits, clock, 18 + 80);

	memset(payload, 0, sizeof(payload));
	spec_whiten(payload, 80, clock, 18);
	spec_fec23(bits, payload + 80, blocks * 10);

	header_data = (uint16_t) (1 | (PACKET_TYPE_DV << 3) | (3 << 7));
	length = build_packet(stream, 0x9e8b33, header_data,
			      spec_hec(header_data, 0x47), payload,
			      80 + blocks * 15, clock, 1);

	pkt = packet_for(stream, length, 0x47, clock, 1);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_packet_get_type(pkt) == PACKET_TYPE_DV);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == 1 + body + 2);
	CHECK(btbb_get_payload_packed(pkt, packed) == 1 + body + 2);
	CHECK((uint8_t) packed[0] == 0x4e);
	for (i = 0; i < body; i++)
		CHECK((uint8_t) packed[1 + i] == (uint8_t) (0x50 + i));

	btbb_packet_unref(pkt);
}

/*
 * Whether the data field is dewhitened from the right place shows at some
 * clock values and not others, because at the rest the length field happens to
 * come out the same either way, so every clock is tried.
 */
static void test_dv_payload(void)
{
	int clock;

	for (clock = 0; clock < 64; clock++)
		test_dv_payload_at_clock(clock);
}

/*
 * The FHS payload is 144 information bits and a CRC, rate 2/3 encoded into 240
 * symbols (Vol 2, Part B, Section 6.5.1.4), and carries the sender's address
 * and clock, which the library reads back out.
 */
static void test_fhs_payload(void)
{
	char bits[20 * 8 + 10];
	char payload[300];
	char stream[MAX_SYMBOLS];
	uint16_t header_data;
	btbb_packet *pkt;
	const uint32_t lap = 0x123456;
	const uint8_t uap = 0x89;
	const uint16_t nap = 0xabcd;
	const uint32_t clk2_27 = 0x2345678;
	int length, bit;
	uint16_t crc;
	const int clock = 0x11;

	memset(bits, 0, sizeof(bits));
	for (bit = 0; bit < 24; bit++)
		bits[34 + bit] = (char) ((lap >> bit) & 1);
	for (bit = 0; bit < 8; bit++)
		bits[64 + bit] = (char) ((uap >> bit) & 1);
	for (bit = 0; bit < 16; bit++)
		bits[72 + bit] = (char) ((nap >> bit) & 1);
	for (bit = 0; bit < 26; bit++)
		bits[115 + bit] = (char) ((clk2_27 >> bit) & 1);

	crc = crcgen(bits, 18 * 8, 0x47);
	for (bit = 0; bit < 16; bit++)
		bits[18 * 8 + bit] = (char) ((crc >> bit) & 1);
	spec_whiten(bits, 20 * 8, clock, 18);
	spec_fec23(bits, payload, 160);

	header_data = (uint16_t) (1 | (PACKET_TYPE_FHS << 3));
	length = build_packet(stream, 0x9e8b33, header_data,
			      spec_hec(header_data, 0x47), payload, 240,
			      clock, 1);

	pkt = packet_for(stream, length, 0x47, clock, 1);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	CHECK(btbb_decode_header(pkt) == 1);
	CHECK(btbb_packet_get_type(pkt) == PACKET_TYPE_FHS);
	CHECK(btbb_decode_payload(pkt) == 1000);
	CHECK(btbb_packet_get_payload_length(pkt) == 20);
	CHECK(lap_from_fhs(pkt) == lap);
	CHECK(uap_from_fhs(pkt) == uap);
	CHECK(nap_from_fhs(pkt) == nap);
	CHECK(clock_from_fhs(pkt) == clk2_27);

	btbb_packet_unref(pkt);
}

/*
 * A payload cannot be shorter than the two byte CRC it carries plus the byte
 * that CRC covers, so there is no CRC to check below three bytes. Checking one
 * there reads from before the payload, which is how the eSCO length search
 * used to accept a packet it had not checked.
 */
static void test_payload_crc_needs_a_payload(void)
{
	btbb_packet *pkt = btbb_packet_new();

	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	/*
	 * A UAP of zero is the default check initialization of Section 1.2.1,
	 * and it is also the case a length check has to catch: the CRC of no
	 * data is then zero, and so is everything the check would be read
	 * from, so without the length check the two match and the packet is
	 * accepted having been checked against nothing.
	 */
	pkt->UAP = 0x00;
	memset(pkt->payload, 0, sizeof(pkt->payload));

	for (pkt->payload_length = 0; pkt->payload_length < 3;
	     pkt->payload_length++)
		CHECK(payload_crc(pkt) == 0);

	btbb_packet_unref(pkt);
}

/*
 * NULL and POLL packets have no payload at all, and an ID packet has no header
 * either, which btbb_header_present() is what tells apart.
 */
static void test_packets_without_payload(void)
{
	static const uint8_t TYPES[] = { PACKET_TYPE_NULL, PACKET_TYPE_POLL };
	size_t row;

	for (row = 0; row < sizeof(TYPES) / sizeof(TYPES[0]); row++) {
		char stream[MAX_SYMBOLS];
		uint16_t header_data = (uint16_t) (1 | (TYPES[row] << 3));
		btbb_packet *pkt;
		int length;

		memset(stream, 0, sizeof(stream));
		length = build_packet(stream, 0x9e8b33, header_data,
				      spec_hec(header_data, 0x47), NULL, 0,
				      0, 0);
		/* enough symbols for btbb_header_present() to look at */
		length = 122;

		pkt = packet_for(stream, length, 0x47, 0, 0);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_header_present(pkt));
		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(btbb_decode_payload(pkt) == 1);
		CHECK(btbb_packet_get_payload_length(pkt) == 0);
		btbb_packet_unref(pkt);
	}

	/* an ID packet is the access code and trailer and nothing else */
	{
		char stream[MAX_SYMBOLS];
		uint64_t syncword = btbb_gen_syncword(0x9e8b33);
		btbb_packet *pkt;
		int i;

		memset(stream, 0, sizeof(stream));
		for (i = 0; i < 64; i++)
			stream[i] = (char) ((syncword >> i) & 1);
		for (i = 0; i < 4; i++)
			stream[64 + i] =
				(char) (((syncword >> 63) ^ (i + 1)) & 1);
		/* random-looking symbols where the header would be */
		for (i = 68; i < 122; i++)
			stream[i] = (char) ((0x6b9d5e >> (i % 23)) & 1);

		pkt = packet_for(stream, 122, 0x47, 0, 0);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(!btbb_header_present(pkt));
		btbb_packet_unref(pkt);
	}
}

/*
 * UAP and clock recovery: try_clock() reads the header back at a candidate
 * CLK6-1 and returns the UAP the HEC implies, so the right clock is the one
 * that gives the UAP the packet was built with, and btbb_decode_header()
 * accepts that clock and no other.
 */
static void test_uap_and_clock_recovery(void)
{
	int clock;

	for (clock = 0; clock < 64; clock += 7) {
		char payload[MAX_SYMBOLS];
		char stream[MAX_SYMBOLS];
		uint16_t header_data =
			(uint16_t) (1 | (PACKET_TYPE_DH1 << 3) | (3 << 7));
		uint8_t hec = spec_hec(header_data, 0x47);
		btbb_packet *pkt;
		int payload_symbols, length, i, matches = 0;

		payload_symbols = build_acl_payload(payload, 1, 17, 1, 0,
						    0x47, clock);
		length = build_packet(stream, 0x9e8b33, header_data, hec,
				      payload, payload_symbols, clock, 1);

		pkt = packet_for(stream, length, 0x47, clock, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		/* the clock is what is being looked for, so start without it */
		btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 0);

		for (i = 0; i < 64; i++) {
			if (try_clock(i, pkt) == 0x47 &&
			    crc_check(i, pkt) > 1) {
				CHECK(i == clock);
				matches++;
			}
		}
		CHECK(matches == 1);

		pkt->clkn = (uint32_t) clock;
		btbb_packet_set_flag(pkt, BTBB_CLK6_VALID, 1);
		btbb_packet_set_uap(pkt, 0x47);
		CHECK(btbb_decode_header(pkt) == 1);
		CHECK(btbb_decode_payload(pkt) == 10);
		CHECK(btbb_packet_get_payload_length(pkt) == 20);

		btbb_packet_unref(pkt);
	}
}

int main(void)
{
	test_access_code_sample_data();
	test_access_code_search();
	test_find_ac_reads_only_what_it_promises();
	test_hec_sample_data();
	test_crc_sample_data();
	test_whitening_sample_data();
	test_fec23_sample_data();
	test_fec13_single_symbol_correction();
	test_dh1_sample_packet();
	test_dm1_sample_packet();
	test_acl_payload_lengths();
	test_acl_payload_length_clamped();
	test_acl_truncated_capture();
	test_uncorrectable_payload_fec();
	test_esco_no_length_matches();
	test_header_needs_its_symbols();
	test_negative_symbol_count();
	test_sco_payload_lengths();
	test_esco_payload_lengths();
	test_esco_truncated_capture();
	test_dv_payload();
	test_payload_crc_needs_a_payload();
	test_fhs_payload();
	test_packets_without_payload();
	test_uap_and_clock_recovery();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
