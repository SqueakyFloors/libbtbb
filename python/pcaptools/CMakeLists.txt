# Copyright 2013 Dominic Spill
#
# This file is part of Libbtbb (pcapdump)
#
# GNU Radio is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# GNU Radio is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

# btaptap is a script, it needs no packaging. It does need to know how far
# below the install prefix it is installed, because that is where it looks
# for the pcapdump module: the module goes into the prefix's site directory,
# which the interpreter searches only when the prefix is its own. So the
# copy that is installed carries the depth of INSTALL_DEFAULT_BINDIR, and
# the script in the source tree, where the module is beside it, is left as
# it is.
set(BTAPTAP ${CMAKE_CURRENT_SOURCE_DIR}/btaptap)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BTAPTAP})

# The depth is the number of directories the path descends: "." counts for
# nothing and ".." for one back up, so bin, bin/ and ./bin are all 1. A path
# that leaves the prefix at any point, like an absolute one or ../bin, puts
# the script outside its own install, where there is no prefix of its own to
# look in.
set(BINDIR_DEPTH 0)
set(bindir_escapes FALSE)
if(IS_ABSOLUTE "${INSTALL_DEFAULT_BINDIR}")
	set(bindir_escapes TRUE)
endif()
string(REGEX MATCHALL "[^/]+" bindir_parts "${INSTALL_DEFAULT_BINDIR}")
foreach(part IN LISTS bindir_parts)
	if(part STREQUAL "..")
		math(EXPR BINDIR_DEPTH "${BINDIR_DEPTH} - 1")
		if(BINDIR_DEPTH LESS 0)
			set(BINDIR_DEPTH 0)
			set(bindir_escapes TRUE)
		endif()
	elseif(NOT part STREQUAL ".")
		math(EXPR BINDIR_DEPTH "${BINDIR_DEPTH} + 1")
	endif()
endforeach()

if(bindir_escapes)
	message(WARNING
	        "INSTALL_DEFAULT_BINDIR leaves the install prefix, so btaptap "
	        "is installed outside it and will look for the pcapdump module "
	        "outside it too")
endif()

file(READ ${BTAPTAP} btaptap_text)
if(NOT btaptap_text MATCHES "\nBINDIR_DEPTH = [0-9]+\n")
	message(FATAL_ERROR "btaptap has no BINDIR_DEPTH line to set")
endif()
string(REGEX REPLACE "\nBINDIR_DEPTH = [0-9]+\n"
                     "\nBINDIR_DEPTH = ${BINDIR_DEPTH}\n"
                     btaptap_text "${btaptap_text}")
set(BTAPTAP_INSTALLED ${CMAKE_CURRENT_BINARY_DIR}/btaptap)
file(WRITE ${BTAPTAP_INSTALLED} "${btaptap_text}")

add_custom_target(btaptap ALL DEPENDS ${BTAPTAP_INSTALLED})
install(PROGRAMS ${BTAPTAP_INSTALLED} DESTINATION ${INSTALL_DEFAULT_BINDIR})

# What the tool gets out of a capture, and what it says when it cannot. It
# runs on the script in the source tree, where the pcapdump module beside it
# is the one it reads captures with; the installed copy, which has to find
# that module elsewhere, is covered by btaptap_install_prefix below.
if(BUILD_TESTING)
	add_test(NAME btaptap COMMAND ${Python3_EXECUTABLE}
	         ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_btaptap.py)
	set_tests_properties(btaptap PROPERTIES TIMEOUT 60)
endif()

# setup.py install is gone from current setuptools, so the pcapdump module is
# built into a wheel with pip and the wheel is installed.
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip --version
                RESULT_VARIABLE PIP_MISSING
                OUTPUT_QUIET ERROR_QUIET)
if(PIP_MISSING)
	message(STATUS "pip not found - not building the pcapdump module")
	return()
endif()

# Build against the setuptools that is already installed when it is one that
# can do the whole job on its own, so that the build needs no network:
# the [project] table needs 61, and building a wheel without the separate
# wheel package needs 70.1. Otherwise pip fetches a build backend itself.
execute_process(COMMAND ${Python3_EXECUTABLE} -c
                "import sys, setuptools; \
                 v = setuptools.__version__.split('.'); \
                 sys.exit(0 if (int(v[0]), int(v[1] if len(v) > 1 else 0)) >= (70, 1) else 1)"
                RESULT_VARIABLE SETUPTOOLS_TOO_OLD
                OUTPUT_QUIET ERROR_QUIET)
