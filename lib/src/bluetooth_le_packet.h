/* -*- c -*- */
/*
 * Copyright 2007 - 2012 Mike Ryan, Dominic Spill, Michael Ossmann
 * Copyright 2005, 2006 Free Software Foundation, Inc.
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
#ifndef INCLUDED_BLUETOOTH_LE_PACKET_H
#define INCLUDED_BLUETOOTH_LE_PACKET_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The longest payload each kind of PDU may carry. Core v5.4, Vol 6, Part B,
 * Section 2.3 gives an advertising physical channel PDU a Length of 1 to 255
 * octets; Section 2.4 gives a data physical channel PDU 0 to 251, which is
 * where Data Length Extension stopped. */
#define LE_ADV_MAX_PAYLOAD_LENGTH  255
#define LE_DATA_MAX_PAYLOAD_LENGTH 251

/* Access address (4) + PDU header (2) + the longest payload any PDU may
 * carry (255) + CRC (3). An advertising PDU is what needs the full 264: the
 * extended advertising PDUs AUX_ADV_IND and AUX_CHAIN_IND reach the 255 the
 * Length field allows. */
#define MAX_LE_SYMBOLS 264

/* The longest payload the symbol buffer can hold, since the access address,
 * the PDU header and the CRC take 9 of its bytes. Not to be confused with
 * pcap-common.h's LE_MAX_PAYLOAD, which is the whole 264-byte frame a
 * capture record holds. */
#define MAX_LE_PAYLOAD_LENGTH (MAX_LE_SYMBOLS - 9)

/* How many bytes lell_allocate_and_decode() reads. It takes no length, and
 * this is what it has always read, so its callers all pass a buffer at least
 * this long. New callers should use lell_allocate_and_decode_n() instead. */
#define LELL_LEGACY_STREAM_LEN 64

#define LE_ADV_AA 0x8E89BED6

#define ADV_IND			0
#define ADV_DIRECT_IND	1
#define ADV_NONCONN_IND	2
#define SCAN_REQ		3
#define SCAN_RSP		4
#define CONNECT_REQ		5
#define ADV_SCAN_IND	6

struct lell_packet {
	// raw unwhitened bytes of packet, including access address
	uint8_t symbols[MAX_LE_SYMBOLS];

	uint32_t access_address;

	// channel index
	uint8_t channel_idx;
	uint8_t channel_k;

	// number of symbols
	int length;

	uint32_t clk100ns;

	// advertising packet header info
	uint8_t adv_type;
	int adv_tx_add;
	int adv_rx_add;

	unsigned access_address_offenses;
	uint32_t refcount;

	/* flags */
	union {
		struct {
			uint32_t access_address_ok : 1;
		} as_bits;
		uint32_t as_word;
	} flags;
};

#endif /* INCLUDED_BLUETOOTH_LE_PACKET_H */
