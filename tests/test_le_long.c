/*
 * Full-length LE packets: decoding a data PDU's length from all eight bits of
 * the field, and writing the whole packet to PCAP and PCAPNG.
 *
 * The decoder used to read only five bits of the length and keep only 64
 * bytes, so a Bluetooth 4.2 Data Length Extension payload came out short. The
 * writers recorded the wrong lengths and could copy more than the record
 * held. Both halves are checked here by reading the files back.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btbb.h"
#include "bluetooth_le_packet.h"
#include "pcap-common.h"

/* The last payload byte of the longest packet, so a writer that records the
 * right length with the wrong bytes is still caught. */
#define TAIL_BYTE 0xa5

/* A data-channel packet whose header declares `payload_len` bytes, with the
 * last payload byte marked. */
static size_t make_frame(uint8_t *frame, unsigned payload_len)
{
	memset(frame, 0, MAX_LE_SYMBOLS);
	/* The access address only has to be readable here: what makes the
	 * decoder treat this as a data PDU, and so read the length as a full
	 * octet, is the 2404 MHz the callers below pass, which is data channel
	 * index 0. */
	frame[0] = 0xd6;
	frame[1] = 0xbe;
	frame[2] = 0x89;
	frame[3] = 0x8e;
	frame[4] = 0x01;
	frame[5] = (uint8_t) payload_len;
	if (payload_len)
		frame[6 + payload_len - 1] = TAIL_BYTE;
	return 4 + 2 + payload_len + 3;
}

static int check_data_length(unsigned payload_len)
{
	uint8_t frame[MAX_LE_SYMBOLS];
	lell_packet *pkt = NULL;
	size_t frame_len = make_frame(frame, payload_len);

	lell_allocate_and_decode_n(frame, frame_len, 2404, 0, &pkt);
	if (!pkt || pkt->length != (int) payload_len) {
		fprintf(stderr, "FAIL: declared %u decoded as %d\n",
		        payload_len, pkt ? pkt->length : -1);
		if (pkt) lell_packet_unref(pkt);
		return 1;
	}
	if (payload_len && pkt->symbols[6 + payload_len - 1] != TAIL_BYTE) {
		fprintf(stderr, "FAIL: payload byte %u was truncated\n",
		        payload_len - 1);
		lell_packet_unref(pkt);
		return 1;
	}
	lell_packet_unref(pkt);
	return 0;
}

/* A packet shorter than the buffer it is handed must not take its length from
 * bytes that were never captured. */
static int check_short_capture_is_not_overread(void)
{
	uint8_t frame[MAX_LE_SYMBOLS];
	lell_packet *pkt = NULL;

	/* A header claiming 251 bytes in a capture that holds 50. */
	make_frame(frame, 251);
	lell_allocate_and_decode_n(frame, 50, 2404, 0, &pkt);
	if (!pkt || pkt->length != 50 - 9) {
		fprintf(stderr, "FAIL: a 50-byte capture decoded as %d payload bytes,"
		        " expected %d\n", pkt ? pkt->length : -1, 50 - 9);
		if (pkt) lell_packet_unref(pkt);
		return 1;
	}
	lell_packet_unref(pkt);
	return 0;
}

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long end;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0) {
		fclose(f);
		return NULL;
	}
	buf = (uint8_t *) malloc((size_t) end);
	rewind(f);
	if (!buf || fread(buf, 1, (size_t) end, f) != (size_t) end) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*size = (size_t) end;
	return buf;
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* The one record a PCAP file holds: both its lengths and its last byte of
 * payload. */
static int check_pcap_record(const char *path, uint32_t expected_len)
{
	const size_t file_header = 24;   /* magic, versions, zone, sigfigs, snaplen, dlt */
	const size_t record_header = 16; /* seconds, microseconds, captured, original */
	const size_t pseudo = sizeof(pcap_bluetooth_le_ll_header);
	size_t size = 0;
	uint8_t *buf = read_file(path, &size);
	uint32_t captured, original;
	int bad = 0;

	if (!buf || size < file_header + record_header) {
		fprintf(stderr, "FAIL: %s is too short to hold a record\n", path);
		free(buf);
		return 1;
	}
	captured = le32(buf + file_header + 8);
	original = le32(buf + file_header + 12);
	if (captured != pseudo + expected_len || original != pseudo + expected_len) {
		fprintf(stderr, "FAIL: %s records captured %u original %u,"
		        " expected %zu for both\n", path, captured, original,
		        pseudo + expected_len);
		bad = 1;
	}
	if (size < file_header + record_header + captured) {
		fprintf(stderr, "FAIL: %s is shorter than the length it records\n", path);
		bad = 1;
	} else {
		const uint8_t *packet = buf + file_header + record_header + pseudo;
		if (packet[6 + 251 - 1] != TAIL_BYTE) {
			fprintf(stderr, "FAIL: %s lost the last payload byte\n", path);
			bad = 1;
		}
	}
	free(buf);
	return bad;
}

