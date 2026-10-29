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
set(MINOR_VERSION 3)
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
# Reduce a directory the build was given to the form the install writes to,
# which is also the form libbtbb.pc can name: slashes that repeat or trail
# gone, "." segments dropped as the install drops them, and "." on its own
# for the prefix itself, which is what an empty setting asks for. Writes the
# result to out_var, and the number of directories it is deep to depth_var;
# an absolute directory is not under the prefix and its depth is 0.
#
# A ".." segment is refused rather than cleaned: the files would land outside
# the prefix, and nothing here could tell a program where they went.
function(btbb_clean_install_dir out_var depth_var dir what)
	# The slashes an absolute path starts with are kept as they are: two of
	# them mean something of their own on Windows and in POSIX, where the
	# rest of a run of slashes does not.
	set(absolute FALSE)
	set(lead "")
	if(IS_ABSOLUTE "${dir}")
		set(absolute TRUE)
		string(REGEX MATCH "^/+" lead "${dir}")
		string(REGEX REPLACE "^/+" "" dir "${dir}")
	endif()
	string(REGEX REPLACE "/+" "/" dir "${dir}")
	string(REPLACE "/" ";" segments "${dir}")
	set(kept "")
	foreach(segment IN LISTS segments)
		if(segment STREQUAL "" OR segment STREQUAL ".")
			continue()
		endif()
		if(segment STREQUAL "..")
			# One directory back, as the install reads it. Only a ".." with
			# nothing before it to go back through leads out.
			if(kept)
				list(REMOVE_AT kept -1)
				continue()
			endif()
			message(FATAL_ERROR
				"${what} is \"${dir}\", which leads out of the directory it "
				"is given relative to. Give a directory under the prefix, "
				"or an absolute path.")
		endif()
		list(APPEND kept "${segment}")
	endforeach()
	string(REPLACE ";" "/" cleaned "${kept}")
	if(absolute)
		set(${out_var} "${lead}${cleaned}" PARENT_SCOPE)
		set(${depth_var} 0 PARENT_SCOPE)
		return()
	endif()
	if(NOT kept)
		# The prefix itself. "." rather than "", which install() would read
		# as the root of the filesystem once a subdirectory was added to it.
		set(${out_var} "." PARENT_SCOPE)
		set(${depth_var} 0 PARENT_SCOPE)
		return()
	endif()
	list(LENGTH kept depth)
	set(${out_var} "${cleaned}" PARENT_SCOPE)
	set(${depth_var} ${depth} PARENT_SCOPE)
endfunction()

# LIB_SUFFIX is tested for being set to something rather than for being true,
# so that a suffix CMake reads as a false constant - "0", "N", "NO" - is
# still a suffix.
set(btbb_libdir_from_suffix FALSE)
if(DEFINED LIB_SUFFIX AND NOT "${LIB_SUFFIX}" STREQUAL "")
	set(btbb_libdir_from_suffix TRUE)
endif()
if(btbb_libdir_from_suffix)
	btbb_clean_install_dir(BTBB_INSTALL_LIBDIR btbb_libdir_depth
		"lib${LIB_SUFFIX}" "The library directory LIB_SUFFIX asks for")
else()
	btbb_clean_install_dir(BTBB_INSTALL_LIBDIR btbb_libdir_depth
		"${CMAKE_INSTALL_LIBDIR}" "CMAKE_INSTALL_LIBDIR")
endif()
btbb_clean_install_dir(BTBB_INSTALL_INCLUDEDIR btbb_includedir_depth
	"${CMAKE_INSTALL_INCLUDEDIR}" "CMAKE_INSTALL_INCLUDEDIR")

# What this build will install where, said once and in full: a packager who
# set one of these reads it back, and a plain build on a lib64 system sees
# why the library is not going where the last one did.
foreach(dir LIBDIR INCLUDEDIR)
	if(IS_ABSOLUTE "${BTBB_INSTALL_${dir}}")
		set(btbb_full_${dir} "${BTBB_INSTALL_${dir}}")
	elseif("${BTBB_INSTALL_${dir}}" STREQUAL ".")
		set(btbb_full_${dir} "${CMAKE_INSTALL_PREFIX}")
	else()
		set(btbb_full_${dir} "${CMAKE_INSTALL_PREFIX}/${BTBB_INSTALL_${dir}}")
	endif()
endforeach()
message(STATUS
	"Installing the library to ${btbb_full_LIBDIR} and the header to "
	"${btbb_full_INCLUDEDIR}")

# Where LIB_SUFFIX overrides a CMAKE_INSTALL_LIBDIR that asks for somewhere
# else, which of the two was followed is worth saying: a build command can
# carry both, one of them from the system rather than from its author. The
# comparison is between the cleaned directories, so that a trailing slash or
# a "./" on one of them is not a disagreement.
if(btbb_libdir_from_suffix)
	btbb_clean_install_dir(btbb_standard_libdir btbb_standard_libdir_depth
		"${CMAKE_INSTALL_LIBDIR}" "CMAKE_INSTALL_LIBDIR")
	if(NOT "${btbb_standard_libdir}" STREQUAL "${BTBB_INSTALL_LIBDIR}")
		message(STATUS
			"LIB_SUFFIX asks for ${BTBB_INSTALL_LIBDIR}, which is used "
			"rather than the ${btbb_standard_libdir} of CMAKE_INSTALL_LIBDIR")
	endif()
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
if(IS_ABSOLUTE "${BTBB_INSTALL_LIBDIR}")
	set(PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
	set(PC_LIBDIR "${BTBB_INSTALL_LIBDIR}")
	# Such an install cannot be staged somewhere else and still be found,
	# which is worth saying while the directories are being chosen: the file
	# lands outside the prefix, so it has no way to tell that the prefix has
	# moved, and the header it names would be the one at the old place. Said
	# only where the header is the one that would move: an absolute
	# includedir is named as it stands and does not move either.
	if(NOT IS_ABSOLUTE "${BTBB_INSTALL_INCLUDEDIR}")
		message(STATUS
			"libbtbb.pc will name ${CMAKE_INSTALL_PREFIX} as the prefix of "
			"the header, because an absolute library directory leaves the "
			"file with no relation to the prefix: an install staged "
			"elsewhere with --prefix needs its own configure")
	endif()
else()
	# One ".." for each directory between the prefix and this file, which is
	# in a pkgconfig directory inside the library's own.
	math(EXPR pc_depth "${btbb_libdir_depth} + 1")
	set(pc_ups "")
	foreach(level RANGE 1 ${pc_depth})
		list(APPEND pc_ups "..")
	endforeach()
	string(REPLACE ";" "/" pc_prefix_up "${pc_ups}")
	set(PC_PREFIX "\${pcfiledir}/${pc_prefix_up}")
	if("${BTBB_INSTALL_LIBDIR}" STREQUAL ".")
		set(PC_LIBDIR "\${prefix}")
	else()
		set(PC_LIBDIR "\${prefix}/${BTBB_INSTALL_LIBDIR}")
	endif()
endif()
if(IS_ABSOLUTE "${BTBB_INSTALL_INCLUDEDIR}")
	set(PC_INCLUDEDIR "${BTBB_INSTALL_INCLUDEDIR}")
elseif("${BTBB_INSTALL_INCLUDEDIR}" STREQUAL ".")
	set(PC_INCLUDEDIR "\${prefix}")
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
