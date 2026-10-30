/* -*- c -*- */
/*
 * The length an eSCO payload is decoded at, and the HV3 that shares a type
 * code with an EV3.
 *
 * Neither length is in the packet, so both are worked out from the payload.
 * These tests build payloads that make the wrong answer visible: one with
 * three lengths whose CRC agrees, and a voice payload with no CRC at all.
 *
 * The decoders are internal to the library and have no external linkage, so
 * the test compiles the translation unit they live in.
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

/* the upper address part the CRC is seeded with */
#define TEST_UAP 0x47

/* where a payload starts in a symbol stream: the access code and the header */
#define PAYLOAD_OFFSET 122

/* a payload byte, least significant bit first, the order of the air */
static void put_byte(char *bits, uint8_t value)
{
	int i;

	for (i = 0; i < 8; i++)
		bits[i] = (char) ((value >> i) & 1);
}

static uint8_t get_byte(const char *bits)
{
	return air_to_host8(bits, 8);
}

/*
 * Put the CRC over the first info_bytes bytes of a payload into the two bytes
 * that follow them, which is where an eSCO payload carries it, so that the
 * payload's CRC agrees at a length of info_bytes + 2.
 */
static void put_crc(char *bits, int info_bytes)
{
	uint16_t crc = crcgen(bits, info_bytes * 8, TEST_UAP);

	put_byte(bits + info_bytes * 8, (uint8_t) (crc & 0xff));
	put_byte(bits + (info_bytes + 1) * 8, (uint8_t) (crc >> 8));
}

/*
 * A 30 byte payload whose CRC agrees at three lengths: 5, 20 and 30. Only 30
 * is the whole payload; the other two are the chance matches a 16-bit CRC
 * gives about one length in 65536, built here on purpose so that a decoder
 * which stops at the first one reports a payload 25 bytes short.
 */
#define COLLISION_BYTES 30
#define COLLISION_MATCH_SHORT 5
#define COLLISION_MATCH_MIDDLE 20

static int build_collision_payload(char *bits)
{
	int i;

	memset(bits, 0, COLLISION_BYTES * 8);

	put_byte(bits + 0 * 8, 0x11);
	put_byte(bits + 1 * 8, 0x22);
	put_byte(bits + 2 * 8, 0x33);
	put_crc(bits, 3);

	for (i = 5; i < 18; i++)
		put_byte(bits + i * 8, (uint8_t) (0x40 + i));
	put_crc(bits, 18);

	for (i = 20; i < 28; i++)
		put_byte(bits + i * 8, (uint8_t) (0x80 + i));
	put_crc(bits, 28);

	return COLLISION_BYTES * 8;
}

/* rate 2/3 FEC, the way an EV4 carries its payload: 15 symbols per 10 bits */
static int fec23_encode(const char *bits, int length, char *out)
{
	int block, i;

	for (block = 0; block * 10 < length; block++) {
		uint16_t codeword = fec23(air_to_host16(bits + block * 10,
							10));

		for (i = 0; i < 15; i++)
			out[block * 15 + i] = (char) ((codeword >> i) & 1);
	}

	return fec23_symbols(length);
}

/*
 * A packet holding nothing but the given payload symbols, as a decode that
 * had read the header would leave it: the type the header gave, and the
 * length of capture the receiver recorded. Whitening is off, so the payload
 * bytes are the bits given here.
 *
 * trailing is how many symbols the capture holds past the payload. A real
 * capture holds some: the receiver takes a fixed window, not a window that
 * ends where the packet does. quiet writes zeros there; otherwise it gets a
 * fixed stand-in for noise, which is what a real capture holds.
 */
