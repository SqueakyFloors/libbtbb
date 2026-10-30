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

#include "btbb.h"
#include "bluetooth_le_packet.h"
#include "bluetooth_packet.h"
#include "pcapng-bt.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

/* generic section options indicating libbtbb */
static const struct {
	struct {
		option_header hdr;
		char libname[8];
	} libopt;
	struct {
		option_header hdr;
	} termopt;
} libbtbb_section_options = {
	.libopt = {
		.hdr = {
			.option_code = SHB_USERAPPL,
			.option_length = 7 },
		.libname = "libbtbb"
	},
	.termopt = {
		.hdr = {
			.option_code = OPT_ENDOFOPT,
			.option_length = 0
		}
	}
};

/*
 * The option list a caller's interface description is passed to the writer
 * in.
 *
 * A reader of an option list steps from one option to the next by the length
 * in each option's header, and stops at an option whose code is zero. So the
 * list has to carry that terminator itself. Without one, a description long
 * enough to fill desc puts the step past it on the four bytes after this
 * object, and pcapng_create() and check_and_fix_tsresol() both read an option
 * header from there.
 *
 * desc is zeroed, so a shorter description already stops the walk inside it;
 * the terminator is what a description of 253 characters or more stops on,
 * those being the lengths whose padded option fills desc. The description is
 * cut to 255 characters, which is what fits in desc and what the writer has
 * always copied.
 */
typedef struct {
	option_header header;
	char desc[256];
	option_header terminator;
} interface_desc_option;

static const option_header *
interface_desc_options( interface_desc_option * opt, const char * desc )
{
	if (desc == NULL) {
		return NULL;
	}

	memset( opt, 0, sizeof( *opt ) );
	opt->terminator.option_code = OPT_ENDOFOPT;
	opt->header.option_code = IF_DESCRIPTION;
	(void) strncpy( &opt->desc[0], desc, sizeof( opt->desc ) - 1 );
	opt->header.option_length = (uint16_t) strlen( &opt->desc[0] );

	return (const option_header *) opt;
}

static PCAPNG_RESULT
check_and_fix_tsresol( PCAPNG_HANDLE * handle,
		       const option_header * interface_options )
{
	PCAPNG_RESULT retval = PCAPNG_OK;
	int got_tsresol = 0;

	while( !got_tsresol &&
	       interface_options &&
	       interface_options->option_code &&
	       interface_options->option_length) {
		if (interface_options->option_code == IF_TSRESOL) {
			got_tsresol = 1;
		}
		else {
			size_t step = 4+4*((interface_options->option_length+3)/4);
			uint8_t * next = &((uint8_t *)interface_options)[step];
			interface_options = (const option_header *) next;
		}
	}

	if (!got_tsresol) {
		const struct {
			option_header hdr;
			uint8_t resol;
		} tsresol = {
			.hdr = {
				.option_code = IF_TSRESOL,
				.option_length = 1,
			},
			.resol = 9 /* 10^-9 is nanoseconds */
		};

		retval = pcapng_append_interface_option( handle, 
							 (const option_header *) &tsresol );
	}

	return retval;
}

/* --------------------------------- BR/EDR ----------------------------- */

static PCAPNG_RESULT
create_bredr_capture_file_single_interface( PCAPNG_HANDLE * handle,
					    const char * filename,
					    const option_header * interface_options )
{
	PCAPNG_RESULT retval = PCAPNG_OK;

	retval = pcapng_create( handle,
				filename,
				(const option_header *) &libbtbb_section_options, 
				(size_t) getpagesize( ),
				DLT_BLUETOOTH_BREDR_BB,
				sizeof(pcap_bluetooth_bredr_bb_header),
				interface_options,
				(size_t) getpagesize( ) );

	if (retval == PCAPNG_OK) {
		/* if there is no timestamp resolution alread in the
		   interface options, record nanosecond resolution */
		retval = check_and_fix_tsresol( handle, interface_options );

		if (retval != PCAPNG_OK) {
			/* no capture file came of it, so it does not
			   outlive this call either */
			pcapng_remove_own_file( handle->fd, filename );
			(void) pcapng_close( handle );
		}
	}

	return retval;
}

