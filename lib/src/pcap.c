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
#include "bluetooth_le_packet.h"
#include "bluetooth_packet.h"
#include "btbb.h"
#include "pcap-common.h"
/* for pcapng_remove_own_file(): both writers make a file before they know it
   will be a capture file, and neither leaves one behind that is not */
#include "pcapng.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum {
	PCAP_OK = 0,
	PCAP_INVALID_HANDLE,
	PCAP_FILE_NOT_ALLOWED,
	PCAP_NO_MEMORY,
	PCAP_FILE_WRITE_ERROR,
} PCAP_RESULT;

typedef struct __attribute__((packed)) pcap_hdr_s {
	uint32_t magic_number;   /* magic number */
	uint16_t version_major;  /* major version number */
	uint16_t version_minor;  /* minor version number */
	int32_t  thiszone;       /* GMT to local correction */
	uint32_t sigfigs;        /* accuracy of timestamps */
	uint32_t snaplen;        /* max length of captured packets, in octets */
	uint32_t network;        /* data link type */
} pcap_hdr_t;

/* Nothing came of the file this call opened, so a file the call made itself
   does not outlive it. The reason it failed is kept for the caller to
   report. created is 0 for a name that was already in use, whatever is
   behind it, which this call did not make and must not remove.
   pcap_file is NULL where the stream was never made. */
static FILE *abandon_new_file( FILE *pcap_file, int fd, const char *filename,
			       int created )
{
	int saved_errno = errno;

	if (created) {
		pcapng_remove_own_file( fd, filename );
	}
	if (pcap_file != NULL) {
		(void) fclose( pcap_file );
	}
	else {
		(void) close( fd );
	}
	errno = saved_errno;
	return NULL;
}

/* Internal, although the name carries the library's public prefix: no header
   the install puts in include/ declares it, so nothing outside these sources
   can call it. */
static FILE *btbb_pcap_open(const char *filename, uint32_t dlt, uint32_t snaplen) {
	pcap_hdr_t pcap_header = {
		.magic_number = 0xa1b23c4d,
		.version_major = 2,
		.version_minor = 4,
		.thiszone = 0,
		.sigfigs = 0,
		.snaplen = snaplen,
		.network = dlt,
	};
	FILE *pcap_file;
	int fd, created = 1;

	/* Two opens rather than one, to learn whether the file is this call's
	   own: with O_EXCL a descriptor means the name was free and this call
	   made what is behind it, so it can be taken away again if the header
	   cannot be written. A name already in use is opened as it always
	   was, and what is there is not this call's to remove.

	   The mode is the 0666 that opening a stream for writing asks for,
	   so a capture file's permissions are what they have always been. */
	fd = open( filename, O_WRONLY|O_CREAT|O_EXCL,
		   S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH );
	if (fd == -1) {
		int open_errno = errno;

		if (open_errno != EEXIST) return NULL;
		/* O_CREAT again, because the name being in use does not mean
		   there is a file to open: a symbolic link whose target does
		   not exist is in use, and opening it through a stream made
		   that target. O_EXCL is what is dropped here. */
		fd = open( filename, O_WRONLY|O_CREAT|O_TRUNC,
			   S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH );
		if (fd == -1) return NULL;
		created = 0;
	}

	pcap_file = fdopen( fd, "wb" );
	if (pcap_file == NULL) {
		return abandon_new_file( NULL, fd, filename, created );
	}

	/* Write straight through, which has to be set before anything else
	   uses the stream. This is what lets a record that cannot be written
	   whole be taken back out below: with a buffer in the way, part of
	   the failed record is on disk and the rest is in the buffer, and
	   there is no way to discard a buffer without flushing it - which
	   would put those bytes in the middle of the file after the
	   truncation. Each record is flushed as it is written in any case, so
	   the cost is a second write per record, the header and the body no
	   longer being coalesced.

	   A stream that cannot be given up its buffer is no good for writing
	   records that have to be whole, so give up on it here rather than
	   write a capture that cannot be rolled back. */
	if (setvbuf(pcap_file, NULL, _IONBF, 0) != 0) {
		/* setvbuf() need not set errno, and errno is the whole of
		   what the caller has to report, so a stream whose buffer
		   cannot be given up is reported as the memory it is. */
		errno = ENOMEM;
		return abandon_new_file( pcap_file, fd, filename, created );
	}

	/* A file whose header did not reach it is no capture file, so say so
	   now rather than leaving the caller with a handle to it. */
	if (fwrite(&pcap_header, sizeof(pcap_header), 1, pcap_file) != 1) {
		return abandon_new_file( pcap_file, fd, filename, created );
	}

	return pcap_file;
}