if(SETUPTOOLS_TOO_OLD)
	message(STATUS "no setuptools 70.1 or newer - pip will fetch a build backend")
	set(PIP_ISOLATION)
else()
	set(PIP_ISOLATION "--no-build-isolation")
endif()

# The package is assembled in the build directory: the generated
# pyproject.toml carries the version, and the sources are copied in beside it
# so that pip sees one self-contained project and the source tree stays clean.
set(PROJECT_DIR ${CMAKE_CURRENT_BINARY_DIR}/pcapdump-dist)
set(OUTPUT      ${CMAKE_CURRENT_BINARY_DIR}/build)
set(SOURCES     pcapdump/__init__.py
                pcapdump/pcapdump.py)

configure_file(pyproject.toml.in ${PROJECT_DIR}/pyproject.toml @ONLY)
set(DEPS ${PROJECT_DIR}/pyproject.toml)
foreach(source ${SOURCES})
	configure_file(${source} ${PROJECT_DIR}/${source} COPYONLY)
	list(APPEND DEPS ${PROJECT_DIR}/${source})
endforeach()

# The wheel directory is emptied first: its name carries the version, so a
# reconfigure would otherwise leave a second wheel behind for the install step
# to trip over.
add_custom_command(OUTPUT ${OUTPUT}/timestamp
                   COMMAND ${CMAKE_COMMAND} -E rm -rf ${OUTPUT}
                   COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT}
                   COMMAND ${Python3_EXECUTABLE} -m pip wheel
                           ${PIP_ISOLATION} --no-deps
                           --wheel-dir ${OUTPUT} ${PROJECT_DIR}
                   COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}/timestamp
                   DEPENDS ${DEPS}
                   COMMENT "Building the pcapdump wheel")

add_custom_target(pcapdump ALL DEPENDS ${OUTPUT}/timestamp)

# The module is installed where the install prefix says, which the
# interpreter searches only when that prefix is its own, so btaptap looks
# there itself. This checks that it does, on an install of its own.
#
# It runs on the copy that is installed, in the binary directory that copy
# was made for, so a configured INSTALL_DEFAULT_BINDIR is covered as it is -
# unless it leaves the prefix, where the warning above says the module will
# not be found and there is nothing to test for.
if(BUILD_TESTING AND NOT bindir_escapes)
	add_test(NAME btaptap_install_prefix COMMAND ${CMAKE_COMMAND}
	         -DPYTHON=${Python3_EXECUTABLE}
	         -DBTAPTAP=${BTAPTAP_INSTALLED}
	         -DWHEEL_DIR=${OUTPUT}
	         -DBINDIR=${INSTALL_DEFAULT_BINDIR}
	         -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/install-prefix-test
	         -P ${CMAKE_CURRENT_SOURCE_DIR}/test_install_prefix.cmake)
	set_tests_properties(btaptap_install_prefix PROPERTIES TIMEOUT 60)
endif()

# DESTDIR is how a packager stages an install under a temporary root, and
# BUILD_ROOT is this project's own version of the same idea; both are honoured,
# as is CMAKE_INSTALL_PREFIX, which is read at install time so that
# `cmake --install --prefix` works.
install(CODE "
	file(GLOB wheels \"${OUTPUT}/pcapdump-*.whl\")
	list(LENGTH wheels wheel_count)
	if(NOT wheel_count EQUAL 1)
		message(FATAL_ERROR
			\"expected one pcapdump wheel in ${OUTPUT}, found \${wheel_count}\")
	endif()
	# --ignore-installed because pip otherwise leaves the prefix empty, and
	# says so and exits 0, when the same version is already installed where
	# the interpreter itself looks. The wheel is a file, so nothing has to
	# be looked up or checked for over the network.
	execute_process(
		COMMAND \"${Python3_EXECUTABLE}\" -m pip install
			--no-deps --upgrade --ignore-installed
			--no-index --disable-pip-version-check
			--prefix \"\$ENV{DESTDIR}${BUILD_ROOT}\${CMAKE_INSTALL_PREFIX}\"
			\${wheels}
		RESULT_VARIABLE pip_result)
	if(NOT pip_result EQUAL 0)
		message(FATAL_ERROR \"installing the pcapdump wheel failed: \${pip_result}\")
	endif()
")
