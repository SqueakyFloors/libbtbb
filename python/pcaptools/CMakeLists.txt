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

# btaptap is a script, it needs no packaging.
add_custom_target(btaptap ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/btaptap)
install(PROGRAMS btaptap DESTINATION ${INSTALL_DEFAULT_BINDIR})

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
if(BUILD_TESTING)
	add_test(NAME btaptap_install_prefix COMMAND ${CMAKE_COMMAND}
	         -DPYTHON=${Python3_EXECUTABLE}
	         -DBTAPTAP=${CMAKE_CURRENT_SOURCE_DIR}/btaptap
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
	execute_process(
		COMMAND \"${Python3_EXECUTABLE}\" -m pip install
			--no-deps --upgrade
			--prefix \"\$ENV{DESTDIR}${BUILD_ROOT}\${CMAKE_INSTALL_PREFIX}\"
			\${wheels}
		RESULT_VARIABLE pip_result)
	if(NOT pip_result EQUAL 0)
		message(FATAL_ERROR \"installing the pcapdump wheel failed: \${pip_result}\")
	endif()
")
