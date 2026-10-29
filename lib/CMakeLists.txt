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

# Where the library, the header and libbtbb.pc are installed, under
# CMAKE_INSTALL_PREFIX. GNUInstallDirs works out the directory this system
# keeps libraries in: lib on macOS and under /usr/local, lib64 on a 64-bit
# Fedora, RHEL or SUSE, and lib/<triplet> where the prefix is /usr on a
# multiarch Debian or Ubuntu. LIB_SUFFIX, the older way of asking for the
# same thing, still decides where it is set, so a build command that passes
# it installs where it always did; it names the library's directory only, and
# the header follows CMAKE_INSTALL_INCLUDEDIR either way.
if(DEFINED LIB_SUFFIX)
	set(BTBB_INSTALL_LIBDIR lib${LIB_SUFFIX})
else()
	set(BTBB_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()
set(BTBB_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
message(STATUS
	"Installing the library to ${BTBB_INSTALL_LIBDIR} and the header to "
	"${BTBB_INSTALL_INCLUDEDIR}, under ${CMAKE_INSTALL_PREFIX}")

add_subdirectory(src)

# The directories libbtbb.pc names have to be the ones the install writes to:
# a consumer whose pkg-config -L names a directory holding no library links
# by luck or not at all.
#
# A directory under the prefix is named relative to it, so that the file
# works wherever the install ends up: the prefix is read back out of this
# file's own location at the top of libbtbb.pc.in. The way back out of the
# pkgconfig directory is one ".." per directory, because the libdir can be a
# path of its own - lib/x86_64-linux-gnu, which is how the Debian package
# builds this library - rather than a single name.
#
# A libdir or includedir given as an absolute path is installed to exactly
# that, wherever the prefix is, so libbtbb.pc names it as it stands and takes
# the configured prefix for the rest.
string(REGEX REPLACE "[^/]+" ".." pc_prefix_up
	"${BTBB_INSTALL_LIBDIR}/pkgconfig")
set(PC_PREFIX "\${pcfiledir}/${pc_prefix_up}")
if(IS_ABSOLUTE "${BTBB_INSTALL_LIBDIR}")
	set(PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
	set(PC_LIBDIR "${BTBB_INSTALL_LIBDIR}")
	# Such an install cannot be staged somewhere else and still be found,
	# which is worth saying while the directories are being chosen: the file
	# lands outside the prefix, so it has no way to tell that the prefix has
	# moved, and the header it names would be the one at the old place.
	message(STATUS
		"libbtbb.pc will name ${CMAKE_INSTALL_PREFIX} as the prefix, "
		"because an absolute library directory has no relation to it: "
		"an install staged elsewhere with --prefix needs its own configure")
else()
	set(PC_LIBDIR "\${prefix}/${BTBB_INSTALL_LIBDIR}")
endif()
if(IS_ABSOLUTE "${BTBB_INSTALL_INCLUDEDIR}")
	set(PC_INCLUDEDIR "${BTBB_INSTALL_INCLUDEDIR}")
else()
	set(PC_INCLUDEDIR "\${prefix}/${BTBB_INSTALL_INCLUDEDIR}")
endif()

CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/libbtbb.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libbtbb.pc
@ONLY)

INSTALL(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libbtbb.pc
    DESTINATION ${BTBB_INSTALL_LIBDIR}/pkgconfig
)
