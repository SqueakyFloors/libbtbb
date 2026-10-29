#
# Copyright 2013 Dominic Spill
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

include(GNUInstallDirs)

# Based heavily upon the hackrf cmake setup.

project(libbtbb C)
set(MAJOR_VERSION 1)
set(MINOR_VERSION 2)
set(PACKAGE libbtbb)
set(VERSION ${MAJOR_VERSION}.${MINOR_VERSION})
add_definitions( -DVERSION="${VERSION}" )

if(NOT MSVC)
	add_definitions(-Wall)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu90")
endif()

add_subdirectory(src)

# The directories libbtbb.pc names have to be the ones the install writes to,
# which are lib${LIB_SUFFIX} and include. GNUInstallDirs answers something
# else on some systems - lib/x86_64-linux-gnu where the prefix is /usr on a
# multiarch distribution, lib64 on others - and a consumer whose pkg-config
# -L names a directory holding no library links by luck or not at all.
set(PC_LIBDIR lib${LIB_SUFFIX})
set(PC_INCLUDEDIR include)

# The way back out of the pkgconfig directory to the prefix, one ".." per
# directory, because LIB_SUFFIX can carry a path rather than a suffix: the
# Debian package builds with -DLIB_SUFFIX=/x86_64-linux-gnu, which installs
# into lib/x86_64-linux-gnu/pkgconfig, three directories down.
string(REGEX REPLACE "[^/]+" ".." PC_PREFIX_UP "${PC_LIBDIR}/pkgconfig")

# Where the install puts a library is LIB_SUFFIX's business here, not
# GNUInstallDirs': changing that is a packaging decision rather than a fix,
# and #88 is where it is being decided.
# A build whose CMAKE_INSTALL_LIBDIR says somewhere else is told, because
# otherwise the setting looks as though it worked. It is a note rather than a
# warning: a 64-bit Fedora, RHEL or SUSE build sets that variable to lib64 of
# its own accord, so a plain build there would be answered with what reads as
# a fault when nothing is wrong.
if(NOT "${CMAKE_INSTALL_LIBDIR}" STREQUAL "${PC_LIBDIR}")
	message(STATUS
		"Installing the library to ${PC_LIBDIR}, not the "
		"${CMAKE_INSTALL_LIBDIR} of CMAKE_INSTALL_LIBDIR: "
		"-DLIB_SUFFIX=64 asks for lib64, and -DLIB_SUFFIX=/<triplet> "
		"for a multiarch directory")
endif()

CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/libbtbb.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libbtbb.pc
@ONLY)

INSTALL(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libbtbb.pc
    DESTINATION lib${LIB_SUFFIX}/pkgconfig
)