static btbb_packet *packet_with_tail(uint8_t type, const char *payload,
				     int payload_symbols, int trailing,
				     int quiet)
{
	static char stream[MAX_SYMBOLS];
	btbb_packet *pkt = btbb_packet_new();
	uint32_t noise = 0x5eed1234;
	int i;

	if (pkt == NULL)
		return NULL;

	memset(stream, 0, sizeof(stream));
	memcpy(stream + PAYLOAD_OFFSET, payload, (size_t) payload_symbols);
	for (i = 0; !quiet && i < trailing; i++) {
		noise = noise * 1103515245u + 12345u;
		stream[PAYLOAD_OFFSET + payload_symbols + i] =
			(char) ((noise >> 20) & 1);
	}

	btbb_packet_set_data(pkt, stream,
			     PAYLOAD_OFFSET + payload_symbols + trailing,
			     0, 0);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 0);
	btbb_packet_set_uap(pkt, TEST_UAP);
	pkt->packet_type = type;

	return pkt;
}

static btbb_packet *packet_with_payload(uint8_t type, const char *payload,
					int payload_symbols, int trailing)
{
	return packet_with_tail(type, payload, payload_symbols, trailing, 0);
}

/* the payload the decoder reported, byte by byte, against what was sent */
static void check_payload_bytes(const btbb_packet *pkt, const char *sent,
				int bytes)
{
	int i;
	int wrong = 0;

	for (i = 0; i < bytes; i++)
		if (get_byte(btbb_get_payload(pkt) + i * 8) !=
		    get_byte(sent + i * 8))
			wrong++;

	CHECK(wrong == 0);
}

/*
 * The search for an eSCO length keeps going to the end rather than stopping
 * at the first length whose CRC agrees, which here is 5 bytes of a 30 byte
 * payload. Each of the lengths that agree here adds bytes of its own, so the
 * longest is the answer; the next test covers the lengths that add nothing
 * but zeros.
 */
static void test_esco_search_reports_the_longest_match(void)
{
	char payload[COLLISION_BYTES * 8];
	char encoded[COLLISION_BYTES * 8 * 2];
	int symbols = build_collision_payload(payload);
	int encoded_symbols = fec23_encode(payload, symbols, encoded);
	btbb_packet *pkt;

	/* EV5 and EV3 carry the payload as it is */
	pkt = packet_with_payload(PACKET_TYPE_EV5, payload, symbols, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 1);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED) == 1);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == 1);
	check_payload_bytes(pkt, payload, COLLISION_BYTES);
	btbb_packet_unref(pkt);

	/* the same, with the capture running on past the packet */
	pkt = packet_with_payload(PACKET_TYPE_EV5, payload, symbols, 200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);
	btbb_packet_unref(pkt);

	pkt = packet_with_payload(PACKET_TYPE_HV3, payload, symbols, 200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);
	check_payload_bytes(pkt, payload, COLLISION_BYTES);
	btbb_packet_unref(pkt);

	/* an EV4 carries it under rate 2/3 FEC, and searches a block at a
	 * time rather than a byte at a time */
	pkt = packet_with_payload(PACKET_TYPE_EV4, encoded, encoded_symbols,
				  200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == 1);
	check_payload_bytes(pkt, payload, COLLISION_BYTES);
	/* an intact payload, so the error correction put nothing right */
	CHECK(pkt->corrected_payload_bits == 0);
	btbb_packet_unref(pkt);
}

/*
 * A capture of zeros after the packet does not run the reported length up to
 * the longest the type allows. Every length that only adds zero bytes agrees
 * as well, because the CRC register comes out zero over a payload whose CRC
 * agrees and zero bytes leave it there. One of them is taken, since the
 * payload's own CRC could be the zero byte, and no more than one.
 */