/* The same for the one enhanced packet block a PCAPNG file holds. */
static int check_pcapng_record(const char *path, uint32_t expected_len)
{
	const size_t pseudo = sizeof(pcap_bluetooth_le_ll_header);
	size_t size = 0;
	uint8_t *buf = read_file(path, &size);
	size_t at;
	int found = 0;
	int bad = 0;

	if (!buf) {
		fprintf(stderr, "FAIL: %s could not be read\n", path);
		return 1;
	}
	/* Walk the blocks: type, then total length, which includes both. */
	for (at = 0; at + 8 <= size; ) {
		uint32_t type = le32(buf + at);
		uint32_t total = le32(buf + at + 4);

		if (total < 12 || at + total > size)
			break;
		if (type == 0x00000006) { /* enhanced packet block */
			uint32_t captured = le32(buf + at + 20);
			uint32_t packet_len = le32(buf + at + 24);
			const uint8_t *packet = buf + at + 28 + pseudo;

			found = 1;
			if (captured != pseudo + expected_len ||
			    packet_len != pseudo + expected_len) {
				fprintf(stderr, "FAIL: %s records captured %u packet %u,"
				        " expected %zu for both\n", path, captured,
				        packet_len, pseudo + expected_len);
				bad = 1;
			}
			if (packet + 6 + 251 - 1 >= buf + size ||
			    packet[6 + 251 - 1] != TAIL_BYTE) {
				fprintf(stderr, "FAIL: %s lost the last payload byte\n", path);
				bad = 1;
			}
			break;
		}
		at += total;
	}
	if (!found) {
		fprintf(stderr, "FAIL: %s holds no packet block\n", path);
		bad = 1;
	}
	free(buf);
	return bad;
}

/* The longest valid packet written to both formats, read back. */
static int check_capture_writers(void)
{
	uint8_t frame[MAX_LE_SYMBOLS];
	lell_packet *pkt = NULL;
	lell_pcap_handle *pcap = NULL;
	lell_pcapng_handle *pcapng = NULL;
	char pcap_path[] = "/tmp/libbtbb-le-pcap-XXXXXX";
	char pcapng_path[] = "/tmp/libbtbb-le-pcapng-XXXXXX";
	/* The whole packet: access address, header, payload and CRC. */
	const uint32_t expected = 4 + 2 + 251 + 3;
	int bad = 0;
	int fd;

	make_frame(frame, 251);
	lell_allocate_and_decode_n(frame, sizeof(frame), 2404, 0, &pkt);
	if (!pkt || pkt->length != 251) {
		fprintf(stderr, "FAIL: the longest packet did not decode\n");
		if (pkt) lell_packet_unref(pkt);
		return 1;
	}

	fd = mkstemp(pcap_path);
	close(fd);
	unlink(pcap_path);
	if (lell_pcap_create_file(pcap_path, &pcap) ||
	    lell_pcap_append_packet(pcap, 0, -40, -90, 0, pkt) ||
	    lell_pcap_close(pcap)) {
		fprintf(stderr, "FAIL: the PCAP write failed\n");
		bad = 1;
	} else {
		bad |= check_pcap_record(pcap_path, expected);
	}
	unlink(pcap_path);

	fd = mkstemp(pcapng_path);
	close(fd);
	unlink(pcapng_path);
	/* A write failure is negative. A positive answer from the append is
	   LELL_PCAPNG_CONNECT_INFO_NOT_RECORDED, which says the packet was
	   written and only a recorded option was not, so it is not a failure
	   of this write. */
	if (lell_pcapng_create_file(pcapng_path, "test", &pcapng) ||
	    lell_pcapng_append_packet(pcapng, 0, -40, -90, 0, pkt) < 0) {
		fprintf(stderr, "FAIL: the PCAPNG write failed\n");
		bad = 1;
	} else {
		/* This one reports PCAPNG_INVALID_HANDLE even when it succeeded. */
		(void) lell_pcapng_close(pcapng);
		bad |= check_pcapng_record(pcapng_path, expected);
	}
	unlink(pcapng_path);

	lell_packet_unref(pkt);
	return bad;
}

int main(void)
{
	static const unsigned lengths[] = {0, 1, 27, 31, 32, 55, 56, 100, 251};
	unsigned i;

	for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
		if (check_data_length(lengths[i]))
			return 1;
	if (check_short_capture_is_not_overread())
		return 1;
	if (check_capture_writers())
		return 1;

	puts("PASS: LE lengths 0..251 preserved; the longest packet is recorded"
	     " whole in PCAP and PCAPNG");
	return 0;
}
