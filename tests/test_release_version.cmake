# The version cmake/set_release.cmake works out for a checkout, and the PEP
# 440 version it maps that onto.
# Run as:
#   cmake -DSET_RELEASE_CMAKE=<path> -DWORK_DIR=<scratch dir>
#         -P test_release_version.cmake

if(NOT DEFINED SET_RELEASE_CMAKE OR NOT DEFINED WORK_DIR)
	message(FATAL_ERROR "SET_RELEASE_CMAKE and WORK_DIR must be set")
endif()

# Including the file with both answers given defines its functions and asks
# git nothing about this checkout: every case below says which directory it is
# asking about.
set(RELEASE_STRING "2020-12-R1")
set(BTBB_COMMIT "unknown")
include("${SET_RELEASE_CMAKE}")

set(failures 0)

function(fail)
	message(SEND_ERROR ${ARGV})
	math(EXPR failures "${failures} + 1")
	set(failures ${failures} PARENT_SCOPE)
endfunction()

# Scratch checkouts of their own, so the cases are about a history this test
# made rather than about whatever the source tree happens to be.
#
# The identity is given per command and the commits are not signed, so the
# test does not depend on the git configuration of whoever runs it, and does
# not read a signing key.
find_program(GIT git)
if(NOT GIT)
	message(FATAL_ERROR "git was not found, so there is no checkout to read")
endif()

set(git_id -c user.name=test -c user.email=test@example.invalid
           -c commit.gpgsign=false -c tag.gpgsign=false)

function(run_git dir)
	execute_process(
		COMMAND "${GIT}" ${git_id} ${ARGN}
		WORKING_DIRECTORY "${dir}"
		RESULT_VARIABLE status
		OUTPUT_QUIET
		ERROR_VARIABLE error
	)
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "git ${ARGN} in ${dir} failed: ${error}")
	endif()
endfunction()

# One commit on top of whatever is there, with a file whose contents change so
# that no two commits are the same.
function(commit_file dir text)
	file(WRITE "${dir}/file.txt" "${text}\n")
	run_git("${dir}" add file.txt)
	run_git("${dir}" commit -q -m "${text}")
endfunction()

function(new_repo dir)
	file(REMOVE_RECURSE "${dir}")
	file(MAKE_DIRECTORY "${dir}")
	run_git("${dir}" init -q)
endfunction()

# What the file says the checkout in <dir> is, against what it should be. The
# commit is checked against the checkout's own HEAD rather than a fixed value,
# because a test that made the history cannot know the hash in advance.
function(expect_version dir what expected_release expected_commit)
	btbb_version_from_git(release commit "${dir}")
	if(NOT release STREQUAL expected_release)
		fail("${what}: release '${release}', expected '${expected_release}'")
	endif()
	if(NOT commit STREQUAL expected_commit)
		fail("${what}: commit '${commit}', expected '${expected_commit}'")
	endif()
	set(failures ${failures} PARENT_SCOPE)
endfunction()

function(head_commit out dir)
	execute_process(
		COMMAND "${GIT}" rev-parse HEAD
		WORKING_DIRECTORY "${dir}"
		OUTPUT_VARIABLE sha
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(${out} "${sha}" PARENT_SCOPE)
endfunction()

set(tagged "${WORK_DIR}/tagged")
new_repo("${tagged}")
commit_file("${tagged}" "first")
run_git("${tagged}" tag 2026-11-RC1)
head_commit(sha "${tagged}")

# HEAD is the release tag itself: that release, and nothing after it.
expect_version("${tagged}" "a release tag" "2026-11-RC1" "${sha}")

# The same checkout with uncommitted changes in it.
file(APPEND "${tagged}/file.txt" "edited\n")
expect_version("${tagged}" "a release tag, modified"
	"2026-11-RC1*" "${sha}*")
run_git("${tagged}" checkout -q -- file.txt)

# Commits past the tag: the release it comes after, and how far past.
commit_file("${tagged}" "second")
commit_file("${tagged}" "third")
head_commit(sha "${tagged}")
expect_version("${tagged}" "two commits past a release tag"
	"2026-11-RC1+2" "${sha}")

# The same, with uncommitted changes.
file(APPEND "${tagged}/file.txt" "edited\n")
expect_version("${tagged}" "two commits past a release tag, modified"
	"2026-11-RC1+2*" "${sha}*")
run_git("${tagged}" checkout -q -- file.txt)

# A tag that is not a release tag is not read as one: the checkout counts from
# the latest release this source knows about instead.
set(untagged "${WORK_DIR}/untagged")
new_repo("${untagged}")
commit_file("${untagged}" "first")
run_git("${untagged}" tag wip-something)
head_commit(sha "${untagged}")
expect_version("${untagged}" "no release tag reachable"
	"${LATEST_RELEASE}+1" "${sha}")

# No git history at all, which is what a source archive is. The ceiling stops
# git walking up into the repository this test is part of, so the directory is
# as bare to git as an unpacked archive.
set(plain "${WORK_DIR}/plain")
file(REMOVE_RECURSE "${plain}")
file(MAKE_DIRECTORY "${plain}")
set(ENV{GIT_CEILING_DIRECTORIES} "${WORK_DIR}")
expect_version("${plain}" "no git history" "" "")
unset(ENV{GIT_CEILING_DIRECTORIES})

# Release string -> expected PACKAGE_VERSION. The expectations spell out the
# version LATEST_RELEASE maps to, so moving that release moves these too.
set(cases
	"2020-12-R1|2020.12.1"
	"2018-06-R1|2018.6.1"
	"2015-09-R2|2015.9.2"
	"2020-12-R1+fork1|2020.12.1+fork1"
	"2020-12-R1+22|2020.12.1+22"
	"2020-12-R1+22*|2020.12.1+22.dirty"
	"2020-12-R1*|2020.12.1+dirty"
	"2026-11-RC1|2026.11.0rc1"
	"2026-11-RC1+3|2026.11.0rc1+3"
	"2026-11-DEV1|2026.11.0.dev1"
	"2026-11-DEV2+7*|2026.11.0.dev2+7.dirty"
	"git-f0fe176|2020.12.1+git.f0fe176"
	"git-f0fe176*|2020.12.1+git.f0fe176.dirty"
	"git-F0FE176|2020.12.1+git.f0fe176"
	"my local build|2020.12.1+my.local.build"
	"-|2020.12.1+unknown"
)

foreach(case IN LISTS cases)
	string(REPLACE "|" ";" parts "${case}")
	list(GET parts 0 release)
	list(GET parts 1 expected)

	set(RELEASE_STRING "${release}")
	unset(PACKAGE_VERSION)
	include("${SET_RELEASE_CMAKE}")
	unset(RELEASE_STRING)

	if(NOT PACKAGE_VERSION STREQUAL expected)
		fail("'${release}' gave '${PACKAGE_VERSION}', expected '${expected}'")
	endif()
endforeach()

if(failures GREATER 0)
	message(FATAL_ERROR "${failures} case(s) failed")
endif()
list(LENGTH cases case_count)
message(STATUS "the checkout cases and all ${case_count} version cases passed")
