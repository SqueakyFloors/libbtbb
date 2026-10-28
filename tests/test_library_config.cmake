# The library configurations the build offers. Run as:
#   cmake -DSOURCE_DIR=<source> -DBINARY_DIR=<scratch> -P test_library_config.cmake
#
# These cases configure, and in one case build, a tree of their own, because
# which library a build produces is decided at configure time: the build
# running this test is in one configuration only and cannot see the others.

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
	message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR must be set")
endif()

set(failures 0)

function(fail message)
	message(SEND_ERROR "${message}")
	math(EXPR failures "${failures} + 1")
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# A configuration that builds neither library leaves nothing for the test
# programs, or for anything else, to link against. It has to be refused while
# configuring: at the link the message names a missing library rather than the
# options that were asked for.
set(no_library ${BINARY_DIR}/no-library)
file(REMOVE_RECURSE ${no_library})
execute_process(
	COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${no_library}
	        -DBUILD_SHARED_LIB=OFF -DBUILD_STATIC_LIB=OFF
	        -DENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE error)
if(result EQUAL 0)
	fail("a configuration building neither library was accepted")
elseif(NOT "${output}${error}" MATCHES "at least one must be built")
	fail("the refusal does not say a library has to be built:\n${error}")
endif()

# The static library on its own: the configuration the Ubertooth tools use
# when they are linked statically. One test program covers what matters here,
# which is that the programs link against the library this configuration
# actually built.
set(static_only ${BINARY_DIR}/static-only)
file(REMOVE_RECURSE ${static_only})
execute_process(
	COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${static_only}
	        -DBUILD_SHARED_LIB=OFF -DBUILD_STATIC_LIB=ON
	        -DENABLE_PYTHON=OFF
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE error)
if(NOT result EQUAL 0)
	fail("the static-only configuration was refused:\n${output}${error}")
else()
	execute_process(
		COMMAND ${CMAKE_COMMAND} --build ${static_only} --target test_smoke
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		fail("a static-only build does not build its tests:\n${output}${error}")
	endif()
endif()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
message(STATUS "all cases passed")
