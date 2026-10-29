#
# libFuzzer targets for the decoders and the capture writers.
#
# They are built where the compiler can build a libFuzzer program and skipped
# where it cannot, so that a build with a compiler that has no fuzzer - any
# gcc, and the clang Apple ships - is unaffected.
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

include(CheckCSourceCompiles)

# The compile flag instruments a translation unit for the fuzzer; the link
# flag brings in the fuzzer itself, which provides main(). A toolchain whose
# driver does not ship the runtime can be pointed at the archive instead.
set(FUZZER_COMPILE_FLAGS "-fsanitize=fuzzer-no-link" CACHE STRING
    "Flags that instrument a translation unit for libFuzzer")
set(FUZZER_LINK_FLAGS "-fsanitize=fuzzer" CACHE STRING
    "Flags that link the libFuzzer runtime, which provides main()")

# A libFuzzer program has no main() of its own, so this is a link of the
# runtime as much as a compile check.
set(CMAKE_REQUIRED_FLAGS "${FUZZER_COMPILE_FLAGS} ${FUZZER_LINK_FLAGS}")
check_c_source_compiles("
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	(void) data;
	(void) size;
	return 0;
}
" HAVE_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT HAVE_LIBFUZZER)
    message(STATUS "Fuzz targets: not built, this compiler builds no libFuzzer program")
    return()
endif()
message(STATUS "Fuzz targets: built")

# The library source is compiled into each target rather than linked, so that
# the code under test carries the fuzzer's instrumentation however the library
# itself was configured. RELEASE is what the library's own build defines.
foreach(fuzz_target find_ac bredr le pcap pcapng)
    add_executable(fuzz_${fuzz_target} fuzz_${fuzz_target}.c ${c_sources})
    target_include_directories(fuzz_${fuzz_target}
                               PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_definitions(fuzz_${fuzz_target}
                               PRIVATE RELEASE="${RELEASE_STRING}")
    target_compile_options(fuzz_${fuzz_target} PRIVATE ${FUZZER_COMPILE_FLAGS})
    target_link_libraries(fuzz_${fuzz_target} ${FUZZER_LINK_FLAGS})

    # The seed corpus, run once through: it costs a second, it keeps the
    # targets working rather than merely building, and a crash that was fixed
    # stays fixed because its input is in the corpus.
    # Standard output is closed because a dump of every packet is not what a
    # test run is for; standard error is left open, so that a sanitizer's
    # report of a failure is in the log.
    add_test(NAME fuzz_${fuzz_target}_corpus
             COMMAND fuzz_${fuzz_target} -runs=0 -close_fd_mask=1
                     ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${fuzz_target})
    set_tests_properties(fuzz_${fuzz_target}_corpus PROPERTIES TIMEOUT 60)
endforeach()