static void test_zeros_after_the_payload_stop_at_one(void)
{
	char payload[COLLISION_BYTES * 8];
	int symbols = build_collision_payload(payload);
	btbb_packet *pkt;
	int tail;

	for (tail = 1; tail <= 4; tail++) {
		pkt = packet_with_tail(PACKET_TYPE_EV5, payload, symbols,
				       tail * 8, 1);
		CHECK(pkt != NULL);
		if (pkt == NULL)
			return;
		CHECK(btbb_decode_payload(pkt) == 10);
		CHECK(btbb_packet_get_payload_length(pkt) ==
		      COLLISION_BYTES + 1);
		/* and the payload itself is reported as it was sent */
		check_payload_bytes(pkt, payload, COLLISION_BYTES);
		btbb_packet_unref(pkt);
	}

	/* a capture of nothing but zeros past the packet, where "the longest
	 * length that agrees" would have reported all 182 bytes an EV5 can
	 * carry */
	pkt = packet_with_tail(PACKET_TYPE_EV5, payload, symbols, 1000, 1);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES + 1);
	btbb_packet_unref(pkt);

	/* the caller's own length is not guessed at at all */
	pkt = packet_with_tail(PACKET_TYPE_EV5, payload, symbols, 1000, 1);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_esco_length(pkt, COLLISION_BYTES - 2);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);
	btbb_packet_unref(pkt);
}

/*
 * A payload whose own CRC ends in a zero byte. The CRC register comes out
 * zero a byte early, so the length one short of the payload agrees as well,
 * and a search that stops at the first length to agree reports the payload
 * one byte short and reads its last information byte as part of the CRC. One
 * payload in 256 is like this, which is what #40 reported.
 */
#define HIGH_ZERO_BYTES 20

static int build_high_zero_payload(char *bits)
{
	int filler, i;

	/* the filler byte that gives a CRC ending in a zero byte is searched
	 * for rather than written down, so that the one thing this payload
	 * has to have is in plain sight */
	for (filler = 0; filler < 256; filler++) {
		memset(bits, 0, HIGH_ZERO_BYTES * 8);
		for (i = 0; i < HIGH_ZERO_BYTES - 3; i++)
			put_byte(bits + i * 8, (uint8_t) (0x10 + i));
		put_byte(bits + (HIGH_ZERO_BYTES - 3) * 8, (uint8_t) filler);

		if ((crcgen(bits, (HIGH_ZERO_BYTES - 2) * 8, TEST_UAP) >> 8)
		    == 0) {
			put_crc(bits, HIGH_ZERO_BYTES - 2);
			return HIGH_ZERO_BYTES * 8;
		}
	}

	return 0;
}

static void test_payload_whose_crc_ends_in_zero(void)
{
	char payload[HIGH_ZERO_BYTES * 8];
	char encoded[HIGH_ZERO_BYTES * 8 * 2];
	int symbols = build_high_zero_payload(payload);
	int encoded_symbols;
	btbb_packet *pkt;

	CHECK(symbols == HIGH_ZERO_BYTES * 8);
	if (symbols != HIGH_ZERO_BYTES * 8)
		return;
	encoded_symbols = fec23_encode(payload, symbols, encoded);

	/* the length one byte short agrees, which is the answer to beat */
	CHECK(crcgen(payload, (HIGH_ZERO_BYTES - 3) * 8, TEST_UAP) ==
	      air_to_host16(&payload[(HIGH_ZERO_BYTES - 3) * 8], 16));

	pkt = packet_with_payload(PACKET_TYPE_EV5, payload, symbols, 200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == HIGH_ZERO_BYTES);
	check_payload_bytes(pkt, payload, HIGH_ZERO_BYTES);
	btbb_packet_unref(pkt);

	pkt = packet_with_payload(PACKET_TYPE_HV3, payload, symbols, 200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == HIGH_ZERO_BYTES);
	btbb_packet_unref(pkt);

	pkt = packet_with_payload(PACKET_TYPE_EV4, encoded, encoded_symbols,
				  200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == HIGH_ZERO_BYTES);
	check_payload_bytes(pkt, payload, HIGH_ZERO_BYTES);
	btbb_packet_unref(pkt);
}

/*
 * A caller that knows the length the link was set up with says so, and that
 * length is decoded and nothing else: not the longest that agrees, and not
 * the first.
 */
