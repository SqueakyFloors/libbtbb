# What the shared library exports, against the header the install offers.
# Run as:
#   cmake -DLIBRARY=<libbtbb.so> -DHEADER=<btbb.h> -DNM=<nm>
#         [-DSTRIP_UNDERSCORE=ON] [-DDYNAMIC_SYMBOLS=ON] -P test_abi_surface.cmake
#
# btbb.h is the only header the install puts in include/, so it is the whole
# of what a program linking this library can call. Two things have to agree
# with it. Every function it declares has to be in the library, or a program
# that includes the header does not link. And no symbol under the library's
# own btbb_ or lell_ prefix may be exported without being declared, because
# such a symbol reads as part of the library's interface to anyone looking at
# the exported names, and once a program calls it, withdrawing it breaks that
# program.
#
# The library also exports internal symbols under other names: no prefix at
# all ("hop", "precalc", "DH") or one of its own ("bt_compidtostr",
# "pcapng_append_packet"). Hiding those is a change to the build and to the
# tests that call them, so they are left out here rather than listed as
# exceptions; issue #79 covers them.

if(NOT DEFINED LIBRARY OR NOT DEFINED HEADER OR NOT DEFINED NM)
	message(FATAL_ERROR "LIBRARY, HEADER and NM must be set")
endif()

set(failures 0)

# message() joins its arguments, so a report written as several strings
# arrives whole.
function(fail)
	message(SEND_ERROR ${ARGV})
	math(EXPR failures "${failures} + 1")
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# The declarations in the header: a return type at the start of a line, then
# the name and its argument list. Every comment line in the header is
# indented, so beginning at the first column is what tells a declaration from
# a comment that names a function.
set(declared)
file(STRINGS "${HEADER}" header_lines)
foreach(line IN LISTS header_lines)
	if(line MATCHES "^[A-Za-z_][A-Za-z0-9_ *]*[A-Za-z0-9_]\\(")
		string(REGEX MATCH "([A-Za-z_][A-Za-z0-9_]*)\\(" unused "${line}")
		list(APPEND declared "${CMAKE_MATCH_1}")
	endif()
endforeach()
list(REMOVE_DUPLICATES declared)
list(SORT declared)
list(LENGTH declared declared_count)

# A regex that matched nothing would leave every check below with nothing to
# compare, and the test would pass while reading none of the header.
if(declared_count LESS 50)
	message(FATAL_ERROR
		"only ${declared_count} declarations found in ${HEADER}: "
		"this test is not reading the header")
endif()

# What the library exports: "<address> <type> <name>" per line, with no
# address where the symbol has none. U, u, w and v are the symbols the library
# imports rather than defines. A Mach-O name carries a leading underscore,
# which the caller asks to be taken off.
#
# The dynamic symbol table is what a program resolves against at run time, and
# -D reads it, so the answer holds for a stripped library too. Mach-O keeps
# one table and its nm has no -D, so there -g is the whole of it.
if(DYNAMIC_SYMBOLS)
	set(nm_args -D --defined-only)
else()
	set(nm_args -g)
endif()
execute_process(
	COMMAND "${NM}" ${nm_args} "${LIBRARY}"
	OUTPUT_VARIABLE nm_output
	ERROR_VARIABLE nm_error
	RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "${NM} ${nm_args} ${LIBRARY} failed: ${nm_error}")
endif()

set(exported)
string(REPLACE "\n" ";" nm_lines "${nm_output}")
foreach(line IN LISTS nm_lines)
	if(line MATCHES "^[0-9a-fA-F]* +([A-Za-z]) +([A-Za-z_][A-Za-z0-9_]*)$")
		set(type "${CMAKE_MATCH_1}")
		set(name "${CMAKE_MATCH_2}")
		if(type MATCHES "^[Uuwv]$")
			continue()
		endif()
		if(STRIP_UNDERSCORE)
			string(REGEX REPLACE "^_" "" name "${name}")
		endif()
		list(APPEND exported "${name}")
	endif()
endforeach()
list(REMOVE_DUPLICATES exported)
list(SORT exported)
list(LENGTH exported exported_count)

# Likewise, a library whose symbols were not read at all would pass both
# checks below. The count is well under the real one so that adding or
# hiding symbols does not have to move it.
if(exported_count LESS 50)
	message(FATAL_ERROR
		"only ${exported_count} symbols read from ${LIBRARY}: "
		"this test is not reading the library")
endif()

foreach(name IN LISTS declared)
	list(FIND exported "${name}" found)
	if(found EQUAL -1)
		fail("${name}() is declared in btbb.h but the library does not "
		     "export it: a program including the header would not link")
	endif()
endforeach()

foreach(name IN LISTS exported)
	if(NOT name MATCHES "^(btbb|lell)_")
		continue()
	endif()
	list(FIND declared "${name}" found)
	if(found EQUAL -1)
		fail("${name} is exported under the library's public prefix but "
		     "btbb.h does not declare it: make it static, or declare it")
	endif()
endforeach()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
message(STATUS
	"all ${declared_count} declared functions are exported, and no other "
	"btbb_ or lell_ symbol is")
