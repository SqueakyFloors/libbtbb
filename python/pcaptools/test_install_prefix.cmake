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

	# And a smoke run as a user makes it, with whatever the interpreter's
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

# The search stops at the prefix the script is installed in. A module in a
# prefix further up belongs to another install, and where the prefix sits
# under a directory anyone can write to, it is a module anyone could have
# put there.
function(check_search_stops_at_prefix root)
	file(REMOVE_RECURSE "${root}")
	foreach(prefix "${root}" "${root}/inner")
		execute_process(COMMAND "${PYTHON}" -m pip install --no-deps
		                        --upgrade --prefix "${prefix}" "${wheel}"
		                RESULT_VARIABLE result
		                OUTPUT_VARIABLE output ERROR_VARIABLE output)
		if(NOT result EQUAL 0)
			message(FATAL_ERROR
				"installing the wheel into ${prefix} failed: "
				"${result}\n${output}")
		endif()
	endforeach()

	# Take the module out of the inner prefix, leaving its site directory
	# behind, so the inner prefix is an install without the module in it.
	file(GLOB modules "${root}/inner/lib*/python*/*-packages/pcapdump"
	                  "${root}/inner/local/lib*/python*/*-packages/pcapdump")
	list(LENGTH modules module_count)
	if(NOT module_count EQUAL 1)
		message(FATAL_ERROR
			"expected the module in one site directory of "
			"${root}/inner, found ${module_count}")
	endif()
	file(REMOVE_RECURSE ${modules})

	file(COPY "${BTAPTAP}" DESTINATION "${root}/inner/${BINDIR}")
	execute_process(COMMAND "${PYTHON}" -I -S
	                        "${root}/inner/${BINDIR}/${script_name}" -h
	                RESULT_VARIABLE result
	                OUTPUT_VARIABLE output ERROR_VARIABLE output)
	if(result EQUAL 0)
		message(FATAL_ERROR
			"${script_name} imported a module from outside the prefix "
			"it is installed in:\n${output}")
	endif()
	if(NOT output MATCHES "No module named")
		message(FATAL_ERROR
			"${script_name} failed, but not on the missing module:"
			"\n${output}")
	endif()

	file(REMOVE_RECURSE "${root}")
endfunction()

check_install("${PREFIX}" "${BINDIR}")

# A prefix named with a glob metacharacter in it is a directory name, not a
# pattern to match other directories with.
check_install("${PREFIX}-[1]" "${BINDIR}")

# A binary directory more than one level inside the prefix, as a packager
# configuring usr/bin gets.
check_install("${PREFIX}-deep" "usr/${BINDIR}")

check_search_stops_at_prefix("${PREFIX}-nested")

message(STATUS "btaptap imported pcapdump from an install of its own")