static void test_known_esco_length_is_decoded(void)
{
	char payload[COLLISION_BYTES * 8];
	char encoded[COLLISION_BYTES * 8 * 2];
	int symbols = build_collision_payload(payload);
	int encoded_symbols = fec23_encode(payload, symbols, encoded);
	btbb_packet *pkt;

	pkt = packet_with_payload(PACKET_TYPE_EV5, payload, symbols, 200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	/* the middle length that agrees, which neither end of the search
	 * would have picked */
	btbb_packet_set_esco_length(pkt, COLLISION_MATCH_MIDDLE - 2);
	CHECK(btbb_packet_get_esco_length(pkt) == COLLISION_MATCH_MIDDLE - 2);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_MATCH_MIDDLE);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == 1);
	check_payload_bytes(pkt, payload, COLLISION_MATCH_MIDDLE);

	/* and the short one */
	btbb_packet_set_esco_length(pkt, COLLISION_MATCH_SHORT - 2);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_MATCH_SHORT);

	/*
	 * A length whose CRC does not agree is still the length the caller
	 * said: an encrypted payload's CRC never agrees, and the length is
	 * what makes the payload reportable at all. 10 is a CRC that did not
	 * come out.
	 */
	btbb_packet_set_esco_length(pkt, 10);
	CHECK(btbb_decode_payload(pkt) == 2);
	CHECK(btbb_packet_get_payload_length(pkt) == 12);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 1);
	/* and the library claims a CRC only where one came out */
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED) == 0);
	check_payload_bytes(pkt, payload, 12);

	/* a length this type cannot carry is no use, so the search runs */
	btbb_packet_set_esco_length(pkt, EV5_MAX_PAYLOAD - 1);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);

	btbb_packet_set_esco_length(pkt, 0);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);

	btbb_packet_unref(pkt);

	/* an EV4's search is the same decision a block at a time */
	pkt = packet_with_payload(PACKET_TYPE_EV4, encoded, encoded_symbols,
				  200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_esco_length(pkt, COLLISION_MATCH_MIDDLE - 2);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_MATCH_MIDDLE);
	check_payload_bytes(pkt, payload, COLLISION_MATCH_MIDDLE);
	CHECK(pkt->corrected_payload_bits == 0);
	btbb_packet_unref(pkt);

	/* an EV4 length the type cannot carry is ignored the same way */
	pkt = packet_with_payload(PACKET_TYPE_EV4, encoded, encoded_symbols,
				  200);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	btbb_packet_set_esco_length(pkt, EV4_MAX_PAYLOAD - 1);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_BYTES);
	btbb_packet_unref(pkt);
}

/* a length that agreed is the payload, even where the capture ends there */
static void test_truncated_capture_keeps_a_match(void)
{
	char payload[COLLISION_BYTES * 8];
	int symbols = build_collision_payload(payload);
	btbb_packet *pkt;

	CHECK(symbols == COLLISION_BYTES * 8);

	/* the capture holds the first 20 bytes and three bytes more */
	pkt = packet_with_payload(PACKET_TYPE_EV5, payload,
				  COLLISION_MATCH_MIDDLE * 8 + 24, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_MATCH_MIDDLE);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == 1);
	btbb_packet_unref(pkt);

	/* with nothing that agreed, the bytes that were captured are all
	 * there is to report */
	pkt = packet_with_payload(PACKET_TYPE_EV5, payload, 8 * 4, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 1);
	CHECK(btbb_packet_get_payload_length(pkt) == 4);
	btbb_packet_unref(pkt);
}

/*
 * An HV3 carries 30 bytes of voice and no CRC at all, so no length agrees
 * with an EV3's search. The two share a type code, and the one that has a
 * CRC to show for itself is the one to report: without it the packet is the
 * HV3 it is the right size to be, not 32 bytes of EV3.
 */