int btbb_pcapng_create_file( const char *filename,
			     const char *interface_desc,
			     btbb_pcapng_handle ** ph )
{
	int retval = PCAPNG_OK;
	PCAPNG_HANDLE * handle = malloc( sizeof(PCAPNG_HANDLE) );
	if (handle) {
		interface_desc_option ifopt;
		const option_header * popt =
			interface_desc_options( &ifopt, interface_desc );

		retval = -create_bredr_capture_file_single_interface( handle,
								      filename,
								      popt );
		if (retval == PCAPNG_OK) {
			*ph = (btbb_pcapng_handle *) handle;
		}
		else {
			free( handle );
		}
	}
	else {
		retval = -PCAPNG_NO_MEMORY;
	}
	return retval;
}

static PCAPNG_RESULT
append_bredr_packet( PCAPNG_HANDLE * handle,
		     pcapng_bredr_packet * pkt )
{
	return pcapng_append_packet( handle, ( const enhanced_packet_block *) pkt );
}

static void
assemble_pcapng_bredr_packet( pcapng_bredr_packet * pkt,
			      const uint32_t interface_id,
			      const uint64_t ns,
			      const uint32_t caplen,
			      const uint8_t rf_channel,
			      const int8_t signal_power,
			      const int8_t noise_power,
			      const uint8_t access_code_offenses,
			      const uint8_t payload_transport,
			      const uint8_t payload_rate,
			      const uint8_t corrected_header_bits,
			      const int16_t corrected_payload_bits,
			      const uint32_t lap,
			      const uint32_t ref_lap,
                              const uint8_t ref_uap,
			      const uint32_t bt_header,
			      const uint16_t flags,
			      const char * payload )
{
	uint32_t pcapng_caplen = sizeof(pcap_bluetooth_bredr_bb_header)  
				 - sizeof(pkt->bredr_bb_header.bredr_payload)  
				 + caplen;
	uint32_t block_length  = PCAPNG_BLOCK_LENGTH( pcapng_caplen );
	/* The cast keeps the shift out of a signed int, where a reference UAP
	   of 0x80 or above would overflow into the sign bit. */
	uint32_t reflapuap = (ref_lap&0xffffff) | ((uint32_t) ref_uap << 24);

	/* The block is rounded up to a 4-byte boundary, so up to three bytes
	 * after the payload are written out without ever being filled in.
	 * Clear the whole record first, so a capture file cannot carry
	 * whatever the caller's memory happened to hold there. */
	memset( pkt, 0, sizeof( *pkt ) );

	pkt->blk_header.block_type = BLOCK_TYPE_ENHANCED_PACKET;
	pkt->blk_header.block_total_length = block_length;
	pkt->blk_header.interface_id = interface_id;
	pkt->blk_header.timestamp_high = (uint32_t) (ns >> 32);
	pkt->blk_header.timestamp_low = (uint32_t) (ns & 0x0ffffffffull);
	pkt->blk_header.captured_len = pcapng_caplen;
	pkt->blk_header.packet_len = pcapng_caplen;
	pkt->bredr_bb_header.rf_channel = rf_channel;
	pkt->bredr_bb_header.signal_power = signal_power;
	pkt->bredr_bb_header.noise_power = noise_power;
	pkt->bredr_bb_header.access_code_offenses = access_code_offenses;
	pkt->bredr_bb_header.payload_transport_rate =
		(payload_transport << 4) | payload_rate;
	pkt->bredr_bb_header.corrected_header_bits = corrected_header_bits;
	pkt->bredr_bb_header.corrected_payload_bits = htole16( corrected_payload_bits );
	pkt->bredr_bb_header.lap = htole32( lap );
	pkt->bredr_bb_header.ref_lap_uap = htole32( reflapuap );
	pkt->bredr_bb_header.bt_header = htole32( bt_header );
	pkt->bredr_bb_header.flags = htole16( flags );
	if (caplen) {
		assert(caplen <= sizeof(pkt->bredr_bb_header.bredr_payload)); // caller ensures this, but to be safe..
		(void) memcpy( &pkt->bredr_bb_header.bredr_payload[0], payload, caplen );
		pkt->bredr_bb_header.flags |= htole16( BREDR_PAYLOAD_PRESENT );
	}
	else {
		pkt->bredr_bb_header.flags &= htole16( ~BREDR_PAYLOAD_PRESENT );
	}
	((uint32_t *)pkt)[block_length/4-2] = 0x00000000; /* no-options */
	((uint32_t *)pkt)[block_length/4-1] = block_length;
}

