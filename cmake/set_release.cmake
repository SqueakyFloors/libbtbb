# The version this build reports, worked out from what git says the checkout
# is, and the two things that follow from it:
#
#   RELEASE_STRING  the release the library reports from btbb_get_release():
#                   the release tag the checkout is at ("2020-12-R1"), or the
#                   nearest release tag and how many commits are past it
#                   ("2020-12-R1+22"), with a "*" where the checkout has
#                   uncommitted changes. A release name rather than a commit,
#                   because that is what a user reading a version wants to
#                   know. The commit is still recorded, below.
#   BTBB_COMMIT     the commit the library was built from, in full, with the
#                   same "*". btbb_get_commit() returns it. A program that
#                   pins a particular libbtbb needs to tell one build of it
#                   from another, which a release name and a commit count
#                   cannot do.
#
# Either can be given instead: cmake -DRELEASE_STRING="your version"
# -DBTBB_COMMIT=<commit>.
#
# Three sources, in this order:
#
#   a git checkout    git says which release it is and what has happened
#                     since, as described above.
#   a source archive  git archive filled the release into RELEASE-VERSION
#                     when the archive was made; see that file. No commit is
#                     recorded, because the archive does not carry one.
#   neither           the release is "unknown" and so is the commit. That is
#                     a checkout with no release tag in reach -- one fetched
#                     without tags, a shallow one, a history with none yet --
#                     and sources copied out of any archive by hand. The
#                     commit is still recorded where git could name it.
#
# A checkout with no release tag could report the last release the sources
# know about instead, but the distance from it would be unknown, and no
# version kept in this file can say which release a build really came from:
# whoever edited it last decided that, not the release that was tagged.
#
# It also sets PACKAGE_VERSION, the same version expressed the way PEP 440
# requires, for the Python tools. See btbb_pep440_version() below.

# A release tag is YYYY-MM-Rn, or YYYY-MM-RCn and YYYY-MM-DEVn for the
# candidate and development tags that come before one. Three globs rather
# than one YYYY-MM-* , so that another tag in the checkout -- a packager's, a
# developer's own, a dated snapshot -- is not read as a release. git takes a
# --match for each.
set(BTBB_RELEASE_TAG_GLOBS
	"[0-9][0-9][0-9][0-9]-[0-9][0-9]-R[0-9]*"
	"[0-9][0-9][0-9][0-9]-[0-9][0-9]-RC[0-9]*"
	"[0-9][0-9][0-9][0-9]-[0-9][0-9]-DEV[0-9]*")

# The same list as git arguments.
set(BTBB_RELEASE_TAG_MATCH "")
foreach(glob IN LISTS BTBB_RELEASE_TAG_GLOBS)
	list(APPEND BTBB_RELEASE_TAG_MATCH --match "${glob}")
endforeach()

# One release name taken from a `git describe --tags` line, or empty where the
# line names no release:
#
#   2020-12-R1               ->  2020-12-R1
#   2020-12-R1-22-gf0fe176   ->  2020-12-R1+22
#
# git appends "-<commits>-g<commit>" where the commit described is past the
# tag. A release name has dashes of its own, so the count is taken off the end
# rather than the name read out of the front. What is left has to be a release
# name: the globs above cannot say "digits and then nothing", so a tag like
# "2026-11-R1-local" reaches here and is not a release.
function(btbb_release_from_describe out describe)
	string(STRIP "${describe}" text)
	set(count "")
	if(text MATCHES "^(.+)-([0-9]+)-g[0-9a-f]+$")
		set(text "${CMAKE_MATCH_1}")
		set(count "+${CMAKE_MATCH_2}")
	endif()
	if(NOT text MATCHES "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-(R|RC|DEV)[0-9]+$")
		set(${out} "" PARENT_SCOPE)
		return()
	endif()
	set(${out} "${text}${count}" PARENT_SCOPE)
endfunction()

# The release the file at <path> carries, or empty where it carries none.
#
# That file is RELEASE-VERSION, and git archive fills the release into it when
# an archive is made. In a git checkout nothing has filled it in and the line
# is still the placeholder, which names no release: a checkout's version comes
# from git, which is asked first.
#
# Comments and blank lines are skipped, so the file can explain itself.
function(btbb_release_from_file out path)
	set(${out} "" PARENT_SCOPE)
	if(NOT EXISTS "${path}")
		return()
	endif()
	file(STRINGS "${path}" lines)
	foreach(line IN LISTS lines)
		string(STRIP "${line}" line)
		if(line STREQUAL "" OR line MATCHES "^#")
			continue()
		endif()
		if(NOT line MATCHES "^\\$Format:")
			btbb_release_from_describe(release "${line}")
			set(${out} "${release}" PARENT_SCOPE)
		endif()
		return()
	endforeach()
endfunction()