static void test_hv3_is_not_reported_as_ev3(void)
{
	char voice[30 * 8];
	btbb_packet *pkt;
	int i;

	for (i = 0; i < 30; i++)
		put_byte(voice + i * 8, (uint8_t) (0x10 + i));

	/*
	 * A capture that runs 16 symbols or more past the packet, which a
	 * real one does: the EV3 search then reaches the end of every length
	 * it could try, agrees with none, and has nothing to show.
	 */
	pkt = packet_with_payload(PACKET_TYPE_HV3, voice, 30 * 8, 64);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 2);
	CHECK(btbb_packet_get_payload_length(pkt) == 30);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 1);
	/* a voice payload has no CRC, so none was checked */
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) == 0);
	check_payload_bytes(pkt, voice, 30);
	btbb_packet_unref(pkt);

	/* a capture that ends with the packet reports the same 30 bytes */
	pkt = packet_with_payload(PACKET_TYPE_HV3, voice, 30 * 8, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 2);
	CHECK(btbb_packet_get_payload_length(pkt) == 30);
	check_payload_bytes(pkt, voice, 30);
	btbb_packet_unref(pkt);

	/* one shorter than an HV3's 240 symbols is neither packet */
	pkt = packet_with_payload(PACKET_TYPE_HV3, voice, 30 * 8 - 8, 0);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;
	CHECK(btbb_decode_payload(pkt) == 1);
	CHECK(btbb_packet_get_payload_length(pkt) == 0);
	CHECK(btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) == 0);
	btbb_packet_unref(pkt);
}

/* an EV3 on a link the caller knows about is an EV3, not an HV3 */
static void test_known_esco_length_rules_out_hv3(void)
{
	char payload[COLLISION_BYTES * 8];
	int symbols = build_collision_payload(payload);
	btbb_packet *pkt;

	pkt = packet_with_payload(PACKET_TYPE_HV3, payload, symbols, 64);
	CHECK(pkt != NULL);
	if (pkt == NULL)
		return;

	btbb_packet_set_esco_length(pkt, COLLISION_MATCH_MIDDLE - 2);
	CHECK(btbb_decode_payload(pkt) == 10);
	CHECK(btbb_packet_get_payload_length(pkt) == COLLISION_MATCH_MIDDLE);

	/* a length whose CRC does not agree is the caller's answer too: an
	 * HV3's 30 voice bytes are not what this link carries */
	btbb_packet_set_esco_length(pkt, 10);
	CHECK(btbb_decode_payload(pkt) == 2);
	CHECK(btbb_packet_get_payload_length(pkt) == 12);

	btbb_packet_unref(pkt);
}

/*
 * How often the reported length is wrong over many payloads, which is what
 * issue #40 is about: each of the cases above stands for about one payload in
 * 256, so only a sweep shows the rate.
 *
 * The payloads are built the way the air carries them - whitened, rate 2/3
 * FEC encoded for an EV4, at a clock that changes from one to the next, with
 * a tail of noise after the packet - and from a fixed seed, so the counts
 * below are exact rather than statistical. Raising SWEEP_TRIALS shows the
 * rate more finely, at the cost of run time.
 */
#define SWEEP_TRIALS 2000

static uint32_t sweep_seed = 0xabcdef;

static int sweep_bit(void)
{
	sweep_seed = sweep_seed * 1103515245u + 12345u;
	return (int) ((sweep_seed >> 20) & 1);
}

static uint8_t sweep_byte(void)
{
	sweep_seed = sweep_seed * 1103515245u + 12345u;
	return (uint8_t) (sweep_seed >> 16);
}

