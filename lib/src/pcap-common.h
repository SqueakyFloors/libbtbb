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
#ifndef PCAP_COMMON_DOT_H
#define PCAP_COMMON_DOT_H

#include "btbb.h"

// since we no longer rely on or assume the existence of libpcap,
// hardcode these DLTs
#define DLT_PPI 192

#if defined( __APPLE__ )
#include <CoreServices/CoreServices.h>
#define htobe32 EndianU32_NtoB
#define be32toh EndianU32_BtoN
#define le32toh EndianU32_LtoN
#define htobe64 EndianU64_NtoB
#define be64toh EndianU64_BtoN
#define htole16 EndianU16_NtoL
#define htole32 EndianU32_NtoL
#else
#include <endian.h>
#endif

#if !defined( htole16 ) /* will be defined under Linux when endian.h already included */
#if defined( __GNUC__ )
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
inline uint16_t htole16(uint16_t host_16bits) { return host_16bits; }
inline uint16_t le16toh(uint16_t little_endian_16bits) { return little_endian_16bits; }
inline uint32_t htole32(uint32_t host_32bits) { return host_32bits; }
inline uint32_t le32toh(uint32_t little_endian_32bits) { return little_endian_32bits; }
inline uint64_t htole64(uint64_t host_64bits) { return host_64bits; }
inline uint64_t le64toh(uint64_t little_endian_64bits) { return little_endian_64bits; }
#else
#error "FIXME: need to support big-endian under GNU"
#endif /* __BYTE_ORDER__ */
#else /* not GNU C */
#error "FIXME: need to support non-GNU compiler"
#endif /* __GNUC__ */
#endif /* htole16 */

#ifndef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#endif

/* --------------------------------- BR/EDR ----------------------------- */


#define BREDR_DEWHITENED        0x0001
#define BREDR_SIGPOWER_VALID    0x0002
#define BREDR_NOISEPOWER_VALID  0x0004
#define BREDR_PAYLOAD_DECRYPTED 0x0008
#define BREDR_REFLAP_VALID      0x0010
#define BREDR_PAYLOAD_PRESENT   0x0020
#define BREDR_CHANNEL_ALIASED   0x0040
#define BREDR_REFUAP_VALID      0x0080
#define BREDR_HEC_CHECKED       0x0100
#define BREDR_HEC_VALID         0x0200
#define BREDR_CRC_CHECKED       0x0400
#define BREDR_CRC_VALID         0x0800
#define BREDR_MIC_CHECKED       0x1000
#define BREDR_MIC_VALID         0x2000

#define BREDR_MAX_PAYLOAD       400
#if !defined( DLT_BLUETOOTH_BREDR_BB )
#define DLT_BLUETOOTH_BREDR_BB 255
#endif

/* The flag word both BR/EDR writers put in a record. The two writers build
   the same LINKTYPE_BLUETOOTH_BREDR_BB record, so they agree on it here. */
static inline uint16_t bredr_record_flags( const int8_t sigdbm,
					   const int8_t noisedbm,
					   const uint32_t reflap,
					   const uint8_t refuap,
					   const btbb_packet *pkt )
{
	return (uint16_t) (BREDR_DEWHITENED | BREDR_SIGPOWER_VALID |
		((noisedbm < sigdbm) ? BREDR_NOISEPOWER_VALID : 0) |
		((reflap != LAP_ANY) ? BREDR_REFLAP_VALID : 0) |
		((refuap != UAP_ANY) ? BREDR_REFUAP_VALID : 0) |
		/* What the library actually verified. It checks the header's
		   HEC whenever it decodes a header, and checks a payload's
		   CRC whenever the packet type carries one, and records both
		   answers on the packet. A reader needs these to tell a
		   verified payload from a guess. */
		(btbb_packet_get_flag(pkt, BTBB_HEC_CHECKED) ? BREDR_HEC_CHECKED : 0) |
		(btbb_packet_get_flag(pkt, BTBB_HEC_VALID)   ? BREDR_HEC_VALID   : 0) |
		(btbb_packet_get_flag(pkt, BTBB_CRC_CHECKED) ? BREDR_CRC_CHECKED : 0) |
		(btbb_packet_get_flag(pkt, BTBB_CRC_CORRECT) ? BREDR_CRC_VALID   : 0));
}

typedef struct __attribute__((packed)) _pcap_bluetooth_bredr_bb_header {
        uint8_t rf_channel;
        int8_t signal_power;
        int8_t noise_power;
        uint8_t access_code_offenses;
        uint8_t payload_transport_rate;
        uint8_t corrected_header_bits;
        int16_t corrected_payload_bits;
        uint32_t lap;
        uint32_t ref_lap_uap;
        uint32_t bt_header;
        uint16_t flags;
	uint8_t bredr_payload[BREDR_MAX_PAYLOAD];
} pcap_bluetooth_bredr_bb_header;

/* --------------------------------- Low Energy ---------------------------- */

#if !defined( DLT_BLUETOOTH_LE_LL )
#define DLT_BLUETOOTH_LE_LL 251
#endif

#if !defined( DLT_BLUETOOTH_LE_LL_WITH_PHDR )
#define DLT_BLUETOOTH_LE_LL_WITH_PHDR 256
#endif
typedef struct __attribute__((packed)) _pcap_bluetooth_le_ll_header {
        uint8_t rf_channel;
        int8_t signal_power;
        int8_t noise_power;
        uint8_t access_address_offenses;
        uint32_t ref_access_address;
        uint16_t flags;
        uint8_t le_packet[0];
} pcap_bluetooth_le_ll_header;

#define LE_DEWHITENED        0x0001
#define LE_SIGPOWER_VALID    0x0002
#define LE_NOISEPOWER_VALID  0x0004
#define LE_PACKET_DECRYPTED  0x0008
#define LE_REF_AA_VALID      0x0010
#define LE_AA_OFFENSES_VALID 0x0020
#define LE_CHANNEL_ALIASED   0x0040
#define LE_CRC_CHECKED       0x0400
#define LE_CRC_VALID         0x0800
#define LE_MIC_CHECKED       0x1000
#define LE_MIC_VALID         0x2000

/* Full captured LE frame: access address (4) + header (2) + payload (251) + CRC (3). */
#define LE_MAX_PAYLOAD       260

#endif /* PCAP_COMMON_DOT_H */
