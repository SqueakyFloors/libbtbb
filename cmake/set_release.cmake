# This script controls the version string that is passed at compile time.
# To override the version, use cmake -DRELEASE_STRING="you_version"
# If RELEASE_STRING is not explicitly set we attempt to use the latest git
# commit.  If cmake isn't being run from within a git repository we use the
# LATEST_RELEASE variable, which developers should set before a release is
# tagged
#
# It also sets PACKAGE_VERSION, the same version expressed the way PEP 440
# requires, for the Python tools. See btbb_pep440_version() below.

set(LATEST_RELEASE "2020-12-R1")

# Map a release string onto a PEP 440 public version with an optional local
# version, which is what Python packaging tools accept:
#
#   2020-12-R1       -> 2020.12.1
#   2018-06-R1       -> 2018.6.1    (leading zeros are not PEP 440 normal form)
#   2020-12-R1+fork1 -> 2020.12.1+fork1
#   git-f0fe176      -> 2020.12.1+git.f0fe176
#   git-f0fe176*     -> 2020.12.1+git.f0fe176.dirty
#
# A release string in no recognised form keeps the latest release as the
# public version and carries the string itself in the local version, with
# anything PEP 440 does not allow there replaced by a dot.
function(btbb_pep440_version out release fallback_public)
	if(release MATCHES "^([0-9]+)-0*([0-9]+)-[Rr]0*([0-9]+)(\\+[a-zA-Z0-9]+)?$")
		string(TOLOWER "${CMAKE_MATCH_4}" local)
		set(${out}
		    "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}${local}"
		    PARENT_SCOPE)
		return()
	endif()

	if(release MATCHES "^git-([0-9a-fA-F]+)(\\*?)$")
		string(TOLOWER "${CMAKE_MATCH_1}" sha)
		if(CMAKE_MATCH_2 STREQUAL "*")
			set(${out} "${fallback_public}+git.${sha}.dirty" PARENT_SCOPE)
		else()
			set(${out} "${fallback_public}+git.${sha}" PARENT_SCOPE)
		endif()
		return()
	endif()

	string(TOLOWER "${release}" local)
	string(REGEX REPLACE "[^a-z0-9]+" "." local "${local}")
	string(REGEX REPLACE "^\\.+|\\.+$" "" local "${local}")
	if(local STREQUAL "")
		set(local "unknown")
	endif()
	set(${out} "${fallback_public}+${local}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED RELEASE_STRING)
	execute_process(
		COMMAND git log -n 1 --format=%h
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		RESULT_VARIABLE GIT_EXIT_ERROR
		ERROR_QUIET
		OUTPUT_VARIABLE GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if (GIT_EXIT_ERROR)
		# We're probably not in a git repo
		set(RELEASE_STRING ${LATEST_RELEASE})
	else (GIT_EXIT_ERROR)
		# We're in a git repo
		execute_process(
			COMMAND git status -s --untracked-files=no
			WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
			OUTPUT_VARIABLE DIRTY
		)
		if ( NOT "${DIRTY}" STREQUAL "" )
			set(DIRTY_FLAG "*")
		else()
			set(DIRTY_FLAG "")
		endif()
		set(RELEASE_STRING "git-${GIT_VERSION}${DIRTY_FLAG}")
	endif (GIT_EXIT_ERROR)
endif(NOT DEFINED RELEASE_STRING)

btbb_pep440_version(LATEST_RELEASE_VERSION "${LATEST_RELEASE}" "0")
btbb_pep440_version(PACKAGE_VERSION "${RELEASE_STRING}" "${LATEST_RELEASE_VERSION}")

# A version the Python tools would reject is a build failure, not something to
# discover at install time.
if(NOT PACKAGE_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*((a|b|rc)[0-9]+)?(\\.post[0-9]+)?(\\.dev[0-9]+)?(\\+[a-z0-9]+(\\.[a-z0-9]+)*)?$")
	message(FATAL_ERROR
		"release string '${RELEASE_STRING}' gives '${PACKAGE_VERSION}', "
		"which is not a PEP 440 version")
endif()