/* BT BR/EDR support */

struct btbb_pcap_handle {
	FILE *pcap_file;
};

int 
btbb_pcap_create_file(const char *filename, btbb_pcap_handle ** ph)
{
	int retval = 0;
	btbb_pcap_handle * handle = malloc( sizeof(btbb_pcap_handle) );
	if (handle) {
		memset(handle, 0, sizeof(*handle));
		/* the longest record: the pseudoheader and a full payload */
		handle->pcap_file = btbb_pcap_open(filename, DLT_BLUETOOTH_BREDR_BB,
						   sizeof(pcap_bluetooth_bredr_bb_header));
		if (handle->pcap_file) {
			*ph = handle;
		}
		else {
			retval = -PCAP_FILE_NOT_ALLOWED;
			goto fail;
		}
	}
	else {
		retval = -PCAP_NO_MEMORY;
		goto fail;
	}
	return retval;
fail:
	{
		/* Why the file could not be opened is the caller's whole
		   report of it, and freeing the handle closes a stream,
		   which sets an errno of its own. */
		int saved_errno = errno;

		(void) btbb_pcap_close( handle );
		errno = saved_errno;
	}
	return retval;
}

typedef struct __attribute__((packed)) pcaprec_hdr_s {
	uint32_t ts_sec;         /* timestamp seconds */
	uint32_t ts_usec;        /* timestamp microseconds */
	uint32_t incl_len;       /* number of octets of packet saved in file */
	uint32_t orig_len;       /* actual length of packet */
} pcaprec_hdr_t;

typedef struct {
	pcaprec_hdr_t pcap_header;
	pcap_bluetooth_bredr_bb_header bredr_bb_header;
} pcap_bredr_packet;

/* Take a record that could not be written whole back out of the file, so the
   capture ends on the last complete record and a reader does not run into a
   record header whose length reaches past the end of the file.
   btbb_pcap_open() left the stream unbuffered, so what reached the file is
   all there was of the record.

   This is the best that can be done rather than a guarantee: a capture going
   to something whose position cannot be found, or that cannot be truncated
   or sought - a named pipe, say - keeps the part of the record that reached
   it, as every capture did before. Writing to such a thing goes on failing
   on its own, and the caller is told this record failed either way, so a
   caller that stops capturing there - the only sensible answer to a full
   disk - ends up with a file no worse than it would have had. */
static void discard_partial_record(FILE *file, off_t start_of_record)
{
	if (start_of_record < 0) {
		return;
	}
	if (ftruncate(fileno(file), start_of_record) != 0) {
		return;
	}
	/* Put the stream back on the end of the file, and only then make it
	   usable again. A seek that failed would leave the stream past the
	   new end of the file, where a further record would be written beyond
	   a hole; leaving the error set is what keeps that from happening
	   quietly. */
	if (fseeko(file, start_of_record, SEEK_SET) == 0) {
		clearerr(file);
	}
}

/* Write one record. Returns 0, or -PCAP_FILE_WRITE_ERROR if any of it did not
   reach the file, so a capture that runs out of disk space is reported to the
   caller instead of looking like a capture that simply ended. A record that
   could not be written whole is taken back out, as the pcapng writer does
   with a block, leaving the file parsable to its end. */
static int btbb_pcap_dump(FILE *file, pcaprec_hdr_t *pcap_header, u_char *data) {
	/* ftello() rather than ftell(), so the position is the off_t that
	   ftruncate() below takes and that the pcapng writer uses, which is
	   64 bits wide wherever large file support is on. */
	off_t start_of_record = ftello(file);

	if ((fwrite(pcap_header, sizeof(*pcap_header), 1, file) == 1) &&
	    (fwrite(data, pcap_header->incl_len, 1, file) == 1) &&
	    (fflush(file) == 0)) {
		return 0;
	}

	discard_partial_record(file, start_of_record);
	return -PCAP_FILE_WRITE_ERROR;
}

