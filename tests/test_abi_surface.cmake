# What the shared library exports, against the header the install offers.
# Run as:
#   cmake -DLIBRARY=<libbtbb.so> -DHEADER=<btbb.h> -DNM=<nm>
#         [-DSTRIP_UNDERSCORE=ON] [-DDYNAMIC_SYMBOLS=ON] -P test_abi_surface.cmake
#
# btbb.h is the only header the install puts in include/, so it is the whole
# of what a program linking this library can call, and the names the library
# exports have to be exactly the functions it declares. Three things are
# checked.
#
# Every function the header declares has to be exported, or a program that
# includes the header does not link.
#
# Nothing else may be exported. An exported symbol reads as part of the
# library's interface to anyone looking at the symbol table, and once a
# program calls it, withdrawing it breaks that program: that is how internal
# helpers named precalc, single_hop() or pcapng_create() come to be names the
# library cannot change. Names beginning with an underscore are left out,
# because those belong to the linker (_init, _end, __bss_start) rather than
# to the library.
#
# And every declaration in the header has to carry the BTBB_API macro, which
# is what gives the function default visibility in a library compiled with
# -fvisibility=hidden. Without it the function is declared but hidden, and
# the first check above is the one that would report it; naming the missing
# macro says what to do about it.

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
		set(name "${CMAKE_MATCH_1}")
		list(APPEND declared "${name}")
		if(NOT line MATCHES "^BTBB_API ")
			fail("${name}() is declared in btbb.h without BTBB_API, "
			     "so the library does not export it: write "
			     "BTBB_API in front of the declaration")
		endif()
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
# An nm without -D is not an error to stop on: the symbol table it does read
# answers the same question for a library the build has not stripped.
if(NOT result EQUAL 0 AND DYNAMIC_SYMBOLS)
	message(STATUS "${NM} has no -D; reading the symbol table instead")
	set(nm_args -g)
	execute_process(
		COMMAND "${NM}" ${nm_args} "${LIBRARY}"
		OUTPUT_VARIABLE nm_output
		ERROR_VARIABLE nm_error
		RESULT_VARIABLE result)
endif()
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
		# What is left beginning with an underscore belongs to the
		# linker, not to the library.
		if(name MATCHES "^_")
			continue()
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
	list(FIND declared "${name}" found)
	if(found EQUAL -1)
		fail("${name} is exported but btbb.h does not declare it: "
		     "make it static, or leave BTBB_API off it so the library "
		     "keeps it to itself, or declare it in the header")
	endif()
endforeach()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
message(STATUS
	"the library exports exactly the ${declared_count} functions btbb.h "
	"declares")
