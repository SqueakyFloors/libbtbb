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

#include "pcapng.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* The option code used for padding. The format defines no padding option, so
   this is a code of this library's own choosing; a reader steps over an
   option it does not know by the length that follows the code. */
#define PCAPNG_OPTION_PADDING 0xffff

/* An option's length field is 16 bits wide, so one option can cover at most
   65532 bytes of value, the largest multiple of 4 that fits. A block with
   more spare room than that needs a second padding option; a 64 kB page
   gives one that big. */
#define MAX_OPTION_VALUE_LENGTH 0xfffc

/* Fill the spare room at the end of a block with padding options, so that a
   reader walks over it as options rather than reading it as data.

   block is the start of the block, block_size its total length, and used the
   offset one past the last option written so far. The last 8 bytes are left
   as they are: 4 for the end-of-options marker, which is an all-zero word,
   and 4 for the block's trailing length. */
static void write_padding_options( void * block,
				   const size_t block_size,
				   const size_t used )
{
	uint8_t * dest = &((uint8_t *) block)[used];
	size_t remaining;

	if (block_size < used + 8) {
		return;
	}
	remaining = block_size - used - 8;

	while (remaining >= sizeof( option_header )) {
		size_t value_length = remaining - sizeof( option_header );
		option_header padopt;

		if (value_length > MAX_OPTION_VALUE_LENGTH) {
			value_length = MAX_OPTION_VALUE_LENGTH;
		}
		padopt.option_code = PCAPNG_OPTION_PADDING;
		padopt.option_length = (uint16_t) value_length;
		(void) memcpy( dest, &padopt, sizeof( padopt ) );

		dest += sizeof( option_header ) + value_length;
		remaining -= sizeof( option_header ) + value_length;
	}
}

