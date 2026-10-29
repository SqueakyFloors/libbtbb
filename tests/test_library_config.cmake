# The library configurations the build offers. Run as:
#   cmake -DSOURCE_DIR=<source> -DBINARY_DIR=<scratch> -P test_library_config.cmake
#
# These cases configure, and in one case build, a tree of their own, because
# which library a build produces is decided at configure time: the build
# running this test is in one configuration only and cannot see the others.

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
	message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR must be set")
endif()

# What the build running this test is using, so that the trees configured
# below are built the same way: a cross-build's compiler is not the host's.
set(like_this_build)
if(GENERATOR)
	list(APPEND like_this_build -G "${GENERATOR}")
endif()
if(C_COMPILER)
	list(APPEND like_this_build -DCMAKE_C_COMPILER=${C_COMPILER})
endif()
if(TOOLCHAIN_FILE)
	list(APPEND like_this_build -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE})
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
	        ${like_this_build}
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
	        ${like_this_build}
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

# What pkg-config tells a program that links the installed library. The
# install writes the library to lib${LIB_SUFFIX} and the header to include,
# and libbtbb.pc has to name those two directories; a -L or -I for a
# directory holding neither leaves the program to link by luck or not at all.
# CMAKE_INSTALL_LIBDIR and CMAKE_INSTALL_INCLUDEDIR are set to names the
# install does not use, which is what a multiarch or lib64 system does on its
# own.
set(pc_build ${BINARY_DIR}/pkgconfig-build)
set(pc_install ${BINARY_DIR}/pkgconfig-install)
file(REMOVE_RECURSE ${pc_build} ${pc_install})
execute_process(
	COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${pc_build}
	        ${like_this_build}
	        -DCMAKE_INSTALL_PREFIX=${pc_install}
	        -DCMAKE_INSTALL_LIBDIR=lib-not-used
	        -DCMAKE_INSTALL_INCLUDEDIR=include-not-used
	        -DBUILD_STATIC_LIB=OFF
	        -DENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE error)
if(NOT result EQUAL 0)
	fail("the tree for the pkg-config case was refused:\n${output}${error}")
else()
	execute_process(COMMAND ${CMAKE_COMMAND} --build ${pc_build}
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		fail("the tree for the pkg-config case does not build:\n${output}${error}")
	else()
		execute_process(COMMAND ${CMAKE_COMMAND} --install ${pc_build}
			RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
		if(NOT result EQUAL 0)
			fail("the install for the pkg-config case failed:\n${output}${error}")
		endif()
	endif()
endif()

file(GLOB pc_files ${pc_install}/*/pkgconfig/libbtbb.pc)
if(NOT pc_files)
	fail("the install wrote no libbtbb.pc under ${pc_install}")
else()
	list(GET pc_files 0 pc_file)
	set(pc_libdir)
	set(pc_includedir)
	file(STRINGS ${pc_file} pc_lines)
	foreach(line IN LISTS pc_lines)
		if(line MATCHES "^libdir=(.+)$")
			set(pc_libdir "${CMAKE_MATCH_1}")
		elseif(line MATCHES "^includedir=(.+)$")
			set(pc_includedir "${CMAKE_MATCH_1}")
		endif()
	endforeach()
	# Both are written relative to the prefix, which is where this case
	# installed.
	string(REPLACE "\${prefix}" "${pc_install}" pc_libdir "${pc_libdir}")
	string(REPLACE "\${prefix}" "${pc_install}" pc_includedir "${pc_includedir}")

	if(NOT pc_libdir)
		fail("libbtbb.pc has no libdir line:\n${pc_lines}")
	else()
		file(GLOB pc_libs ${pc_libdir}/libbtbb.*)
		if(NOT pc_libs)
			fail("libbtbb.pc names libdir ${pc_libdir}, which holds no library")
		endif()
	endif()

	if(NOT pc_includedir)
		fail("libbtbb.pc has no includedir line:\n${pc_lines}")
	elseif(NOT EXISTS ${pc_includedir}/btbb.h)
		fail("libbtbb.pc names includedir ${pc_includedir}, which holds no btbb.h")
	endif()
endif()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
message(STATUS "all cases passed")
