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
# CMAKE_INSTALL_PREFIX unless one of them is an absolute path. GNUInstallDirs
# works out the directory this system keeps libraries in: lib/<triplet> where
# the prefix is /usr on a multiarch Debian or Ubuntu, lib64 on most other
# 64-bit Linux systems, and lib on macOS, on Arch and Alpine, and for a conda
# prefix. CMAKE_INSTALL_LIBDIR and CMAKE_INSTALL_INCLUDEDIR choose them.
#
# LIB_SUFFIX, the older way of asking for the same thing, still decides where
# it is set to something, so a build command that passes it installs where it
# always did. It names the library's directory only, and the header follows
# CMAKE_INSTALL_INCLUDEDIR either way. An empty LIB_SUFFIX asks for no
# suffix, which says nothing about which directory is wanted, and leaves
# CMAKE_INSTALL_LIBDIR in charge rather than quietly turning it into lib.
if(LIB_SUFFIX)
	set(BTBB_INSTALL_LIBDIR lib${LIB_SUFFIX})
else()
	set(BTBB_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()
set(BTBB_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
# A trailing slash would be carried into libbtbb.pc and into the ".." walk
# back out of the pkgconfig directory below, where it reads as a directory of
# its own.
string(REGEX REPLACE "/+$" "" BTBB_INSTALL_LIBDIR "${BTBB_INSTALL_LIBDIR}")
string(REGEX REPLACE "/+$" "" BTBB_INSTALL_INCLUDEDIR "${BTBB_INSTALL_INCLUDEDIR}")

# What this build will install where, said once: a packager who set one of
# these reads it back, and a plain build on a lib64 system sees why the
# library is not going where the last one did.
set(btbb_under " under ${CMAKE_INSTALL_PREFIX}")
if(IS_ABSOLUTE "${BTBB_INSTALL_LIBDIR}" OR IS_ABSOLUTE "${BTBB_INSTALL_INCLUDEDIR}")
	set(btbb_under "")
endif()
message(STATUS
	"Installing the library to ${BTBB_INSTALL_LIBDIR} and the header to "
	"${BTBB_INSTALL_INCLUDEDIR}${btbb_under}")
# Where LIB_SUFFIX overrides a CMAKE_INSTALL_LIBDIR that asks for somewhere
# else, which of the two was followed is worth saying: a build command can
# carry both, one of them from the system rather than from its author.
if(LIB_SUFFIX AND NOT "${CMAKE_INSTALL_LIBDIR}" STREQUAL "${BTBB_INSTALL_LIBDIR}")
	message(STATUS
		"LIB_SUFFIX asks for ${BTBB_INSTALL_LIBDIR}, which is used rather "
		"than the ${CMAKE_INSTALL_LIBDIR} of CMAKE_INSTALL_LIBDIR")
endif()

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
