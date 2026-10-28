# btaptap has to find its pcapdump module after an install to a prefix of its
# own, which is not the prefix the interpreter was built for.
#
# Run as: cmake -DPYTHON=<exe> -DBTAPTAP=<path> -DWHEEL_DIR=<dir>
#               -DBINDIR=<name> -DPREFIX=<dir> -P test_install_prefix.cmake

foreach(var PYTHON BTAPTAP WHEEL_DIR BINDIR PREFIX)
	if(NOT DEFINED ${var})
		message(FATAL_ERROR "${var} is not set")
	endif()
endforeach()

file(GLOB wheels "${WHEEL_DIR}/pcapdump-*.whl")
list(LENGTH wheels wheel_count)
if(NOT wheel_count EQUAL 1)
	message(FATAL_ERROR
		"expected one pcapdump wheel in ${WHEEL_DIR}, found ${wheel_count}")
endif()
list(GET wheels 0 wheel)

get_filename_component(script_name "${BTAPTAP}" NAME)

# Make the install the project's own install rules make - the wheel under the
# prefix, and the script in the prefix's binary directory - and run it.
function(check_install prefix bindir)
	file(REMOVE_RECURSE "${prefix}")
	execute_process(COMMAND "${PYTHON}" -m pip install --no-deps --upgrade
	                        --prefix "${prefix}" "${wheel}"
	                RESULT_VARIABLE result
	                OUTPUT_VARIABLE output ERROR_VARIABLE output)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR
			"installing the wheel into ${prefix} failed: "
			"${result}\n${output}")
	endif()

	file(COPY "${BTAPTAP}" DESTINATION "${prefix}/${bindir}")
	set(script "${prefix}/${bindir}/${script_name}")

	# -h is the shortest run that still imports the module: the import
	# happens as the script loads, before it looks at its arguments, and the
	# usage text goes to stderr.
	#
	# -I -S cuts off every other source of the module - the environment, the
	# user site directory, and the interpreter's own site-packages - so this
	# run can only pass if the script found the module through its own
	# location.
	execute_process(COMMAND "${PYTHON}" -I -S "${script}" -h
	                WORKING_DIRECTORY "${prefix}"
	                RESULT_VARIABLE result
	                OUTPUT_VARIABLE output ERROR_VARIABLE output)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR
			"${script} -h, with no other source of the module, "
			"failed: ${result}\n${output}")
	endif()
	if(NOT output MATCHES "Usage: ")
		message(FATAL_ERROR "${script} -h printed no usage text:\n${output}")
	endif()

	# And the same run as a user makes it, with whatever the interpreter's
	# own sys.path holds.
	execute_process(COMMAND "${PYTHON}" "${script}" -h
	                WORKING_DIRECTORY "${prefix}"
	                RESULT_VARIABLE result
	                OUTPUT_VARIABLE output ERROR_VARIABLE output)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "${script} -h failed: ${result}\n${output}")
	endif()

	file(REMOVE_RECURSE "${prefix}")
endfunction()

check_install("${PREFIX}" "${BINDIR}")

# A prefix named with a glob metacharacter in it is a directory name, not a
# pattern to match other directories with.
check_install("${PREFIX}-[1]" "${BINDIR}")

# A binary directory more than one level inside the prefix, as a packager
# configuring usr/bin gets.
check_install("${PREFIX}-deep" "usr/${BINDIR}")

message(STATUS "btaptap imported pcapdump from an install of its own")
