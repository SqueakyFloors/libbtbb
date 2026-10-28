/* -*- c -*- */
/*
 * Copyright 2007 - 2013 Dominic Spill, Michael Ossmann, Will Code
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
#ifndef INCLUDED_BTBB_H
#define INCLUDED_BTBB_H

#include <stddef.h>
#include <stdint.h>

#define BTBB_WHITENED    0
#define BTBB_NAP_VALID   1
#define BTBB_UAP_VALID   2
#define BTBB_LAP_VALID   3
#define BTBB_CLK6_VALID  4
#define BTBB_CLK27_VALID 5
#define BTBB_CRC_CORRECT 6
#define BTBB_HAS_PAYLOAD 7
#define BTBB_IS_EDR      8

#define BTBB_HOP_REVERSAL_INIT 9
#define BTBB_GOT_FIRST_PACKET  10
#define BTBB_IS_AFH            11
#define BTBB_LOOKS_LIKE_AFH    12
#define BTBB_IS_ALIASED        13
#define BTBB_FOLLOWING         14

/* Whether the header's HEC and the payload's CRC were checked, and what the
   HEC check said. The CRC's answer is BTBB_CRC_CORRECT above. A capture file
   records all four, so a reader can tell a verified payload from a guess. */
#define BTBB_HEC_CHECKED 15
#define BTBB_HEC_VALID   16
#define BTBB_CRC_CHECKED 17

/* Payload modulation */
#define BTBB_MOD_GFSK              0x00
#define BTBB_MOD_PI_OVER_2_DQPSK   0x01
#define BTBB_MOD_8DPSK             0x02

/* Transport types */
#define BTBB_TRANSPORT_ANY     0x00
#define BTBB_TRANSPORT_SCO     0x01
#define BTBB_TRANSPORT_ESCO    0x02
#define BTBB_TRANSPORT_ACL     0x03
#define BTBB_TRANSPORT_CSB     0x04

