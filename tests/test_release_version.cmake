# The release string to PEP 440 version mapping in cmake/set_release.cmake.
# Run as: cmake -DSET_RELEASE_CMAKE=<path> -P test_release_version.cmake

if(NOT DEFINED SET_RELEASE_CMAKE)
	message(FATAL_ERROR "SET_RELEASE_CMAKE is not set")
endif()

# Release string -> expected PACKAGE_VERSION. The expectations spell out the
# version LATEST_RELEASE maps to, so moving that release moves these too.
set(cases
	"2020-12-R1|2020.12.1"
	"2018-06-R1|2018.6.1"
	"2015-09-R2|2015.9.2"
	"2020-12-R1+fork1|2020.12.1+fork1"
	"git-f0fe176|2020.12.1+git.f0fe176"
	"git-f0fe176*|2020.12.1+git.f0fe176.dirty"
	"git-F0FE176|2020.12.1+git.f0fe176"
	"my local build|2020.12.1+my.local.build"
	"-|2020.12.1+unknown"
)

set(failures 0)
foreach(case IN LISTS cases)
	string(REPLACE "|" ";" parts "${case}")
	list(GET parts 0 release)
	list(GET parts 1 expected)

	set(RELEASE_STRING "${release}")
	unset(PACKAGE_VERSION)
	include("${SET_RELEASE_CMAKE}")
	unset(RELEASE_STRING)

	if(NOT PACKAGE_VERSION STREQUAL expected)
		message(SEND_ERROR
			"'${release}' gave '${PACKAGE_VERSION}', expected '${expected}'")
		math(EXPR failures "${failures} + 1")
	endif()
endforeach()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
list(LENGTH cases case_count)
message(STATUS "all ${case_count} cases passed")
