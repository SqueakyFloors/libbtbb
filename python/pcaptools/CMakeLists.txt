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

add_custom_command(OUTPUT ${OUTPUT}/timestamp
                   COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT}
                   COMMAND ${Python3_EXECUTABLE} -m pip wheel
                           --no-build-isolation --no-deps
                           --wheel-dir ${OUTPUT} ${PROJECT_DIR}
                   COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}/timestamp
                   DEPENDS ${DEPS}
                   COMMENT "Building the pcapdump wheel")

add_custom_target(pcapdump ALL DEPENDS ${OUTPUT}/timestamp)

# BUILD_ROOT is how the packaging scripts stage an install under a temporary
# root. CMAKE_INSTALL_PREFIX is read at install time so that
# `cmake --install --prefix` works.
install(CODE "
	file(GLOB wheels \"${OUTPUT}/pcapdump-*.whl\")
	if(NOT wheels)
		message(FATAL_ERROR \"no pcapdump wheel in ${OUTPUT} - build first\")
	endif()
	execute_process(
		COMMAND \"${Python3_EXECUTABLE}\" -m pip install
			--no-deps --upgrade
			--prefix \"${BUILD_ROOT}\${CMAKE_INSTALL_PREFIX}\"
			\${wheels}
		RESULT_VARIABLE pip_result)
	if(NOT pip_result EQUAL 0)
		message(FATAL_ERROR \"installing the pcapdump wheel failed: \${pip_result}\")
	endif()
")