int btbb_pcapng_append_packet(btbb_pcapng_handle * h, const uint64_t ns,
			      const int8_t sigdbm, const int8_t noisedbm,
			      const uint32_t reflap, const uint8_t refuap, 
			      const btbb_packet *pkt)
{
	uint16_t flags = bredr_record_flags( sigdbm, noisedbm, reflap, refuap, pkt );
	/* A NULL or POLL packet has no payload, so the length is 0 and a
	   buffer sized from it would be a zero-length array, which C does not
	   define. The record holds BREDR_MAX_PAYLOAD bytes, and the decoder
	   never produces more than a DH5's 343, so size the buffer for the
	   record and leave a longer payload unrecorded rather than overrun
	   it. */
	char payload_bytes[BREDR_MAX_PAYLOAD];
	int payload_length = btbb_packet_get_payload_length(pkt);
	uint32_t caplen = 0;
	/* The format gives neither corrected-bits field a validity flag, so a
	   reader takes both at face value. The payload's count describes the
	   payload in the record, so it is only recorded alongside one.

	   The count goes in as the decoder made it. The published layout
	   describes this field as running to +180 for the rate 2/3 code,
	   which corrects one bit per 15-symbol block; the longest payload
	   that code carries is a DM5's 228 bytes, which is 183 blocks, so a
	   reception that needed a correction in every block records 183. A
	   reader is better served by the real number than by one capped to a
	   description three short of the format's own definition, and a
	   signed 16-bit field holds it. */
	int16_t corrected_payload_bits = 0;
	pcapng_bredr_packet pcapng_pkt;

	/* Only a payload the decoder produced for this packet goes in the
	   record. A rejected candidate clock from the search for a piconet's
	   address part leaves a payload behind without that flag. */
	if (btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) &&
	    (payload_length > 0) && (payload_length <= MAX_PAYLOAD_LENGTH / 8)) {
		caplen = (uint32_t) btbb_get_payload_packed( pkt, &payload_bytes[0] );
		corrected_payload_bits = pkt->corrected_payload_bits;
	}

	assemble_pcapng_bredr_packet( &pcapng_pkt,
				      0,
				      ns,
				      caplen,
				      btbb_packet_get_channel(pkt),
				      sigdbm,
				      noisedbm,
				      btbb_packet_get_ac_errors(pkt),
				      btbb_packet_get_transport(pkt),
				      btbb_packet_get_modulation(pkt),
				      pkt->corrected_header_bits,
				      corrected_payload_bits,
				      btbb_packet_get_lap(pkt),
				      reflap,
				      refuap,
				      btbb_packet_get_header_packed(pkt),
				      flags,
				      payload_bytes );
	return -append_bredr_packet( (PCAPNG_HANDLE *)h, &pcapng_pkt );
}

static PCAPNG_RESULT
record_bd_addr_info( PCAPNG_HANDLE * handle,
		     const uint64_t bd_addr,
		     const uint8_t  uap_mask,
                     const uint8_t  nap_valid )
{
	const bredr_br_addr_option bdopt = {
		.header = {
			.option_code = PCAPNG_BREDR_OPTION_BD_ADDR,
			/* the length of the option's value, which does not
			   include this 4-byte header */
			.option_length = sizeof(bredr_bd_addr_info),
		},
		.bd_addr_info = {
			.bd_addr = {
				((bd_addr>>0)  & 0xff),
				((bd_addr>>8)  & 0xff),
				((bd_addr>>16) & 0xff),
				((bd_addr>>24) & 0xff),
				((bd_addr>>32) & 0xff),
				((bd_addr>>40) & 0xff)
			},
			.uap_mask = uap_mask,
			.nap_valid = nap_valid,
		}
	};
	return pcapng_append_interface_option( handle,
					       (const option_header *) &bdopt );
}

int btbb_pcapng_record_bdaddr(btbb_pcapng_handle * h, const uint64_t bdaddr,
                              const uint8_t uapmask, const uint8_t napvalid)
{
	return -record_bd_addr_info( (PCAPNG_HANDLE *) h,
				     bdaddr, uapmask, napvalid );
}

static PCAPNG_RESULT
record_bredr_master_clock_info( PCAPNG_HANDLE * handle,
				const uint64_t bd_addr,
				const uint64_t ns,
				const uint32_t clk,
	                        const uint32_t clk_mask)
{
	const bredr_clk_option mcopt = {
		.header = {
			.option_code = PCAPNG_BREDR_OPTION_MASTER_CLOCK_INFO,
			/* the length of the option's value, which does not
			   include this 4-byte header */
			.option_length = sizeof(bredr_clk_info)
		},
		.clock_info = {
			.ts = ns,
			.lap_uap = htole32(bd_addr & 0xffffffff),
			.clk = clk,
			.clk_mask = clk_mask
		}
	};
	return pcapng_append_interface_option( handle,
					       (const option_header *) &mcopt );
}

