# The library configurations the build offers. Run as:
#   cmake -DSOURCE_DIR=<source> -DBINARY_DIR=<scratch>
#         -DSHARED_SUFFIX=<.so or .dylib> -P test_library_config.cmake
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

# message() joins its arguments, so a report written as several strings
# arrives whole.
function(fail)
	message(SEND_ERROR ${ARGV})
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

# Find the one libbtbb.pc an install left under a prefix, and check it.
function(check_installed_pc where what)
	# The libdir can be the prefix itself or carry a path, so the file is in
	# the prefix or one or two directories under it.
	file(GLOB pc_files
		${where}/pkgconfig/libbtbb.pc
		${where}/*/pkgconfig/libbtbb.pc
		${where}/*/*/pkgconfig/libbtbb.pc)
	if(NOT pc_files)
		fail("${what}: no libbtbb.pc was installed under ${where}")
		set(failures ${failures} PARENT_SCOPE)
		return()
	endif()
	list(LENGTH pc_files pc_count)
	if(pc_count GREATER 1)
		fail("${what}: ${pc_count} libbtbb.pc files were installed under "
		     "${where}, where a program would have one: ${pc_files}")
		set(failures ${failures} PARENT_SCOPE)
		return()
	endif()
	list(GET pc_files 0 pc_file)
	check_pc_file(${pc_file} "${what}")
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# Read an installed libbtbb.pc the way pkg-config reads it - pcfiledir is the
# directory the file is in, and the other variables are built from it - and
# check that the two directories it hands a program hold the library that
# `Libs: -lbtbb` asks the linker for and the header `Cflags` points at.
function(check_pc_file pc_file what)
	get_filename_component(pcfiledir ${pc_file} DIRECTORY)

	set(pc_prefix "")
	set(pc_libdir "")
	set(pc_includedir "")
	file(STRINGS ${pc_file} pc_lines)
	foreach(line IN LISTS pc_lines)
		if(line MATCHES "^prefix=(.*)$")
			set(pc_prefix "${CMAKE_MATCH_1}")
		elseif(line MATCHES "^libdir=(.*)$")
			set(pc_libdir "${CMAKE_MATCH_1}")
		elseif(line MATCHES "^includedir=(.*)$")
			set(pc_includedir "${CMAKE_MATCH_1}")
		endif()
	endforeach()

	if(NOT pc_libdir OR NOT pc_includedir)
		fail("${what}: libbtbb.pc has no libdir or includedir line:\n${pc_lines}")
		set(failures ${failures} PARENT_SCOPE)
		return()
	endif()

	# prefix first, since the other two are written relative to it. ABSOLUTE
	# collapses the "../.." that leads back out of the pkgconfig directory.
	string(REPLACE "\${pcfiledir}" "${pcfiledir}" pc_prefix "${pc_prefix}")
	foreach(var pc_libdir pc_includedir)
		string(REPLACE "\${prefix}" "${pc_prefix}" ${var} "${${var}}")
		get_filename_component(${var} "${${var}}" ABSOLUTE)
	endforeach()

	if(NOT EXISTS ${pc_libdir}/libbtbb${SHARED_SUFFIX})
		fail("${what}: libbtbb.pc names libdir ${pc_libdir}, which holds no "
		     "libbtbb${SHARED_SUFFIX} for -lbtbb to find")
	endif()
	if(NOT EXISTS ${pc_includedir}/btbb.h)
		fail("${what}: libbtbb.pc names includedir ${pc_includedir}, which "
		     "holds no btbb.h")
	endif()
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# The directories an install was asked for are the ones it writes to. The two
# are given relative to the prefix, as CMAKE_INSTALL_LIBDIR and LIB_SUFFIX
# are.
function(check_layout where libdir includedir what)
	foreach(file
		${libdir}/libbtbb${SHARED_SUFFIX}
		${libdir}/pkgconfig/libbtbb.pc
		${includedir}/btbb.h)
		if(NOT EXISTS ${where}/${file})
			# What did land, so that a report says where the file went
			# instead of only that it is missing.
			file(GLOB_RECURSE landed RELATIVE ${where} ${where}/*)
			fail("${what}: ${file} was not installed under ${where}, which "
			     "holds:\n${landed}")
		endif()
	endforeach()
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# Configure, build and install a tree of its own into its own prefix. The
# options for the case go in ARGN. "installed" is left TRUE for a case that
# got as far as installing, so the checks a failed case cannot answer are
# skipped rather than reported as further failures.
function(build_and_install what build install)
	set(installed FALSE PARENT_SCOPE)
	file(REMOVE_RECURSE ${build} ${install})
	execute_process(
		COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${build}
		        ${like_this_build}
		        -DCMAKE_INSTALL_PREFIX=${install}
		        ${ARGN}
		        -DBUILD_STATIC_LIB=OFF
		        -DENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		fail("the tree for ${what} was refused:\n${output}${error}")
	else()
		execute_process(COMMAND ${CMAKE_COMMAND} --build ${build}
			RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
		if(NOT result EQUAL 0)
			fail("the tree for ${what} does not build:\n${output}${error}")
		else()
			execute_process(COMMAND ${CMAKE_COMMAND} --install ${build}
				RESULT_VARIABLE result OUTPUT_VARIABLE output
				ERROR_VARIABLE error)
			if(NOT result EQUAL 0)
				fail("the install for ${what} failed:\n${output}${error}")
			else()
				set(installed TRUE PARENT_SCOPE)
			endif()
		endif()
	endif()
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# Where an install writes the library, the header and libbtbb.pc, and what
# pkg-config then tells a program that links it. A -L or -I for a directory
# holding neither leaves the program to link by luck or not at all.
#
# The plain build first: nothing is passed, so the directories are the ones
# GNUInstallDirs works out for this system, whatever they are, and the two
# libbtbb.pc names have to be those.
set(plain_build ${BINARY_DIR}/plain-build)
set(plain_install ${BINARY_DIR}/plain-install)
build_and_install("a build with no directories given" ${plain_build} ${plain_install})
if(installed)
	check_installed_pc(${plain_install} "installed with no directories given")
endif()

# CMAKE_INSTALL_LIBDIR and CMAKE_INSTALL_INCLUDEDIR, the standard settings:
# a 64-bit Fedora, RHEL or SUSE sets the first to lib64 of its own accord,
# a multiarch Debian or Ubuntu to lib/<triplet> where the prefix is /usr, and
# a packager or a user can set either to anything. Names of their own here,
# so that a case passing says the setting was followed rather than that it
# happened to match the default.
set(chosen_build ${BINARY_DIR}/chosen-build)
set(chosen_install ${BINARY_DIR}/chosen-install)
build_and_install("the libdir and includedir the build was given"
	${chosen_build} ${chosen_install}
	-DCMAKE_INSTALL_LIBDIR=libdir-chosen
	-DCMAKE_INSTALL_INCLUDEDIR=includedir-chosen)
if(installed)
	check_layout(${chosen_install} libdir-chosen includedir-chosen
		"installed with CMAKE_INSTALL_LIBDIR and CMAKE_INSTALL_INCLUDEDIR set")
	check_installed_pc(${chosen_install}
		"installed to the prefix it was configured with")

	# The same build installed somewhere else, which is what
	# `cmake --install build --prefix <dir>` does and what this repository's
	# own CI does with it. The prefix the configure knew is not the one the
	# files end up under, so libbtbb.pc has to work its own out from where it
	# lands. The first install is taken away first, so that a libbtbb.pc
	# still naming the configure-time prefix points at nothing rather than at
	# those files.
	set(chosen_elsewhere ${BINARY_DIR}/chosen-install-elsewhere)
	file(REMOVE_RECURSE ${chosen_elsewhere} ${chosen_install})
	execute_process(
		COMMAND ${CMAKE_COMMAND} --install ${chosen_build} --prefix ${chosen_elsewhere}
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		fail("the install into a second prefix failed:\n${output}${error}")
	else()
		check_layout(${chosen_elsewhere} libdir-chosen includedir-chosen
			"installed with --prefix into another directory")
		check_installed_pc(${chosen_elsewhere}
			"installed with --prefix into another directory")
	endif()
endif()

# LIB_SUFFIX, the older setting, carrying a path rather than a suffix, which
# is how the Debian package builds this library
# (-DLIB_SUFFIX=/x86_64-linux-gnu): the library, and libbtbb.pc with it, go
# one directory deeper than usual.
set(ma_build ${BINARY_DIR}/multiarch-build)
set(ma_install ${BINARY_DIR}/multiarch-install)
set(ma_elsewhere ${BINARY_DIR}/multiarch-install-elsewhere)
file(REMOVE_RECURSE ${ma_elsewhere})
build_and_install("LIB_SUFFIX carrying a path" ${ma_build} ${ma_install}
	-DLIB_SUFFIX=/multiarch)
if(installed)
	check_layout(${ma_install} lib/multiarch include
		"installed with LIB_SUFFIX=/multiarch")
	check_installed_pc(${ma_install} "installed with LIB_SUFFIX=/multiarch")
	# And the same install staged somewhere else, the two together: a deeper
	# libdir and a prefix that is not the configured one.
	file(REMOVE_RECURSE ${ma_install})
	execute_process(
		COMMAND ${CMAKE_COMMAND} --install ${ma_build} --prefix ${ma_elsewhere}
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		fail("the LIB_SUFFIX path install into a second prefix failed:\n${output}${error}")
	else()
		check_installed_pc(${ma_elsewhere}
			"installed with LIB_SUFFIX=/multiarch and --prefix into another directory")
	endif()
endif()

# Both at once. A build command that passes LIB_SUFFIX installs where it
# always did, whatever the system or the other setting says, so an install
# recipe carrying it does not change under its author. The header is not
# LIB_SUFFIX's business and follows CMAKE_INSTALL_INCLUDEDIR either way.
set(both_build ${BINARY_DIR}/both-build)
set(both_install ${BINARY_DIR}/both-install)
build_and_install("LIB_SUFFIX together with CMAKE_INSTALL_LIBDIR"
	${both_build} ${both_install}
	-DLIB_SUFFIX=-suffix-wins
	-DCMAKE_INSTALL_LIBDIR=libdir-overridden
	-DCMAKE_INSTALL_INCLUDEDIR=includedir-chosen)
if(installed)
	check_layout(${both_install} lib-suffix-wins includedir-chosen
		"installed with LIB_SUFFIX and CMAKE_INSTALL_LIBDIR both set")
	check_installed_pc(${both_install}
		"installed with LIB_SUFFIX and CMAKE_INSTALL_LIBDIR both set")
	if(EXISTS ${both_install}/libdir-overridden)
		fail("installed with LIB_SUFFIX and CMAKE_INSTALL_LIBDIR both set: "
		     "something was written to the libdir-overridden of "
		     "CMAKE_INSTALL_LIBDIR, which LIB_SUFFIX overrides")
	endif()
endif()

# An empty LIB_SUFFIX asks for no suffix, which says nothing about which
# directory is wanted: the standard setting stays in charge rather than being
# turned into lib, so a build command carrying both still installs where
# CMAKE_INSTALL_LIBDIR says.
set(empty_build ${BINARY_DIR}/empty-suffix-build)
set(empty_install ${BINARY_DIR}/empty-suffix-install)
build_and_install("an empty LIB_SUFFIX with CMAKE_INSTALL_LIBDIR"
	${empty_build} ${empty_install}
	-DLIB_SUFFIX=
	-DCMAKE_INSTALL_LIBDIR=libdir-chosen)
if(installed)
	check_layout(${empty_install} libdir-chosen include
		"installed with an empty LIB_SUFFIX and CMAKE_INSTALL_LIBDIR set")
	check_installed_pc(${empty_install}
		"installed with an empty LIB_SUFFIX and CMAKE_INSTALL_LIBDIR set")
endif()

# Directories given as empty, which asks for the prefix itself: the files go
# straight into it, and libbtbb.pc is in a pkgconfig directory there. The
# install has to stay inside the prefix for that - a directory joined on to
# an empty one would be read as the root of the filesystem - and the walk
# back out of the pkgconfig directory is one directory shorter than usual.
set(root_build ${BINARY_DIR}/root-build)
set(root_install ${BINARY_DIR}/root-install)
build_and_install("the prefix itself as both directories"
	${root_build} ${root_install}
	-DCMAKE_INSTALL_LIBDIR=
	-DCMAKE_INSTALL_INCLUDEDIR=)
if(installed)
	check_layout(${root_install} . .
		"installed with both directories left empty")
	check_installed_pc(${root_install}
		"installed with both directories left empty")
endif()

# A directory written with a "." in it, and an absolute includedir beside it.
# CMake drops the "." when it installs, so libbtbb.pc has to count the same
# directories the install wrote, and the header's own setting is honored
# wherever the library goes.
set(dot_build ${BINARY_DIR}/dot-build)
set(dot_install ${BINARY_DIR}/dot-install)
set(dot_includedir ${BINARY_DIR}/dot-includedir)
file(REMOVE_RECURSE ${dot_includedir})
build_and_install("a libdir with a dot segment and an absolute includedir"
	${dot_build} ${dot_install}
	-DCMAKE_INSTALL_LIBDIR=./libdir-chosen/
	-DCMAKE_INSTALL_INCLUDEDIR=${dot_includedir})
if(installed)
	foreach(file ${dot_install}/libdir-chosen/libbtbb${SHARED_SUFFIX}
	             ${dot_install}/libdir-chosen/pkgconfig/libbtbb.pc
	             ${dot_includedir}/btbb.h)
		if(NOT EXISTS ${file})
			fail("installed with a dot segment and an absolute includedir: "
			     "${file} was not installed")
		endif()
	endforeach()
	check_installed_pc(${dot_install}
		"installed with a dot segment and an absolute includedir")
endif()

# Two cases that are settled while configuring, so they need no build or
# install: what libbtbb.pc is written with, and a directory that is refused.
function(check_configured_pc_libdir what expected)
	set(build ${BINARY_DIR}/configured)
	file(REMOVE_RECURSE ${build})
	execute_process(
		COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${build}
		        ${like_this_build}
		        -DCMAKE_INSTALL_PREFIX=${BINARY_DIR}/configured-prefix
		        ${ARGN}
		        -DENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		fail("${what}: the configure was refused:\n${output}${error}")
	else()
		file(STRINGS ${build}/lib/libbtbb.pc libdir_line REGEX "^libdir=")
		if(NOT "${libdir_line}" STREQUAL "libdir=${expected}")
			fail("${what}: libbtbb.pc has ${libdir_line}, "
			     "where it should have libdir=${expected}")
		endif()
	endif()
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# A suffix CMake reads as a false constant is still a suffix.
check_configured_pc_libdir("LIB_SUFFIX=0" "\${prefix}/lib0"
	-DLIB_SUFFIX=0 -DCMAKE_INSTALL_LIBDIR=libdir-overridden)

# A directory leading out of the prefix is refused while configuring: the
# install would write outside it, and libbtbb.pc could not say where.
# A ".." that has a directory before it to go back through is followed, as
# the install follows it, so each of these is a directory that really would
# land outside the prefix.
foreach(out_option -DCMAKE_INSTALL_LIBDIR=../outside
                   -DCMAKE_INSTALL_INCLUDEDIR=../outside
                   -DLIB_SUFFIX=/../..)
	set(out_build ${BINARY_DIR}/out-of-prefix)
	file(REMOVE_RECURSE ${out_build})
	execute_process(
		COMMAND ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${out_build}
		        ${like_this_build}
		        ${out_option}
		        -DENABLE_PYTHON=OFF -DBUILD_TESTING=OFF
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE error)
	if(result EQUAL 0)
		fail("${out_option} leads out of the prefix and was accepted")
	# "leads out" alone: message() wraps its text, so a longer phrase can be
	# broken across two lines.
	elseif(NOT "${output}${error}" MATCHES "leads out")
		fail("the refusal of ${out_option} does not say what is wrong:"
		     "\n${output}${error}")
	endif()
endforeach()

# A libdir given as an absolute path, which GNUInstallDirs allows and a
# packager can hand to any CMake project: the library goes exactly there
# rather than under the prefix, and libbtbb.pc cannot name it relative to a
# prefix it has no relation to.
set(abs_build ${BINARY_DIR}/absolute-build)
set(abs_install ${BINARY_DIR}/absolute-install)
set(abs_libdir ${BINARY_DIR}/absolute-libdir)
file(REMOVE_RECURSE ${abs_libdir})
build_and_install("a libdir given as an absolute path"
	${abs_build} ${abs_install}
	-DCMAKE_INSTALL_LIBDIR=${abs_libdir})
if(installed)
	foreach(file ${abs_libdir}/libbtbb${SHARED_SUFFIX}
	             ${abs_libdir}/pkgconfig/libbtbb.pc
	             ${abs_install}/include/btbb.h)
		if(NOT EXISTS ${file})
			fail("installed with an absolute CMAKE_INSTALL_LIBDIR: ${file} "
			     "was not installed")
		endif()
	endforeach()
	if(EXISTS ${abs_libdir}/pkgconfig/libbtbb.pc)
		check_pc_file(${abs_libdir}/pkgconfig/libbtbb.pc
			"installed with an absolute CMAKE_INSTALL_LIBDIR")
	endif()
endif()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
message(STATUS "all cases passed")
