libbtbb
=======

This is the Bluetooth baseband decoding library, forked from the GR-Bluetooth 
project.  It can be used to extract Bluetooth packet and piconet information 
from Ubertooth devices as well as GR-Bluetooth/USRP.

This code is incomplete, it is still under active development.  Patches and 
bug reports should be submitted to the bug tracker on GitHub:
https://github.com/greatscottgadgets/libbtbb/issues

This software has been developed and tested on Linux, it should work on other 
platforms but this has yet to be tested.


Tested against the Bluetooth specification
==========================================

Before this fork the library had three test programs from 2012 and 2015 that
only an old `tests/Makefile` built, one of which had not compiled in years, and
a Travis configuration that no longer ran. Nothing checked a change.

It now has 12 tests in the build: 772 assertions in their source, 175,934
assertion executions per run, driven in large part by the Bluetooth Core
Specification v5.4's own sample data — all 7680 hop frequencies of Vol 2,
Part G, Section 2, the access code, HEC, CRC, FEC and whitening vectors and the
complete DH1 and DM1 packets of Sections 3 to 9, and the Low Energy link layer
rules of Vol 6, Part B. CI builds and runs them on Linux with gcc and macOS
with clang under `-Wall -Wextra -Werror`, and again under AddressSanitizer and
UndefinedBehaviorSanitizer.

They found 51 defects: AUX1, EV3, EV4, EV5 and DV packets never produced a
payload at all, every connection parameter a PCAPNG file recorded for a
CONNECT_IND belonged to another part of the packet, and buffers were written
and read past their ends. [TESTING.md](TESTING.md) has what each test covers
and where its vectors come from, a row per defect with what a user saw, and the
numbers the fixes moved, measured before and after.


Concepts
========

What this library decodes, and why it has to work the way it does — address
parts, access code, whitening, clock recovery, the Low Energy connection
request — is explained in [How Bluetooth sniffing works](https://github.com/SqueakyFloors/ubertooth/blob/main/docs/concepts.md).


Build Instructions
==================

Libbtbb can be built and installed as follows:
```
$ mkdir build
$ cd build
$ cmake ..
$ make
$ sudo make install
```

This will install the library to /usr/local/lib and the headers to 
/usr/local/include, to install to different locations use:
```
$ cmake -DINSTALL_DIR=/path/to/install -DINCLUDE_DIR=/path/to/include ..
```

If you have previous versions of libbtbb, libubertooth or the Ubertooth tools
installed, you can use the cleanup script to remove them:
```
$ sudo cmake/cleanup.sh -d
```

To list the installed files without removing them, use:
```
$ cmake/cleanup.sh
```