int btbb_pcapng_record_btclock(btbb_pcapng_handle * h, const uint64_t bdaddr,
                               const uint64_t ns, const uint32_t clk,
			       const uint32_t clkmask)
{
	return -record_bredr_master_clock_info( (PCAPNG_HANDLE *) h,
						bdaddr, ns, clk, clkmask );
}

int btbb_pcapng_close(btbb_pcapng_handle * h)
{
	PCAPNG_RESULT result = PCAPNG_INVALID_HANDLE;

	if (h) {
		result = pcapng_close( (PCAPNG_HANDLE *) h );
		free( h );
	}
	return -result;
}

/* --------------------------------- Low Energy ---------------------------- */

static PCAPNG_RESULT
create_le_capture_file_single_interface( PCAPNG_HANDLE * handle,
					 const char * filename,
					 const option_header * interface_options )
{
	PCAPNG_RESULT retval = PCAPNG_OK;

	retval = pcapng_create( handle,
				filename,
				(const option_header *) &libbtbb_section_options, 
				(size_t) getpagesize( ),
				DLT_BLUETOOTH_LE_LL_WITH_PHDR,
				sizeof(pcap_bluetooth_le_ll_header) + LE_MAX_PAYLOAD,
				interface_options,
				(size_t) getpagesize( ) );

	if (retval == PCAPNG_OK) {
		/* if there is no timestamp resolution alread in the
		   interface options, record nanosecond resolution */
		retval = check_and_fix_tsresol( handle, interface_options );

		if (retval != PCAPNG_OK) {
			/* no capture file came of it, so it does not
			   outlive this call either */
			pcapng_remove_own_file( handle->fd, filename );
			(void) pcapng_close( handle );
		}
	}

	return retval;
}

int
lell_pcapng_create_file(const char *filename, const char *interface_desc,
			lell_pcapng_handle ** ph)
{
	int retval = PCAPNG_OK;
	PCAPNG_HANDLE * handle = malloc( sizeof(PCAPNG_HANDLE) );
	if (handle) {
		interface_desc_option ifopt;
		const option_header * popt =
			interface_desc_options( &ifopt, interface_desc );

		retval = -create_le_capture_file_single_interface( handle,
								   filename,
								   popt );
		if (retval == PCAPNG_OK) {
			*ph = (lell_pcapng_handle *) handle;
		}
		else {
			free( handle );
		}
	}
	else {
		retval = -PCAPNG_NO_MEMORY;
	}
	return retval;
}

static PCAPNG_RESULT
append_le_packet( PCAPNG_HANDLE * handle,
		  pcapng_le_packet * pkt )
{
	return pcapng_append_packet( handle, ( const enhanced_packet_block *) pkt );
}

static void
assemble_pcapng_le_packet( pcapng_le_packet * pkt,
			   const uint32_t interface_id,
			   const uint64_t ns,
			   const uint32_t caplen,
			   const uint8_t rf_channel,
			   const int8_t signal_power,
			   const int8_t noise_power,
			   const uint8_t access_address_offenses,
			   const uint32_t ref_access_address,
			   const uint16_t flags,
			   const uint8_t * lepkt )
{
	uint32_t stored_caplen = MIN(LE_MAX_PAYLOAD, caplen);
	uint32_t pcapng_caplen = sizeof(pcap_bluetooth_le_ll_header)+stored_caplen;
	uint32_t pcapng_origlen = sizeof(pcap_bluetooth_le_ll_header)+caplen;
	uint32_t block_length  = PCAPNG_BLOCK_LENGTH( pcapng_caplen );

	/* The block is rounded up to a 4-byte boundary, so up to three bytes
	 * after the packet are written out without ever being filled in. Clear
	 * the whole record first, so a capture file cannot carry whatever the
	 * caller's memory happened to hold there. */
	memset( pkt, 0, sizeof( *pkt ) );

	pkt->blk_header.block_type = BLOCK_TYPE_ENHANCED_PACKET;
	pkt->blk_header.block_total_length = block_length;
	pkt->blk_header.interface_id = interface_id;
	pkt->blk_header.timestamp_high = (uint32_t) (ns >> 32);
	pkt->blk_header.timestamp_low = (uint32_t) (ns & 0x0ffffffffull);
	pkt->blk_header.captured_len = pcapng_caplen;
	pkt->blk_header.packet_len = pcapng_origlen;
	pkt->le_ll_header.rf_channel = rf_channel;
	pkt->le_ll_header.signal_power = signal_power;
	pkt->le_ll_header.noise_power = noise_power;
	pkt->le_ll_header.access_address_offenses = access_address_offenses;
	pkt->le_ll_header.ref_access_address = htole32( ref_access_address );
	pkt->le_ll_header.flags = htole16( flags );
	(void) memcpy( &pkt->le_packet[0], lepkt, stored_caplen );
	((uint32_t *)pkt)[block_length/4-2] = 0x00000000; /* no-options */
	((uint32_t *)pkt)[block_length/4-1] = block_length;
}