# What the git checkout in <dir> is: its release name in <release_out> and its
# commit in <commit_out>, both empty where there is no git history to read.
# Asked of one directory rather than of the caller's working directory, so the
# answer is about this checkout and so the tests below can ask about a
# checkout of their own.
function(btbb_version_from_git release_out commit_out dir)
	set(${release_out} "" PARENT_SCOPE)
	set(${commit_out} "" PARENT_SCOPE)

	# Does the checkout git would answer about hold these sources at all?
	# git looks up the directory tree, so sources unpacked inside an
	# unrelated repository -- a vendored copy, a tarball in someone's home
	# -- would otherwise be given that repository's release and commit.
	# Whether it tracks anything in this directory is what tells the two
	# apart, and asks about no file by name, so moving this file does not
	# quietly turn the answer off.
	execute_process(
		COMMAND git ls-files --error-unmatch .
		WORKING_DIRECTORY "${dir}"
		RESULT_VARIABLE not_ours
		OUTPUT_QUIET
		ERROR_QUIET
	)
	if(not_ours)
		return()
	endif()

	execute_process(
		COMMAND git rev-parse HEAD
		WORKING_DIRECTORY "${dir}"
		RESULT_VARIABLE no_history
		ERROR_QUIET
		OUTPUT_VARIABLE commit
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	# Not a git checkout, or a checkout with no commit in it: either way
	# there is nothing here to report.
	if(no_history)
		return()
	endif()

	# A git that cannot answer here leaves the flag off rather than failing
	# the configure step: the release name is right either way, and the flag
	# is a courtesy to whoever reads it.
	execute_process(
		COMMAND git status -s --untracked-files=no
		WORKING_DIRECTORY "${dir}"
		ERROR_QUIET
		OUTPUT_VARIABLE changes
	)
	set(flag "")
	if(NOT "${changes}" STREQUAL "")
		set(flag "*")
	endif()

	# Exactly a release tag: that release, with nothing after it.
	execute_process(
		COMMAND git describe --tags --exact-match
		        ${BTBB_RELEASE_TAG_MATCH} HEAD
		WORKING_DIRECTORY "${dir}"
		RESULT_VARIABLE not_tagged
		ERROR_QUIET
		OUTPUT_VARIABLE tag
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(NOT not_tagged)
		set(${release_out} "${tag}${flag}" PARENT_SCOPE)
		set(${commit_out} "${commit}${flag}" PARENT_SCOPE)
		return()
	endif()

	# Otherwise the release this one comes after, and how far past it this
	# checkout is. --abbrev=0 asks for the tag alone, without the commit
	# git would otherwise append.
	execute_process(
		COMMAND git describe --tags --abbrev=0
		        ${BTBB_RELEASE_TAG_MATCH} HEAD
		WORKING_DIRECTORY "${dir}"
		RESULT_VARIABLE no_tag
		ERROR_QUIET
		OUTPUT_VARIABLE tag
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(no_tag)
		# No release tag to count from: a clone made without tags, a
		# shallow one, or a history that has none yet. There is no
		# release to name, and counting from the start of history would
		# not be the distance from one -- a shallow clone would answer
		# that with its own depth, and a clone of one commit saying "+1"
		# would read exactly like a real build one commit past a
		# release. The caller reports the release as unknown; the commit
		# is still here, and still identifies the build.
		set(${release_out} "" PARENT_SCOPE)
		set(${commit_out} "${commit}${flag}" PARENT_SCOPE)
		return()
	endif()
	execute_process(
		COMMAND git rev-list --count "${tag}..HEAD"
		WORKING_DIRECTORY "${dir}"
		RESULT_VARIABLE no_count
		ERROR_QUIET
		OUTPUT_VARIABLE count
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(no_count)
		# The tag is the whole answer: better an honest release name with
		# no distance than no version at all.
		set(${release_out} "${tag}${flag}" PARENT_SCOPE)
	else()
		set(${release_out} "${tag}+${count}${flag}" PARENT_SCOPE)
	endif()
	set(${commit_out} "${commit}${flag}" PARENT_SCOPE)
endfunction()

# The local version PEP 440 allows after a "+": its parts separated by dots,
# or nothing at all where there are none.
function(btbb_local_version out parts)
	if("${parts}" STREQUAL "")
		set(${out} "" PARENT_SCOPE)
		return()
	endif()
	string(REPLACE ";" "." joined "${parts}")
	set(${out} "+${joined}" PARENT_SCOPE)
endfunction()

# Map a release string onto a PEP 440 public version with an optional local
# version, which is what Python packaging tools accept:
#
#   2020-12-R1       -> 2020.12.1
#   2018-06-R1       -> 2018.6.1    (leading zeros are not PEP 440 normal form)
#   2026-11-RC1      -> 2026.11.0rc1
#   2026-11-DEV1     -> 2026.11.0.dev1
#   2020-12-R1+22    -> 2020.12.1+22
#   2020-12-R1+22*   -> 2020.12.1+22.dirty
#   2020-12-R1*      -> 2020.12.1+dirty
#   2020-12-R1+fork1 -> 2020.12.1+fork1
#   unknown          -> 0+unknown      (with fallback_public "0")
#
# A release string in no recognized form takes fallback_public as its public
# version and carries the string itself in the local version, with anything
# PEP 440 does not allow there replaced by a dot. The caller passes "0",
# because a version that names no release should not be read as one.
function(btbb_pep440_version out release fallback_public)
	set(rest "${release}")
	set(local_parts "")

	# The "*" and whatever follows a "+" say nothing about which release
	# this is, so they are taken off first and the release itself is matched
	# on its own. They become local version parts, in the order they were
	# written.
	if(rest MATCHES "^(.*)\\*$")
		set(rest "${CMAKE_MATCH_1}")
		set(local_parts "dirty")
	endif()
	if(rest MATCHES "^([^+]*)\\+([a-zA-Z0-9]+)$")
		set(rest "${CMAKE_MATCH_1}")
		string(TOLOWER "${CMAKE_MATCH_2}" suffix)
		list(INSERT local_parts 0 "${suffix}")
	endif()

	if(rest MATCHES "^([0-9]+)-0*([0-9]+)-([A-Za-z]+)0*([0-9]+)$")
		set(year "${CMAKE_MATCH_1}")
		set(month "${CMAKE_MATCH_2}")
		string(TOUPPER "${CMAKE_MATCH_3}" kind)
		set(number "${CMAKE_MATCH_4}")
		set(public "")
		if(kind STREQUAL "R")
			set(public "${year}.${month}.${number}")
		elseif(kind STREQUAL "RC")
			# PEP 440 spells a release candidate as a pre-release of
			# the version it is a candidate for, which sorts before
			# that version rather than after it.
			set(public "${year}.${month}.0rc${number}")
		elseif(kind STREQUAL "DEV")
			set(public "${year}.${month}.0.dev${number}")
		endif()
		if(NOT public STREQUAL "")
			btbb_local_version(local "${local_parts}")
			set(${out} "${public}${local}" PARENT_SCOPE)
			return()
		endif()
	endif()

	string(TOLOWER "${rest}" local)
	string(REGEX REPLACE "[^a-z0-9]+" "." local "${local}")
	string(REGEX REPLACE "^\\.+|\\.+$" "" local "${local}")
	if(local STREQUAL "")
		set(local "unknown")
	endif()
	list(INSERT local_parts 0 "${local}")
	btbb_local_version(local "${local_parts}")
	set(${out} "${fallback_public}${local}" PARENT_SCOPE)
endfunction()

# An override given as an empty value says nothing, so it is read as no
# override: btbb_get_release() and btbb_get_commit() always have an answer.
if(DEFINED RELEASE_STRING AND "${RELEASE_STRING}" STREQUAL "")
	unset(RELEASE_STRING)
	# A -D on the command line is a cache entry, which the line above does
	# not reach.
	unset(RELEASE_STRING CACHE)
endif()
if(DEFINED BTBB_COMMIT AND "${BTBB_COMMIT}" STREQUAL "")
	unset(BTBB_COMMIT)
	unset(BTBB_COMMIT CACHE)
endif()

if(NOT DEFINED RELEASE_STRING OR NOT DEFINED BTBB_COMMIT)
	btbb_version_from_git(GIT_RELEASE GIT_COMMIT "${CMAKE_CURRENT_LIST_DIR}")
endif()

if(NOT DEFINED RELEASE_STRING)
	if(NOT "${GIT_RELEASE}" STREQUAL "")
		set(RELEASE_STRING "${GIT_RELEASE}")
	else()
		# No git answer: the release an archive carries, if these
		# sources came out of one.
		btbb_release_from_file(ARCHIVE_RELEASE
			"${CMAKE_CURRENT_LIST_DIR}/../RELEASE-VERSION")
		if(NOT "${ARCHIVE_RELEASE}" STREQUAL "")
			set(RELEASE_STRING "${ARCHIVE_RELEASE}")
		else()
			set(RELEASE_STRING "unknown")
		endif()
	endif()
endif()

if(NOT DEFINED BTBB_COMMIT)
	if("${GIT_COMMIT}" STREQUAL "")
		# Nothing to record. btbb_get_commit() answers "unknown", which
		# is what a build from a source archive has to say.
		set(BTBB_COMMIT "unknown")
	else()
		set(BTBB_COMMIT "${GIT_COMMIT}")
	endif()
endif()

btbb_pep440_version(PACKAGE_VERSION "${RELEASE_STRING}" "0")

# A version the Python tools would reject is a build failure, not something to
# discover at install time.
if(NOT PACKAGE_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*((a|b|rc)[0-9]+)?(\\.post[0-9]+)?(\\.dev[0-9]+)?(\\+[a-z0-9]+(\\.[a-z0-9]+)*)?$")
	message(FATAL_ERROR
		"release string '${RELEASE_STRING}' gives '${PACKAGE_VERSION}', "
		"which is not a PEP 440 version")
endif()
