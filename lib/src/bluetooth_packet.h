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
#ifndef INCLUDED_BLUETOOTH_PACKET_H
#define INCLUDED_BLUETOOTH_PACKET_H
#include "btbb.h"

/* maximum number of symbols */
#define MAX_SYMBOLS 3125

/* maximum number of payload bits */
#define MAX_PAYLOAD_LENGTH 2744

/* The longest payload each eSCO packet type carries, in bytes including its
 * two byte CRC (Vol 2, Part B, Section 6.5.3). The length itself is not in
 * the packet: it is agreed when the link is set up, so a decode that is not
 * told it searches up to these. */
#define EV3_MAX_PAYLOAD (30 + 2)
#define EV4_MAX_PAYLOAD (120 + 2)
#define EV5_MAX_PAYLOAD (180 + 2)

/* minimum header bit errors to indicate that this is an ID packet */
#define ID_THRESHOLD 5

#define PACKET_TYPE_NULL 0
#define PACKET_TYPE_POLL 1
#define PACKET_TYPE_FHS 2
#define PACKET_TYPE_DM1 3
#define PACKET_TYPE_DH1 4
#define PACKET_TYPE_HV1 5
#define PACKET_TYPE_HV2 6
#define PACKET_TYPE_HV3 7
#define PACKET_TYPE_DV 8
#define PACKET_TYPE_AUX1 9
#define PACKET_TYPE_DM3 10
#define PACKET_TYPE_DH3 11
#define PACKET_TYPE_EV4 12
#define PACKET_TYPE_EV5 13
#define PACKET_TYPE_DM5 14
#define PACKET_TYPE_DH5 15

struct btbb_packet {

	uint32_t refcount;

	uint32_t flags;

	uint8_t channel; /* Bluetooth channel (0-79) */
	uint8_t UAP;     /* upper address part */
	uint16_t NAP;    /* non-significant address part */
	uint32_t LAP;    /* lower address part found in access code */

	uint8_t modulation;
	uint8_t transport;
	uint8_t packet_type;
	uint8_t packet_lt_addr; /* LLID field of payload header (2 bits) */
	uint8_t packet_flags; /* Flags - FLOW/ARQN/SQEN */
	uint8_t packet_hec; /* Flags - FLOW/ARQN/SQEN */

	/* packet header, one bit per char */
	char packet_header[18];

	/* number of payload header bytes: 0, 1, 2, or -1 for
	 * unknown. payload is one bit per char. */
	int payload_header_length;
	char payload_header[16];

	/* LLID field of payload header (2 bits) */
	uint8_t payload_llid;

	/* flow field of payload header (1 bit) */
	uint8_t payload_flow;

	/* payload length: the total length of the asynchronous data
	* in bytes.  This does not include the length of synchronous
	* data, such as the voice field of a DV packet.  If there is a
	* payload header, this payload length is payload body length
	* (the length indicated in the payload header's length field)
	* plus payload_header_length plus 2 bytes CRC (if present).
	*/
	int payload_length;

	/* The payload length an eSCO link carries, in information bytes, or 0
	* if the caller has not said. See btbb_packet_set_esco_length(). */
	int esco_length;

	/* The actual payload data in host format
	* Ready for passing to wireshark
	* 2744 is the maximum length, but most packets are shorter.
	* Dynamic allocation would probably be better in the long run but is
	* problematic in the short run.
	*/
	char payload[MAX_PAYLOAD_LENGTH];

	uint16_t crc;
	/* The packet's clock as CLK1-27, a count of 625 us slots, which is
	   half the 312.5 us value btbb_packet_set_data() is given. The name
	   is historic: nothing here holds CLKN. */
	uint32_t clkn;
	uint8_t ac_errors; /* Number of bit errors in the AC */

	/* How many bits the forward error correction had to put right, which
	* the capture file writers record. Both are 0 until something is
	* decoded, and only a type that carries FEC can report more.
	*
	* The rate 1/3 decode gives up when a quarter or more of the bits
	* needed putting right, so its counts stop well below the whole: 0 to
	* 3 of the header's 18 bits, and 0 to 19 of an HV1 payload's 80. A
	* noisier one than that is no payload at all rather than a payload
	* with a high count.
	*
	* The rate 2/3 code puts right one symbol per 15-symbol block, so its
	* count is one per block corrected. The published record layout
	* describes the payload field as running to +180; the longest payload
	* this code carries is a DM5's 228 bytes, which is 183 blocks, so a
	* reception marginal enough to need a correction in every one of them
	* reports 183. The count the decoder made is reported either way: a
	* reader is better served by the real number than by one capped to
	* the description, and a signed 16-bit field holds it.
	*/
	uint8_t corrected_header_bits;
	int16_t corrected_payload_bits;

	/* the raw symbol stream (less the preamble), one bit per char */
	//FIXME maybe this should be a vector so we can grow it only
	//to the size needed and later shrink it if we find we have
	//more symbols than necessary
	uint16_t length; /* number of symbols */
	char symbols[MAX_SYMBOLS];

};

/* type-specific CRC checks and decoding */
int fhs(int clock, btbb_packet* p);
int DM(int clock, btbb_packet* p);
int DH(int clock, btbb_packet* p);
int EV3(int clock, btbb_packet* p);
int EV4(int clock, btbb_packet* p);
int EV5(int clock, btbb_packet* p);
int HV(int clock, btbb_packet* p);

/* check if the packet's CRC is correct for a given clock (CLK1-6) */
int crc_check(int clock, btbb_packet* p);

/* format payload for tun interface */
char *tun_format(btbb_packet* p);

/* try a clock value (CLK1-6) to unwhiten packet header,
 * sets resultant packet_type and UAP, returns UAP.
 *
 * A packet of fewer than 122 symbols holds no header to read, so for one of
 * those this sets nothing and returns 0, which a caller cannot tell from a
 * header that decoded to a UAP of 0x00.
 */
uint8_t try_clock(int clock, btbb_packet* p);

/* extract LAP from FHS payload */
uint32_t lap_from_fhs(btbb_packet* p);

/* extract UAP from FHS payload */
uint8_t uap_from_fhs(btbb_packet* p);

/* extract NAP from FHS payload */
uint16_t nap_from_fhs(btbb_packet* p);

/* extract clock from FHS payload */
uint32_t clock_from_fhs(btbb_packet* p);

#endif /* INCLUDED_BLUETOOTH_PACKET_H */