static void
assemble_pcapng_bredr_packet( pcap_bredr_packet * pkt,
			      const uint32_t interface_id __attribute__((unused)),
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
			      const uint8_t * payload )
{
	uint32_t pcap_caplen = sizeof(pcap_bluetooth_bredr_bb_header) -
				sizeof(pkt->bredr_bb_header.bredr_payload) 
				+ caplen;
	/* The cast keeps the shift out of a signed int, where a reference UAP
	   of 0x80 or above would overflow into the sign bit. */
	uint32_t reflapuap = (ref_lap&0xffffff) | ((uint32_t) ref_uap << 24);

	pkt->pcap_header.ts_sec  = ns / 1000000000ull;
	pkt->pcap_header.ts_usec = ns % 1000000000ull;
	pkt->pcap_header.incl_len = pcap_caplen;
	pkt->pcap_header.orig_len = pcap_caplen;

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
}

int 
btbb_pcap_append_packet(btbb_pcap_handle * h, const uint64_t ns, 
			const int8_t sigdbm, const int8_t noisedbm,
			const uint32_t reflap, const uint8_t refuap, 
			const btbb_packet *pkt)
{
	if (h && h->pcap_file) {
		uint16_t flags = bredr_record_flags( sigdbm, noisedbm, reflap, refuap, pkt );
		/* A NULL or POLL packet has no payload, so the length is 0 and
		   a buffer sized from it would be a zero-length array, which
		   C does not define. The record holds BREDR_MAX_PAYLOAD
		   bytes, and the decoder never produces more than a DH5's
		   343, so size the buffer for the record and leave a longer
		   payload unrecorded rather than overrun it. */
		uint8_t payload_bytes[BREDR_MAX_PAYLOAD];
		int payload_length = btbb_packet_get_payload_length(pkt);
		uint32_t caplen = 0;
		/* The format gives neither corrected-bits field a validity
		   flag, so a reader takes both at face value. The payload's
		   count describes the payload in the record, so it is only
		   recorded alongside one.

		   The count goes in as the decoder made it. The published
		   layout describes this field as running to +180 for the rate
		   2/3 code, which corrects one bit per 15-symbol block; the
		   longest payload that code carries is a DM5's 228 bytes,
		   which is 183 blocks, so a reception that needed a
		   correction in every block records 183. A reader is better
		   served by the real number than by one capped to a
		   description three short of the format's own definition, and
		   a signed 16-bit field holds it. */
		int16_t corrected_payload_bits = 0;
		pcap_bredr_packet pcap_pkt;

		/* Only a payload the decoder produced for this packet goes in
		   the record. A rejected candidate clock from the search for
		   a piconet's address part leaves a payload behind without
		   that flag. */
		if (btbb_packet_get_flag(pkt, BTBB_HAS_PAYLOAD) &&
		    (payload_length > 0) &&
		    (payload_length <= MAX_PAYLOAD_LENGTH / 8)) {
			caplen = (uint32_t) btbb_get_payload_packed( pkt, (char *) &payload_bytes[0] );
			corrected_payload_bits = pkt->corrected_payload_bits;
		}

		assemble_pcapng_bredr_packet( &pcap_pkt,
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
		return btbb_pcap_dump(h->pcap_file, &pcap_pkt.pcap_header,
				      (u_char *)&pcap_pkt.bredr_bb_header);
	}
	return -PCAP_INVALID_HANDLE;
}

int 
btbb_pcap_close(btbb_pcap_handle * h)
{
	int retval = 0;

	/* The stream is unbuffered, so every record has already reached the
	   file or been reported as not reaching it; this catches a failure of
	   the close itself. */
	if (h && h->pcap_file && (fclose(h->pcap_file) != 0)) {
		retval = -PCAP_FILE_WRITE_ERROR;
	}
	if (h) {
		free(h);
		return retval;
	}
	return -PCAP_INVALID_HANDLE;
}

/* BTLE support */

struct lell_pcap_handle {
	FILE *pcap_file;
	int dlt;
	uint8_t btle_ppi_version;
};

#define PPI_BTLE 30006

typedef struct __attribute__((packed)) {
	uint8_t pph_version;
	uint8_t pph_flags;
	uint16_t pph_len;
	uint32_t pph_dlt;
} ppi_packet_header_t;

typedef struct __attribute__((packed)) {
	uint16_t pfh_type;
	uint16_t pfh_datalen;
} ppi_fieldheader_t;

typedef struct __attribute__((packed)) {
	uint8_t btle_version;
	uint16_t btle_channel;
	uint8_t btle_clkn_high;
	uint32_t btle_clk100ns;
	int8_t rssi_max;
	int8_t rssi_min;
	int8_t rssi_avg;
	uint8_t rssi_count;
} ppi_btle_t;

static int
lell_pcap_create_file_dlt(const char *filename, int dlt, lell_pcap_handle ** ph)
{
	int retval = 0;
	lell_pcap_handle * handle = malloc( sizeof(lell_pcap_handle) );
	if (handle) {
		/* The snapshot length is the longest record this link type
		   writes, so a reader sizing a buffer from it has room for
		   every record in the file. */
		uint32_t snaplen = (dlt == DLT_PPI)
			? (uint32_t) (sizeof(ppi_packet_header_t) +
				      sizeof(ppi_fieldheader_t) +
				      sizeof(ppi_btle_t) + LE_MAX_PAYLOAD)
			: (uint32_t) (sizeof(pcap_bluetooth_le_ll_header) +
				      LE_MAX_PAYLOAD);

		memset(handle, 0, sizeof(*handle));
		handle->pcap_file = btbb_pcap_open(filename, dlt, snaplen);
		if (handle->pcap_file) {
			handle->dlt = dlt;
			*ph = handle;
		}
		else {
			retval = -PCAP_FILE_NOT_ALLOWED;
			goto fail;
		}
	}
	else {
		retval = -PCAP_NO_MEMORY;
		goto fail;
	}
	return retval;
fail:
	{
		/* Why the file could not be opened is the caller's whole
		   report of it, and freeing the handle closes a stream,
		   which sets an errno of its own. */
		int saved_errno = errno;

		(void) lell_pcap_close( handle );
		errno = saved_errno;
	}
	return retval;
}

int 
lell_pcap_create_file(const char *filename, lell_pcap_handle ** ph)
{
	return lell_pcap_create_file_dlt(filename, DLT_BLUETOOTH_LE_LL_WITH_PHDR, ph);
}

int 
lell_pcap_ppi_create_file(const char *filename, int btle_ppi_version, 
			  lell_pcap_handle ** ph)
{
	int retval = lell_pcap_create_file_dlt(filename, DLT_PPI, ph);
	if (!retval) {
		(*ph)->btle_ppi_version = btle_ppi_version;
	}
	return retval;
}

typedef struct {
	pcaprec_hdr_t pcap_header;
	pcap_bluetooth_le_ll_header le_ll_header;
	uint8_t le_packet[LE_MAX_PAYLOAD];
} pcap_le_packet;

static void
assemble_pcapng_le_packet( pcap_le_packet * pkt,
			   const uint32_t interface_id __attribute__((unused)),
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
	uint32_t incl_len = MIN(LE_MAX_PAYLOAD, caplen);

	pkt->pcap_header.ts_sec  = ns / 1000000000ull;
	pkt->pcap_header.ts_usec = ns % 1000000000ull;
	pkt->pcap_header.incl_len = sizeof(pcap_bluetooth_le_ll_header)+incl_len;
	pkt->pcap_header.orig_len = sizeof(pcap_bluetooth_le_ll_header)+caplen;

	pkt->le_ll_header.rf_channel = rf_channel;
	pkt->le_ll_header.signal_power = signal_power;
	pkt->le_ll_header.noise_power = noise_power;
	pkt->le_ll_header.access_address_offenses = access_address_offenses;
	pkt->le_ll_header.ref_access_address = htole32( ref_access_address );
	pkt->le_ll_header.flags = htole16( flags );
	(void) memcpy( &pkt->le_packet[0], lepkt, incl_len );
}

int 
lell_pcap_append_packet(lell_pcap_handle * h, const uint64_t ns,
			const int8_t sigdbm, const int8_t noisedbm,
			const uint32_t refAA, const lell_packet *pkt)
{
	if (h && h->pcap_file &&
	    (h->dlt == DLT_BLUETOOTH_LE_LL_WITH_PHDR)) {
		uint16_t flags = LE_DEWHITENED | LE_AA_OFFENSES_VALID |
			LE_SIGPOWER_VALID |
			((noisedbm < sigdbm) ? LE_NOISEPOWER_VALID : 0) |
			LE_REF_AA_FLAG(refAA);
		pcap_le_packet pcap_pkt;
		assemble_pcapng_le_packet( &pcap_pkt,
					   0,
					   ns,
					   pkt->length + LE_FRAMING_BYTES,
					   pkt->channel_k,
					   sigdbm,
					   noisedbm,
					   pkt->access_address_offenses,
					   refAA,
					   flags,
					   &pkt->symbols[0] );
		return btbb_pcap_dump(h->pcap_file, &pcap_pkt.pcap_header,
				      (u_char *)&pcap_pkt.le_ll_header);
	}
	return -PCAP_INVALID_HANDLE;
}

typedef struct __attribute__((packed)) {
	pcaprec_hdr_t pcap_header;
        ppi_packet_header_t ppi_packet_header;
	ppi_fieldheader_t ppi_fieldheader;
	ppi_btle_t le_ll_ppi_header;
	uint8_t le_packet[LE_MAX_PAYLOAD];
} pcap_ppi_le_packet;

int 
lell_pcap_append_ppi_packet(lell_pcap_handle * h, const uint64_t ns,
			    const uint8_t clkn_high,
			    const int8_t rssi_min, const int8_t rssi_max,
			    const int8_t rssi_avg, const uint8_t rssi_count,
			    const lell_packet *pkt)
{
	if (h && h->pcap_file &&
	    (h->dlt == DLT_PPI)) {
		pcap_ppi_le_packet pcap_pkt;
		const uint16_t pcap_headerlen =
			sizeof(ppi_packet_header_t) +
			sizeof(ppi_fieldheader_t) +
			sizeof(ppi_btle_t);
		uint16_t MHz = 2402 + 2*lell_get_channel_k(pkt);
		unsigned packet_len = pkt->length + LE_FRAMING_BYTES;
		unsigned incl_len   = MIN(LE_MAX_PAYLOAD, packet_len);

		pcap_pkt.pcap_header.ts_sec  = ns / 1000000000ull;
		pcap_pkt.pcap_header.ts_usec = ns % 1000000000ull;
		pcap_pkt.pcap_header.incl_len = pcap_headerlen + incl_len;
		pcap_pkt.pcap_header.orig_len = pcap_headerlen + packet_len;

		pcap_pkt.ppi_packet_header.pph_version = 0;
		pcap_pkt.ppi_packet_header.pph_flags = 0;
		pcap_pkt.ppi_packet_header.pph_len = htole16(pcap_headerlen);
		pcap_pkt.ppi_packet_header.pph_dlt = htole32(DLT_BLUETOOTH_LE_LL);

		pcap_pkt.ppi_fieldheader.pfh_type = htole16(PPI_BTLE);
		pcap_pkt.ppi_fieldheader.pfh_datalen = htole16(sizeof(ppi_btle_t));
	
		pcap_pkt.le_ll_ppi_header.btle_version = h->btle_ppi_version;
		pcap_pkt.le_ll_ppi_header.btle_channel = htole16(MHz);
		pcap_pkt.le_ll_ppi_header.btle_clkn_high = clkn_high;
		pcap_pkt.le_ll_ppi_header.btle_clk100ns = htole32(pkt->clk100ns);
		pcap_pkt.le_ll_ppi_header.rssi_max = rssi_max;
		pcap_pkt.le_ll_ppi_header.rssi_min = rssi_min;
		pcap_pkt.le_ll_ppi_header.rssi_avg = rssi_avg;
		pcap_pkt.le_ll_ppi_header.rssi_count = rssi_count;
		(void) memcpy( &pcap_pkt.le_packet[0], &pkt->symbols[0], incl_len);
		return btbb_pcap_dump(h->pcap_file, &pcap_pkt.pcap_header,
				      (u_char *)&pcap_pkt.ppi_packet_header);
	}
	return -PCAP_INVALID_HANDLE;
}

int 
lell_pcap_close(lell_pcap_handle *h)
{
	int retval = 0;

	/* The stream is unbuffered, so every record has already reached the
	   file or been reported as not reaching it; this catches a failure of
	   the close itself. */
	if (h && h->pcap_file && (fclose(h->pcap_file) != 0)) {
		retval = -PCAP_FILE_WRITE_ERROR;
	}
	if (h) {
		free(h);
		return retval;
	}
	return -PCAP_INVALID_HANDLE;
}