PCAPNG_RESULT pcapng_create( PCAPNG_HANDLE * handle,
			     const char * filename,
			     const option_header * section_options,
			     const size_t section_options_space,
			     const uint16_t link_type,
			     const uint32_t snaplen,
			     const option_header * interface_options,
			     const size_t interface_options_space )
{
	PCAPNG_RESULT retval = PCAPNG_OK;
	int PGSZ = getpagesize( );
	size_t zeroes = 0;
	ssize_t result = -1;

	handle->section_header = NULL;
	handle->interface_description = NULL;
	handle->section_header_size = handle->next_section_option_offset =
		handle->interface_description_size =
		handle->next_interface_option_offset = 0;

	handle->fd = open( filename, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
	if (handle->fd == -1) {
		switch( errno ) {
		case EEXIST:
			retval = PCAPNG_FILE_EXISTS;
			break;
		case EMFILE:
		case ENFILE:
			retval = PCAPNG_TOO_MANY_FILES_OPEN;
			break;
		case ENOMEM:
		case ENOSPC:
			retval = PCAPNG_NO_MEMORY;
			break;
		default:
			retval = PCAPNG_FILE_NOT_ALLOWED;
		}
	}

	if (retval == PCAPNG_OK) {
		/* section header */
		const section_header_block shb = {
			.block_type = BLOCK_TYPE_SECTION_HEADER,
			.block_total_length = 28,
			.byte_order_magic = SECTION_HEADER_BYTE_ORDER_MAGIC,
			.major_version = 1,
			.minor_version = 0,
			.section_length = (uint64_t) -1,
		};
		handle->section_header_size = sizeof( shb );
		result = write( handle->fd, &shb, sizeof( shb ) );
		/* write initial section options */
		while ((result != -1) &&
		       section_options &&
		       section_options->option_code &&
		       section_options->option_length) {
			size_t paddedsz = 4*((section_options->option_length+3)/4);
			zeroes = paddedsz - section_options->option_length;
			result = write( handle->fd, section_options, 4+section_options->option_length );
			while ((zeroes > 0) && (result != -1)) {
				result = write( handle->fd, "\0", 1 );
				zeroes--;
			}
			section_options = (const option_header *) &((uint8_t *)section_options)[4+paddedsz];
			handle->section_header_size += (4+paddedsz);
		}
		handle->next_section_option_offset = handle->section_header_size;
	}

	if (result == -1) {
		retval = PCAPNG_FILE_WRITE_ERROR;
	}
	else {
		/* determine the size of section header with desired space */
		zeroes = (size_t) PGSZ*((handle->section_header_size + 4 +
					 section_options_space + PGSZ - 1)/PGSZ) -
			handle->section_header_size;
		handle->section_header_size += zeroes;
		while ((zeroes > 0) && (result != -1)) {
			result = write( handle->fd, "\0", 1 );
			zeroes--;
		}

		/* mmap the section header */
		handle->section_header = mmap( NULL, handle->section_header_size, 
					       PROT_READ|PROT_WRITE,
					       MAP_SHARED,
					       handle->fd, 0 );
	}

	if (retval == PCAPNG_OK) {
		if (result == -1) {
			retval = PCAPNG_FILE_WRITE_ERROR;
		}
		else if (handle->section_header == MAP_FAILED) {
			retval = PCAPNG_MMAP_FAILED;
		}
		else {
			/* write the interface header */
			const interface_description_block idb = {
				.block_type = BLOCK_TYPE_INTERFACE,
				.block_total_length = 0,
				.link_type = link_type,
				.snaplen = snaplen
			};
			handle->interface_description_size = sizeof( idb );
			result = write( handle->fd, &idb, sizeof( idb ) );

			/* write interface options */
			while ((result != -1) &&
			       interface_options &&
			       interface_options->option_code &&
			       interface_options->option_length) {
				size_t paddedsz = 4*((interface_options->option_length+3)/4);
				zeroes = paddedsz - interface_options->option_length;
				result = write( handle->fd, interface_options, 4+interface_options->option_length );
				while ((zeroes > 0) && (result != -1)) {
					result = write( handle->fd, "\0", 1 );
					zeroes--;
				}
				interface_options = (const option_header *) &((uint8_t *)interface_options)[4+paddedsz];
				handle->interface_description_size += (4+paddedsz);
			}
			handle->next_interface_option_offset = handle->interface_description_size;
		}
	}

	if (retval == PCAPNG_OK) {
		if (result == -1) {
			retval = PCAPNG_FILE_WRITE_ERROR;
		}
		else {
			/* determine the size of interface description with desired space */
			zeroes = (size_t) PGSZ*((handle->interface_description_size + 4 +
						 interface_options_space + PGSZ - 1)/PGSZ) -
				handle->interface_description_size;
			handle->interface_description_size += zeroes;
			while ((zeroes > 0) && (result != -1)) {
				result = write( handle->fd, "\0", 1 );
				zeroes--;
			}

			/* mmap the interface description */
			handle->interface_description = mmap( NULL, handle->interface_description_size, 
							      PROT_READ|PROT_WRITE,
							      MAP_SHARED,
							      handle->fd,
							      handle->section_header_size );
		}
	}

	if (retval == PCAPNG_OK) {
		if (result == -1) {
			retval = PCAPNG_FILE_WRITE_ERROR;
		}
		else if (handle->interface_description == MAP_FAILED) {
			retval = PCAPNG_MMAP_FAILED;
		}
		else {
			/* Add padding options, update the header sizes. */
			write_padding_options( handle->section_header,
					       handle->section_header_size,
					       handle->next_section_option_offset );
			handle->section_header->block_total_length =
				(uint32_t) handle->section_header_size;
			((uint32_t*)handle->section_header)[handle->section_header_size/4-1] =
				(uint32_t) handle->section_header_size;

			write_padding_options( handle->interface_description,
					       handle->interface_description_size,
					       handle->next_interface_option_offset );
			handle->interface_description->block_total_length =
				(uint32_t) handle->interface_description_size;
			((uint32_t*)handle->interface_description)[handle->interface_description_size/4-1] =
				(uint32_t) handle->interface_description_size;

			handle->section_header->section_length = (uint64_t) handle->interface_description_size;
		}
	}

	if (retval != PCAPNG_OK) {
		(void) pcapng_close( handle );
	}

	return retval;
}

PCAPNG_RESULT pcapng_append_section_option( PCAPNG_HANDLE * handle,
					    const option_header * section_option )
{
	PCAPNG_RESULT retval = PCAPNG_OK;
	if (handle && (handle->fd != -1)) {
		if (handle->section_header &&
		    (handle->section_header != MAP_FAILED) &&
		    handle->next_section_option_offset &&
		    section_option) {
			size_t copysz = 4+section_option->option_length;
			size_t paddedsz = 4*((copysz+3)/4);
			uint8_t * dest = &((uint8_t *)handle->section_header)[handle->next_section_option_offset];

			/* The option, the end-of-options marker and the
			   trailing block length must all still fit in the
			   space set aside when the file was created. */
			if (handle->next_section_option_offset + paddedsz + 8 >
			    handle->section_header_size) {
				retval = PCAPNG_NO_MEMORY;
			}
			else {
				(void) memcpy( dest, section_option, copysz );
				handle->next_section_option_offset += paddedsz;

				/* update padding option */
				write_padding_options( handle->section_header,
						       handle->section_header_size,
						       handle->next_section_option_offset );
			}
		}
		else {
			retval = PCAPNG_NO_MEMORY;
		}
	}
	else {
		retval = PCAPNG_INVALID_HANDLE;
	}
	return retval;
}

PCAPNG_RESULT pcapng_append_interface_option( PCAPNG_HANDLE * handle,
					      const option_header * interface_option )
{
	PCAPNG_RESULT retval = PCAPNG_OK;
	if (handle && (handle->fd != -1)) {
		if (handle->interface_description &&
		    (handle->interface_description != MAP_FAILED) &&
		    handle->next_interface_option_offset &&
		    interface_option) {
			size_t copysz = 4+interface_option->option_length;
			size_t paddedsz = 4*((copysz+3)/4);
			uint8_t * dest = &((uint8_t *)handle->interface_description)[handle->next_interface_option_offset];

			/* The option, the end-of-options marker and the
			   trailing block length must all still fit in the
			   space set aside when the file was created. */
			if (handle->next_interface_option_offset + paddedsz + 8 >
			    handle->interface_description_size) {
				retval = PCAPNG_NO_MEMORY;
			}
			else {
				(void) memcpy( dest, interface_option, copysz );
				handle->next_interface_option_offset += paddedsz;

				/* update padding option */
				write_padding_options( handle->interface_description,
						       handle->interface_description_size,
						       handle->next_interface_option_offset );
			}
		}
		else {
			retval = PCAPNG_NO_MEMORY;
		}
	}
	else {
		retval = PCAPNG_INVALID_HANDLE;
	}
	return retval;
}

PCAPNG_RESULT pcapng_append_packet( PCAPNG_HANDLE * handle,
				    const enhanced_packet_block * packet )
{
	PCAPNG_RESULT retval = PCAPNG_OK;
	if (handle && (handle->fd != -1) &&
	    handle->section_header && (handle->section_header != MAP_FAILED)) {
		size_t writesz = packet->block_total_length;
		off_t before = lseek( handle->fd, 0, SEEK_CUR );
		ssize_t result = write( handle->fd, packet, writesz );

		if ((result != -1) && ((size_t) result == writesz)) {
			handle->section_header->section_length += writesz;
		}
		else {
			/* A short write leaves part of a block in the file,
			   which no reader can make sense of, so take it back
			   out and leave the capture ending on the last whole
			   block. The caller is told either way, so it can
			   stop capturing rather than keep trying. */
			if ((result > 0) && (before != (off_t) -1) &&
			    (ftruncate( handle->fd, before ) == 0)) {
				(void) lseek( handle->fd, before, SEEK_SET );
			}
			retval = PCAPNG_FILE_WRITE_ERROR;
		}
	}
	else {
		retval = PCAPNG_INVALID_HANDLE;
	}
	return retval;
}

PCAPNG_RESULT pcapng_close( PCAPNG_HANDLE * handle )
{
	if (handle->interface_description &&
	    (handle->interface_description != MAP_FAILED)) {
		(void) munmap( handle->interface_description,
			       handle->interface_description_size );
	}
	if (handle->section_header &&
	    (handle->section_header != MAP_FAILED)) {
		(void) munmap( handle->section_header,
			       handle->section_header_size );
	}
	if (handle->fd != -1) {
		(void) close( handle->fd );
	}
	return PCAPNG_OK;
}
