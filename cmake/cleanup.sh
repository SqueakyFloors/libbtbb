#!/bin/bash

# A quick and dirty script to remove old installs of
# libbtbb, libubertooth and associated Ubertooth tools
# Copyright 2014 Dominic Spill
# License: GPL v2

FIND=`which find`

INSTALL_DIRS="/usr /usr/local"

LIBS="btbb ubertooth"

HEADERS="btbb.h \
         bluetooth_packet.h \
         bluetooth_piconet.h \
         bluetooth_le_packet.h \
         ubertooth_interface.h \
         ubertooth_control.h \
         ubertooth.h \
		 "

if [ "$1" == "-d" ]
then
	EXEC="-print -exec rm -f {} ;"
	echo "Deleting previous installs:"
else
	EXEC=-print
	echo 'Installed files, use "sudo cleanup.sh -d" to delete these files'
fi

# A library can be in lib, in lib64, or a directory deeper than that in a
# multiarch lib/<triplet>, so all three are searched. The pkg-config file
# goes with it: one left behind from an install in another directory hands a
# build a -L for a directory with no library in it. A directory that does not
# exist is skipped rather than reported by find as missing.
for dir in $INSTALL_DIRS; do
	for libdir in ${dir}/lib ${dir}/lib64; do
		[ -d "$libdir" ] || continue
		for lib in $LIBS; do
			$FIND ${libdir} -maxdepth 2 \
				\( -name "lib$lib.so*" -o -name "lib$lib.a" \
				   -o -name "lib$lib*.dylib" \) $EXEC
			$FIND ${libdir} -maxdepth 3 -path "*/pkgconfig/lib$lib.pc" $EXEC
		done
	done
	# Two directories deep for the header as well: an install can be given
	# a directory of its own for it, as include/libbtbb.
	for header in $HEADERS; do
		$FIND ${dir}/include -maxdepth 2 -name "$header" $EXEC
	done
	$FIND ${dir}/bin -maxdepth 1 -name "ubertooth-*" $EXEC
done