int
lell_pcapng_append_packet(lell_pcapng_handle * h, const uint64_t ns,
			  const int8_t sigdbm, const int8_t noisedbm,
			  const uint32_t refAA, const lell_packet *pkt)
{
	uint16_t flags = LE_DEWHITENED | LE_AA_OFFENSES_VALID |
		LE_SIGPOWER_VALID |
		((noisedbm < sigdbm) ? LE_NOISEPOWER_VALID : 0) |
		LE_REF_AA_FLAG(refAA);
	pcapng_le_packet pcapng_pkt;

	/* The extra 9 bytes added to the packet length are for:
	   4 bytes for Access Address
	   2 bytes for PDU header
	   3 bytes for CRC */
	assemble_pcapng_le_packet( &pcapng_pkt,
				   0,
				   ns,
				   LE_FRAMING_BYTES+pkt->length,
				   pkt->channel_k,
				   sigdbm,
				   noisedbm,
				   pkt->access_address_offenses,
				   refAA,
				   flags,
				   &pkt->symbols[0] );
	int retval = -append_le_packet( (PCAPNG_HANDLE *) h, &pcapng_pkt );
	if ((retval == 0) && !lell_packet_is_data(pkt) &&
	    (pkt->adv_type == CONNECT_REQ) &&
	    (pkt->length >= LE_CONNECT_IND_PDU_LENGTH)) {
		/* The option records the PDU's 34 payload bytes, InitA through
		   HopSCA. In the capture those start after the 4-byte access
		   address and the 2-byte PDU header. */
		if (lell_pcapng_record_connect_req(h, ns,
						   &pkt->symbols[6]) != 0) {
			/* The room for these options is set aside when the
			   file is created and each one takes 48 bytes of it,
			   so a long capture can fill it, and with the packet
			   above just written to this handle that is the only
			   way to get here. Say so, rather than carry on
			   recording packets and silently stop recording the
			   parameters a reader needs to follow the connections
			   in them. The packet itself is in the file, which is
			   why this is not an error. */
			retval = LELL_PCAPNG_CONNECT_INFO_NOT_RECORDED;
		}
	}
	return retval;
}

static PCAPNG_RESULT
record_le_connect_req_info( PCAPNG_HANDLE * handle,
			    const uint64_t ns,
			    const uint8_t * pdu )
{
	le_ll_connection_info_option cropt = {
		.header = {
			.option_code = PCAPNG_LE_LL_CONNECTION_INFO,
			/* the length of the option's value, which does not
			   include this 4-byte header */
			.option_length = sizeof(le_ll_connection_info)
		},
		.connection_info = {
			.ns = ns
		}
	};
	(void) memcpy( &cropt.connection_info.pdu.bytes[0], pdu,
		       LE_CONNECT_IND_PDU_LENGTH );
	return pcapng_append_interface_option( handle,
					       (const option_header *) &cropt );
}

int
lell_pcapng_record_connect_req(lell_pcapng_handle * h, const uint64_t ns, 
			       const uint8_t * pdu)
{
	return -record_le_connect_req_info( (PCAPNG_HANDLE *) h, ns, pdu );
}

int lell_pcapng_close(lell_pcapng_handle *h)
{
	PCAPNG_RESULT result = PCAPNG_INVALID_HANDLE;

	if (h) {
		result = pcapng_close( (PCAPNG_HANDLE *) h );
		free( h );
	}
	return -result;
}
