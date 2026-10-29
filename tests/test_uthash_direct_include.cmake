# uthash.h is reached through btbb_uthash.h, which sets the settings uthash
# reads as it is included. This checks that the wrapper compiles and that
# uthash.h on its own does not, since a direct include would silently get
# uthash's defaults, where an allocation failure ends the process with no
# message.
#
# Run as: cmake -DC_COMPILER=<cc> -DINCLUDE_DIR=<lib/src> -P
#         test_uthash_direct_include.cmake

if(NOT DEFINED C_COMPILER)
	message(FATAL_ERROR "C_COMPILER is not set")
endif()
if(NOT DEFINED INCLUDE_DIR)
	message(FATAL_ERROR "INCLUDE_DIR is not set")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/uthash-include-test")
file(MAKE_DIRECTORY "${work}")

# Compile one source, and report whether it compiled and what it said.
function(compile_source name source out_result out_output)
	file(WRITE "${work}/${name}.c" "${source}")
	execute_process(
		COMMAND "${C_COMPILER}" -c "${work}/${name}.c"
			-I "${INCLUDE_DIR}" -o "${work}/${name}.o"
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE errors)
	set(${out_result} "${result}" PARENT_SCOPE)
	set(${out_output} "${output}${errors}" PARENT_SCOPE)
endfunction()

set(failures 0)

compile_source(through_the_wrapper
	"#include \"btbb_uthash.h\"\nint main(void) { return 0; }\n"
	wrapper_result wrapper_output)
if(NOT wrapper_result EQUAL 0)
	message(SEND_ERROR
		"btbb_uthash.h did not compile:\n${wrapper_output}")
	math(EXPR failures "${failures} + 1")
endif()

compile_source(uthash_on_its_own
	"#include \"uthash.h\"\nint main(void) { return 0; }\n"
	direct_result direct_output)
if(direct_result EQUAL 0)
	message(SEND_ERROR "uthash.h compiled on its own; it should refuse to")
	math(EXPR failures "${failures} + 1")
elseif(NOT direct_output MATCHES "btbb_uthash.h")
	message(SEND_ERROR
		"uthash.h was refused without saying what to include instead:\n${direct_output}")
	math(EXPR failures "${failures} + 1")
endif()

file(REMOVE_RECURSE "${work}")

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
message(STATUS "the wrapper compiles and uthash.h on its own does not")
