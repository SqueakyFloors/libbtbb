/* -*- c -*- */
/*
 * Copyright 2014 Christopher D. Kilgour techie AT whiterocker.com
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
#ifndef PCAPNG_BT_DOT_H
#define PCAPNG_BT_DOT_H

#include "pcap-common.h"
#include "pcapng.h"

typedef struct __attribute__((packed)) {
	uint32_t centre_freq;
	uint32_t analog_bandwidth;
	int32_t  intermediate_freq;
	uint32_t sampling_bandwidth;
} bt_wideband_rf_info;

typedef struct __attribute__((packed)) {
  option_header header;
  bt_wideband_rf_info wideband_rf_info;
} bt_wideband_rf_option;

#define PCAPNG_BT_WIDEBAND_RF_INFO 0xd300

/* Size of a pcapng enhanced packet block with nothing in it: the 28-byte
   block header, the 4-byte options word and the 4-byte trailing length.
   NOTE: the Bluetooth pseudoheader counts as part of the block's packet
   data, so it is not included here. */
#define PCAPNG_ENHANCED_BLK_SZ 36

/* The total length of an enhanced packet block carrying this much packet
   data, rounded up to the 4-byte boundary a pcapng block ends on. */
#define PCAPNG_BLOCK_LENGTH(packet_data_size) \
  (4*((PCAPNG_ENHANCED_BLK_SZ + (packet_data_size) + 3)/4))

/* --------------------------------- BR/EDR ----------------------------- */

/* The writers put the options word and the trailing block length in place a
   word at a time, so a record has to be aligned for that even though its
   members are packed. */
typedef struct __attribute__((packed, aligned(4))) {
  enhanced_packet_block blk_header;
  pcap_bluetooth_bredr_bb_header bredr_bb_header;

  /* The block header, the pseudoheader and BREDR_MAX_PAYLOAD together come
     to 2 more than a multiple of 4, so these two bytes round the record up
     to a 4-byte boundary. A longer BREDR_MAX_PAYLOAD would need this
     adjusted, or the longest block would run past the end of this struct. */
  uint8_t pad[2];

  /* Room for the options word and the trailing block length. They are
     written at the aligned end of the record actually built, which for a
     payload shorter than the maximum is before these two members. */
  uint32_t options;
  uint32_t blk_tot_length;
} pcapng_bredr_packet;

typedef struct __attribute__((packed)) {
  uint8_t bd_addr[6];
  uint8_t uap_mask;
  uint8_t nap_valid;
} bredr_bd_addr_info;

typedef struct __attribute__((packed)) {
  option_header header;
  bredr_bd_addr_info bd_addr_info;
} bredr_br_addr_option;

typedef struct __attribute__((packed)) {
  uint64_t ts;
  uint32_t lap_uap;
  uint32_t clk;
  uint32_t clk_mask;
} bredr_clk_info;

typedef struct __attribute__((packed)) {
  option_header header;
  bredr_clk_info clock_info;
} bredr_clk_option;

#define PCAPNG_BREDR_OPTION_BD_ADDR           0xd340
#define PCAPNG_BREDR_OPTION_MASTER_CLOCK_INFO 0xd341

/* --------------------------------- Low Energy ---------------------------- */

typedef struct __attribute__((packed, aligned(4))) {
  enhanced_packet_block blk_header;
  pcap_bluetooth_le_ll_header le_ll_header;
  uint8_t le_packet[LE_MAX_PAYLOAD];

  /* The block header, the pseudoheader and LE_MAX_PAYLOAD together come to
     2 more than a multiple of 4, so these two bytes round the record up to a
     4-byte boundary. What matters is that residue, not the size itself: a
     change to LE_MAX_PAYLOAD that leaves it 2 more than a multiple of 4
     needs no adjustment here, and any other change does, or the longest
     block would run past the end of this struct. The assertion at the end of
     this file is what catches it. */
  uint8_t pad[2];

  /* Room for the options word and the trailing block length. They are
     written at the aligned end of the record actually built, which for a
     packet shorter than the maximum is before these two members. */
  uint32_t options;
  uint32_t blk_tot_length;
} pcapng_le_packet;

typedef struct __attribute__((packed)) {
  uint64_t ns;
  union {
    struct {
      uint8_t InitA[6];
      uint8_t AdvA[6];
      uint8_t AA[4];
      uint8_t CRCInit[3];
      uint8_t WinSize;
      uint8_t WinOffset[2];
      uint8_t Interval[2];
      uint8_t Latency[2];
      uint8_t Timeout[2];
      uint8_t ChM[5];
      uint8_t HopSCA;
    } fields;
    uint8_t bytes[0];
  } pdu;
} le_ll_connection_info;

typedef struct __attribute__((packed)) {
  option_header header;
  le_ll_connection_info connection_info;
} le_ll_connection_info_option;

#define PCAPNG_LE_LL_CONNECTION_INFO 0xd380

/* The payload length a CONNECT_IND PDU declares: InitA and AdvA, 6 bytes
   each, then the 22 bytes of LLData that carry the connection parameters. */
#define LE_CONNECT_IND_PDU_LENGTH 34

/* The writers build a block at the start of one of these records, so each
   record must be long enough for the longest block of its kind. Raising a
   maximum payload without widening the record would otherwise write past
   the end of a stack object. */
typedef char pcapng_bredr_packet_is_long_enough[
  (sizeof(pcapng_bredr_packet) >=
   PCAPNG_BLOCK_LENGTH(sizeof(pcap_bluetooth_bredr_bb_header))) ? 1 : -1];
typedef char pcapng_le_packet_is_long_enough[
  (sizeof(pcapng_le_packet) >=
   PCAPNG_BLOCK_LENGTH(sizeof(pcap_bluetooth_le_ll_header) +
			   LE_MAX_PAYLOAD)) ? 1 : -1];

#endif /* PCAPNG_BT_DOT_H */