/* the length the decoder reports for one payload of info information bytes */
static int sweep_once(uint8_t type, int info)
{
	static char stream[MAX_SYMBOLS];
	static char payload[(180 + 2) * 8];
	static char whitened[(180 + 2) * 8];
	char encoded[((180 + 2) * 8 * 3) / 2 + 15];
	btbb_packet *pkt = btbb_packet_new();
	int bytes = info + 2;
	int i, symbols, length;
	int clock;
	uint16_t crc;

	if (pkt == NULL)
		return -1;

	for (i = 0; i < info; i++)
		put_byte(payload + i * 8, sweep_byte());
	crc = crcgen(payload, info * 8, TEST_UAP);
	put_byte(payload + info * 8, (uint8_t) (crc & 0xff));
	put_byte(payload + (info + 1) * 8, (uint8_t) (crc >> 8));

	clock = (int) (sweep_byte() & 0x3f);

	/* whitening is its own inverse, so the decoder's own unwhiten() turns
	 * the payload into the bits the air carries */
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);
	unwhiten(payload, whitened, clock, bytes * 8,
		 HEADER_WHITENING_BITS, pkt);

	memset(stream, 0, sizeof(stream));
	if (type == PACKET_TYPE_EV4) {
		symbols = fec23_encode(whitened, bytes * 8, encoded);
		memcpy(stream + PAYLOAD_OFFSET, encoded, (size_t) symbols);
	} else {
		symbols = bytes * 8;
		memcpy(stream + PAYLOAD_OFFSET, whitened, (size_t) symbols);
	}
	for (i = 0; i < 320; i++)
		stream[PAYLOAD_OFFSET + symbols + i] = (char) sweep_bit();

	btbb_packet_set_data(pkt, stream, PAYLOAD_OFFSET + symbols + 320,
			     0, (uint32_t) clock << 1);
	btbb_packet_set_flag(pkt, BTBB_WHITENED, 1);
	btbb_packet_set_uap(pkt, TEST_UAP);
	pkt->packet_type = type;

	(void) btbb_decode_payload(pkt);
	length = btbb_packet_get_payload_length(pkt);
	btbb_packet_unref(pkt);

	return length;
}

static void sweep(uint8_t type, int info, int *shorter, int *longer)
{
	int trial;

	/* from the same seed each time, so that one sweep's counts do not
	 * depend on which others ran before it */
	sweep_seed = 0xabcdef;
	*shorter = 0;
	*longer = 0;
	for (trial = 0; trial < SWEEP_TRIALS; trial++) {
		int length = sweep_once(type, info);

		if (length < info + 2)
			++*shorter;
		else if (length > info + 2)
			++*longer;
	}
}

static void test_length_sweep(void)
{
	int shorter, longer;

	/*
	 * At the longest length a type carries, which is what a link normally
	 * sets up, a payload comes out at its own length: no longer length
	 * exists to agree by chance, and the length one short of it no longer
	 * wins. Before this fix these three reported 8, 11 and 11 wrong
	 * lengths in these 2,000.
	 */
	sweep(PACKET_TYPE_HV3, 30, &shorter, &longer);     /* EV3 */
	CHECK(shorter == 0);
	CHECK(longer == 0);

	sweep(PACKET_TYPE_EV4, 120, &shorter, &longer);
	CHECK(shorter == 0);
	CHECK(longer == 0);

	/*
	 * A payload whose CRC is 0x0000 leaves the register at zero two bytes
	 * early, so three lengths agree and the rule above takes the middle
	 * one: one byte short. None of these 2,000 is like that, since it is
	 * one payload in 65536; a longer sweep does reach one.
	 */
	sweep(PACKET_TYPE_EV5, 180, &shorter, &longer);
	CHECK(shorter == 0);
	CHECK(longer == 0);

	/*
	 * Short of a type's longest length the guess does not go away: a zero
	 * byte of noise right after the payload agrees as well, and so does a
	 * longer length by chance. It changes direction, from one byte short
	 * to one byte long, which keeps every information byte. A caller that
	 * knows the link's length has no guess made for it at all.
	 */
	sweep(PACKET_TYPE_EV5, 20, &shorter, &longer);
	CHECK(shorter == 0);
	CHECK(longer == 6);
}

int main(void)
{
	test_esco_search_reports_the_longest_match();
	test_zeros_after_the_payload_stop_at_one();
	test_payload_whose_crc_ends_in_zero();
	test_known_esco_length_is_decoded();
	test_truncated_capture_keeps_a_match();
	test_hv3_is_not_reported_as_ev3();
	test_known_esco_length_rules_out_hv3();
	test_length_sweep();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