#ifdef __cplusplus
extern "C"
{
#endif

/* BT BR/EDR support */

typedef struct btbb_packet btbb_packet;

/* Initialize the library. Compute the syndrome. Return 0 on success,
 * negative on error.
 *
 * The library limits max_ac_errors to 5. Using a larger value will
 * take up a lot of memory (several GB), without decoding many useful
 * packets. Even a limit of 5 results in a syndrome table of several
 * hundred MB and lots of noise. For embedded targets, a value of 2 is
 * reasonable. */
int btbb_init(int max_ac_errors);

const char* btbb_get_release(void);
const char* btbb_get_version(void);

btbb_packet *btbb_packet_new(void);
void btbb_packet_ref(btbb_packet *pkt);
void btbb_packet_unref(btbb_packet *pkt);

/* Search for a packet with specified LAP (or LAP_ANY). Limit to
 * 'max_ac_errors' bit errors.
 *
 * Every one of the first 'search_length' symbols of 'stream' is tried
 * as the start of the 64 symbol sync word, so the search reads
 * search_length + 63 symbols, and none at all for a search_length of
 * zero. Decoding a packet it finds needs the rest of the packet, which
 * the caller passes separately to btbb_packet_set_data().
 *
 * The _n form is the one to call: it takes the number of symbols
 * 'stream' holds and searches only the offsets that hold a whole sync
 * word, which is stream_len - 63 of them, so it reads no further than
 * the caller's symbols however large a search_length it is given. The
 * form without a length cannot check that, and reads whatever
 * search_length asks for.
 *
 * Returns offset into 'stream' at which packet was found. If no
 * packet was found, returns a negative number. On a match a packet is
 * allocated at *pkt if *pkt is NULL, and filled in with the LAP and
 * the number of access code bit errors, which the caller reads back
 * with btbb_packet_get_lap() and btbb_packet_get_ac_errors(). */
int btbb_find_ac(char *stream,
	       int search_length,
	       uint32_t lap,
	       int max_ac_errors,
	       btbb_packet **pkt);
int btbb_find_ac_n(char *stream,
	       size_t stream_len,
	       int search_length,
	       uint32_t lap,
	       int max_ac_errors,
	       btbb_packet **pkt);
#define LAP_ANY 0xffffffffUL
#define UAP_ANY 0xff

void btbb_packet_set_flag(btbb_packet *pkt, int flag, int val);
int btbb_packet_get_flag(const btbb_packet *pkt, int flag);

uint32_t btbb_packet_get_lap(const btbb_packet *pkt);
void btbb_packet_set_uap(btbb_packet *pkt, uint8_t uap);
uint8_t btbb_packet_get_uap(const btbb_packet *pkt);
uint16_t btbb_packet_get_nap(const btbb_packet *pkt);

void btbb_packet_set_modulation(btbb_packet *pkt, uint8_t modulation);
void btbb_packet_set_transport(btbb_packet *pkt, uint8_t transport);
uint8_t btbb_packet_get_modulation(const btbb_packet *pkt);
uint8_t btbb_packet_get_transport(const btbb_packet *pkt);

uint8_t btbb_packet_get_channel(const btbb_packet *pkt);
uint8_t btbb_packet_get_ac_errors(const btbb_packet *pkt);
/* The packet's clock as CLK1-27, the 625 us slot count: the 312.5 us value
   given to btbb_packet_set_data() below, shifted down by one. */
uint32_t btbb_packet_get_clkn(const btbb_packet *pkt);
uint32_t btbb_packet_get_header_packed(const btbb_packet* pkt);

/* Copy the symbols of a packet in. This is the length every decode
 * works from, so a packet holding fewer symbols than a field needs is
 * decoded as far as the symbols go and no further. A length below zero
 * is taken as none, and one above the longest packet, 3125 symbols, as
 * that. */
void btbb_packet_set_data(btbb_packet *pkt,
			  char *syms,      // Symbol data
			  int length,      // Number of symbols
			  uint8_t channel, // Bluetooth channel 0-79
			  uint32_t clkn);  // 312.5us clock (CLK27-0)

/* Get a pointer to packet symbols. */
const char *btbb_get_symbols(const btbb_packet* pkt);

int btbb_packet_get_payload_length(const btbb_packet* pkt);

/* Get a pointer to payload. */
const char *btbb_get_payload(const btbb_packet* pkt);

/* Pack the payload in to bytes. 'dst' must hold
 * btbb_packet_get_payload_length(pkt) bytes, which is never more than
 * 343, the longest payload any packet type has. Returns the number of
 * bytes written. */
int btbb_get_payload_packed(const btbb_packet* pkt, char *dst);

/* Tell the decoder the payload length an eSCO link carries, in information
 * bytes, or 0 if it is not known. The reported payload length is this plus
 * the two CRC bytes.
 *
 * An eSCO payload carries no length of its own: the length is agreed when
 * the link is set up (Vol 2, Part B, Section 6.5.3), so without it the
 * decoder has to try every length the packet type allows and pick between
 * the ones whose CRC agrees. More than one does: a 16-bit CRC agrees with
 * about one length in 65536 by chance, and a zero byte right after the
 * payload - a byte in 256 of the noise a capture holds past the packet -
 * makes the length one byte longer agree as well. Where the payload is at
 * the longest length its type carries the decoder settles that, since there
 * is no longer length to pick; short of it, a payload can be reported one
 * byte long, with its CRC's low byte as the last information byte.
 *
 * So a caller that knows the link's length should say so: the decoder then
 * decodes that length, or as much of it as the capture holds, and tries no
 * other. It is also the only way to get the length of an encrypted payload,
 * whose CRC never agrees.
 *
 * This applies to the EV3, EV4 and EV5 types only. A length the type cannot
 * carry (below 1, or above 30 for an EV3, 120 for an EV4 or 180 for an EV5)
 * is ignored, and the decoder searches as if the length were unknown. An EV3
 * shares its type code with an HV3 voice packet, so a length set here also
 * says that packets of that type code on this link are EV3s.
 *
 * The value stays on the packet object until it is set again, including
 * across the frames btbb_find_ac() hands back in the same object, so set it
 * to 0 when the packets no longer come from that link. */
void btbb_packet_set_esco_length(btbb_packet *pkt, int length);
int btbb_packet_get_esco_length(const btbb_packet *pkt);

uint8_t btbb_packet_get_type(const btbb_packet* pkt);
uint8_t btbb_packet_get_lt_addr(const btbb_packet* pkt);
uint8_t btbb_packet_get_header_flags(const btbb_packet* pkt);
uint8_t btbb_packet_get_hec(const btbb_packet *pkt);

/* Generate Sync Word from an LAP */
uint64_t btbb_gen_syncword(const int LAP);

/* Decode the packet header. The access code and the header take the
 * first 122 symbols of a packet, so this returns 0 for a packet given
 * fewer than that. */
int btbb_decode_header(btbb_packet* pkt);

/* Decode the packet payload. Decodes only what the symbols the packet
 * was given hold: a payload the capture ended partway through is
 * reported as the bytes that were decoded, or as none at all. */
int btbb_decode_payload(btbb_packet* pkt);

/* print packet information */
void btbb_print_packet(const btbb_packet* pkt);

/* check to see if the packet has a header */
int btbb_header_present(const btbb_packet* pkt);

/* Packet queue (linked list) */
typedef struct pkt_queue {
	btbb_packet *pkt;

	struct pkt_queue *next;

} pkt_queue;

typedef struct btbb_piconet btbb_piconet;

btbb_piconet *btbb_piconet_new(void);
void btbb_piconet_ref(btbb_piconet *pn);
void btbb_piconet_unref(btbb_piconet *pn);

/* initialize the piconet struct */
void btbb_init_piconet(btbb_piconet *pn, uint32_t lap);

void btbb_piconet_set_uap(btbb_piconet *pn, uint8_t uap);
uint8_t btbb_piconet_get_uap(const btbb_piconet *pn);
uint32_t btbb_piconet_get_lap(const btbb_piconet *pn);
uint16_t btbb_piconet_get_nap(const btbb_piconet *pn);
uint64_t btbb_piconet_get_bdaddr(const btbb_piconet *pn);

/* The offset between the local clock and the piconet's, in 312.5 us CLKN
   ticks, which is what a receiver is told to hop on. It means something only
   while BTBB_CLK27_VALID is set: before a clock is recovered, and again after
   one is given up, it reads 0. */
int btbb_piconet_get_clk_offset(const btbb_piconet *pn);
void btbb_piconet_set_clk_offset(btbb_piconet *pn, int clk_offset);

void btbb_piconet_set_flag(btbb_piconet *pn, int flag, int val);
int btbb_piconet_get_flag(const btbb_piconet *pn, int flag);

uint8_t btbb_piconet_set_channel_seen(btbb_piconet *pn, uint8_t channel);
uint8_t btbb_piconet_clear_channel_seen(btbb_piconet *pn, uint8_t channel);
uint8_t btbb_piconet_get_channel_seen(btbb_piconet *pn, uint8_t channel);
void btbb_piconet_set_afh_map(btbb_piconet *pn, uint8_t *afh_map);
uint8_t *btbb_piconet_get_afh_map(btbb_piconet *pn);

/* btbb_process_packet() returns this when the clock and UAP it recovered turn
   out not to be the piconet's: header after header of this piconet arrived
   while following it and none of them passed its HEC. The piconet has started
   discovery again, so BTBB_FOLLOWING, the clock and the UAP are gone and new
   ones will be reported in time. A receiver put on the old offset has to be
   taken off it: a capture tool is expected to warn once and follow the
   piconet again when the library has a clock for it.

   It is a return value and not one of the BTBB_* flags above, so it is no use
   to btbb_piconet_get_flag(). It is positive, where the one other thing this
   function reports is negative, so a caller that only looks for that is
   unaffected. */
#define BTBB_PROCESS_LOCK_LOST 1

/* Extract as much information (LAP/UAP/CLK) as possible from received packet.
   Returns BTBB_PROCESS_LOCK_LOST (above), -1 when the piconet's clock has
   just been recovered and the caller should follow it on
   btbb_piconet_get_clk_offset(), and 0 otherwise. */
int btbb_process_packet(btbb_packet *pkt, btbb_piconet *pn);

/* use packet headers to determine UAP */
int btbb_uap_from_header(btbb_packet *pkt, btbb_piconet *pn);

/* Print hexadecimal representation of the derived AFH map */
void btbb_print_afh_map(btbb_piconet *pn);

/* decode a whole packet from the given piconet */
int btbb_decode(btbb_packet* pkt);


/* Start recovering CLK1-27 from the hops observed so far. Returns the
 * number of candidate clock values left, which is zero where there is
 * nothing to work with.
 *
 * Hops as an aliased receiver observes them, folded from 79 channels
 * into 25, are not reversed: a nonzero 'aliased' is refused, with a
 * note on stderr and no candidates. It used to be accepted and then
 * reversed as if unaliased, against channels such a receiver never
 * reports. */
int btbb_init_hop_reversal(int aliased, btbb_piconet *pn);

/* narrow a list of candidate clock values based on all observed hops */
int btbb_winnow(btbb_piconet *pn);

int btbb_init_survey(void);
/* Destructively iterate over survey results - optionally remove elements */
btbb_piconet *btbb_next_survey_result(void);

typedef struct btbb_pcapng_handle btbb_pcapng_handle;
/* create a PCAPNG file for BREDR captures */
int btbb_pcapng_create_file(const char *filename, const char *interface_desc, btbb_pcapng_handle ** ph);
/* save a BREDR packet to PCAPNG capture file */
int btbb_pcapng_append_packet(btbb_pcapng_handle * h, const uint64_t ns,
                              const int8_t sigdbm, const int8_t noisedbm,
                              const uint32_t reflap, const uint8_t refuap,
                              const btbb_packet *pkt);
/* record a BDADDR to PCAPNG capture file */
int btbb_pcapng_record_bdaddr(btbb_pcapng_handle * h, const uint64_t bdaddr,
                              const uint8_t uapmask, const uint8_t napvalid);
/* record BT CLOCK to PCAPNG capture file */
int btbb_pcapng_record_btclock(btbb_pcapng_handle * h, const uint64_t bdaddr,
                               const uint64_t ns, const uint32_t clk, const uint32_t clkmask);
int btbb_pcapng_close(btbb_pcapng_handle * h);


/* BLE support */
typedef struct lell_packet lell_packet;
/* Decode and allocate an LE packet; *pkt is left NULL if the packet
 * cannot be allocated. The _n form is the one to call: it takes the
 * number of bytes 'stream' holds and reads no more than that, keeping
 * whatever of the packet it was given and no further, and it stores at
 * most 260 bytes, the longest LE packet. The form without a length
 * reads 64 bytes, which is what it has always read and what every
 * caller it has had provides. */
void lell_allocate_and_decode(const uint8_t *stream, uint16_t phys_channel, uint32_t clk100ns, lell_packet **pkt);
void lell_allocate_and_decode_n(const uint8_t *stream, size_t stream_len,
                                uint16_t phys_channel, uint32_t clk100ns,
                                lell_packet **pkt);
lell_packet *lell_packet_new(void);
void lell_packet_ref(lell_packet *pkt);
void lell_packet_unref(lell_packet *pkt);
uint32_t lell_get_access_address(const lell_packet *pkt);
unsigned lell_get_access_address_offenses(const lell_packet *pkt);
unsigned lell_packet_is_data(const lell_packet *pkt);
unsigned lell_get_channel_index(const lell_packet *pkt);
unsigned lell_get_channel_k(const lell_packet *pkt);
unsigned lell_get_payload_length(const lell_packet *pkt);
const char * lell_get_adv_type_str(const lell_packet *pkt);
void lell_print(const lell_packet *pkt);

typedef struct lell_pcapng_handle lell_pcapng_handle;
/* create a PCAPNG file for LE captures */
int lell_pcapng_create_file(const char *filename, const char *interface_desc, lell_pcapng_handle ** ph);

/* lell_pcapng_append_packet() returns this when the packet reached the file
   but the connection parameters it carried did not. A CONNECT_IND's
   parameters are recorded as an option of the interface description block,
   whose room for them is set aside when the file is created, and a long
   capture can fill it. A capture tool is expected to warn once and carry on.

   It is positive because the packet was written; a failed write is negative,
   as every error from these writers is. So a caller has to test a write for
   "< 0" and not for "non-zero", or it will take a capture that is still
   being written for a broken one. */
#define LELL_PCAPNG_CONNECT_INFO_NOT_RECORDED 1

/* save an LE packet to PCAPNG capture file */
int lell_pcapng_append_packet(lell_pcapng_handle * h, const uint64_t ns,
                              const int8_t sigdbm, const int8_t noisedbm,
                              const uint32_t refAA, const lell_packet *pkt);
/* Record the parameters of an LE CONNECT_IND in a PCAPNG capture file.
   'pdu' points at the 34 bytes of the PDU's payload, InitA through HopSCA,
   which in a captured frame start 6 bytes in, after the access address and
   the PDU header. */
int lell_pcapng_record_connect_req(lell_pcapng_handle * h, const uint64_t ns, const uint8_t * pdu);
int lell_pcapng_close(lell_pcapng_handle *h);


/* PCAP Support */
typedef struct btbb_pcap_handle btbb_pcap_handle;
/* create a PCAP file for BREDR captures with LINKTYPE_BLUETOOTH_BREDR_BB */
int btbb_pcap_create_file(const char *filename, btbb_pcap_handle ** ph);
/* write a BREDR packet to PCAP file */
int btbb_pcap_append_packet(btbb_pcap_handle * h, const uint64_t ns,
                            const int8_t sigdbm, const int8_t noisedbm,
                            const uint32_t reflap, const uint8_t refuap,
                            const btbb_packet *pkt);
int btbb_pcap_close(btbb_pcap_handle * h);

typedef struct lell_pcap_handle lell_pcap_handle;
/* create a PCAP file for LE captures using LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR */
int lell_pcap_create_file(const char *filename, lell_pcap_handle ** ph);
/* create a PCAP file for LE captures using LINKTYPE_PPI */
int lell_pcap_ppi_create_file(const char *filename, int btle_ppi_version, lell_pcap_handle ** ph);
/* save an LE packet to PCAP capture file */
int lell_pcap_append_packet(lell_pcap_handle * h, const uint64_t ns,
                            const int8_t sigdbm, const int8_t noisedbm,
                            const uint32_t refAA, const lell_packet *pkt);
int lell_pcap_append_ppi_packet(lell_pcap_handle * h, const uint64_t ns,
                                const uint8_t clkn_high,
                                const int8_t rssi_min, const int8_t rssi_max,
                                const int8_t rssi_avg, const uint8_t rssi_count,
                                const lell_packet *pkt);
int lell_pcap_close(lell_pcap_handle *h);

#ifdef __cplusplus
} // __cplusplus defined.
#endif

#endif /* INCLUDED_BTBB_H */
