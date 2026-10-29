#
# This file is part of Libbtbb.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.
#
# Based slightly upon the hackrf cmake setup.

include(${PROJECT_SOURCE_DIR}/../cmake/set_release.cmake)
add_definitions( -DRELEASE="${RELEASE_STRING}" )
message(STATUS "Setting version string ${RELEASE_STRING}")

# Source
set(c_sources ${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_packet.c
              ${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_piconet.c
              ${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_le_packet.c
              ${CMAKE_CURRENT_SOURCE_DIR}/companies.c
              ${CMAKE_CURRENT_SOURCE_DIR}/pcap.c
              ${CMAKE_CURRENT_SOURCE_DIR}/pcapng.c
              ${CMAKE_CURRENT_SOURCE_DIR}/pcapng-bt.c
			  CACHE INTERNAL "List of C sources")
set(c_headers ${CMAKE_CURRENT_SOURCE_DIR}/btbb.h
			  CACHE INTERNAL "List of C headers")

# A configuration that builds neither library leaves nothing for anything to
# link against, so it is refused here rather than at the link of the first
# program. The two options are BUILD_SHARED_LIB and BUILD_STATIC_LIB.
if( NOT BUILD_SHARED_LIB AND NOT BUILD_STATIC_LIB )
	message( FATAL_ERROR "Not building static or shared library - at least one must be built: set BUILD_SHARED_LIB or BUILD_STATIC_LIB to ON")
endif()

set(CMAKE_MACOSX_RPATH 1)

# Nothing is exported unless btbb.h says so. The sources are compiled with
# -fvisibility=hidden, which leaves every function internal, and the BTBB_API
# macro on each declaration in btbb.h gives that one function default
# visibility again. So the library's helpers - hop(), precalc(),
# pcapng_create() and the rest - are no longer names a program can link
# against and come to depend on, and a program that calls a function btbb.h
# declares is unaffected. The abi_surface test compares the two sets.
set(CMAKE_C_VISIBILITY_PRESET hidden)

if( BUILD_SHARED_LIB )
    # Shared library
    message(STATUS "Building shared library")
    add_library(btbb SHARED ${c_sources})
    # SOVERSION is the number a program records when it links this library,
    # so it stays at 1 for as long as a program that uses the library through
    # btbb.h keeps working: functions and constants may be added to the
    # header, but one that is already there cannot change its signature or go
    # away, and neither can pkt_queue, the one type the header defines rather
    # than leaving opaque. Anything else moves it to 2, and every program
    # linking libbtbb then has to be rebuilt. MINOR_VERSION carries the
    # additions; the abi_surface test holds down which symbols are exported,
    # and the changelog records what was compared with 2020-12-R1.
    set_target_properties(btbb PROPERTIES VERSION ${MAJOR_VERSION}.${MINOR_VERSION} SOVERSION 1)
    set_target_properties(btbb PROPERTIES CLEAN_DIRECT_OUTPUT 1)
    install(TARGETS btbb
            LIBRARY DESTINATION lib${LIB_SUFFIX}
            COMPONENT sharedlibs)
endif()

if( BUILD_STATIC_LIB )
    # Static library
    message(STATUS "Building static library")
    add_library(btbb-static STATIC ${c_sources})
    set_target_properties(btbb-static PROPERTIES OUTPUT_NAME "btbb")
    set_target_properties(btbb-static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
    install(TARGETS btbb-static
            DESTINATION lib${LIB_SUFFIX}
            COMPONENT staticlibs)
endif()

install(FILES ${c_headers}
        DESTINATION include
        COMPONENT headers
        )
