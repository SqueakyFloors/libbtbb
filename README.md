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

libbtbb was written and maintained by a small group of volunteers, and it has
served Ubertooth and other projects well for more than a decade. Like many
volunteer projects of its scope, it carried only a few tests, from 2012 and
2015. This fork builds on that work by adding a test suite to the normal build.

There are now 19 tests: 870 assertions in their source, 176,054 assertion
executions per run, driven in large part by the Bluetooth Core Specification
v5.4's own sample data: all 7680 hop frequencies of Vol 2, Part G, Section 2,
the access code, HEC, CRC, FEC and whitening vectors and the complete DH1 and
DM1 packets of Sections 3 to 9, and the Low Energy link layer rules of Vol 6,
Part B. CI builds and runs them on Linux with gcc and macOS with clang under
`-Wall -Wextra -Werror`, and again under AddressSanitizer and
UndefinedBehaviorSanitizer.

The tests have helped find and fix 72 issues, many in packet types and capture
paths that are rarely seen on the air. [TESTING.md](TESTING.md) describes what
each test covers and where its vectors come from, lists each fix with what a
user would notice, and gives before and after measurements.


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
/usr/local/include. To install somewhere else, give the prefix both go under:
```
$ cmake -DCMAKE_INSTALL_PREFIX=/path/to/install ..
```

The library goes in `lib` under that prefix. `-DLIB_SUFFIX=64` puts it in
`lib64`, as 64-bit builds of some distributions expect, and `LIB_SUFFIX` may
carry a path: `-DLIB_SUFFIX=/x86_64-linux-gnu` gives
`lib/x86_64-linux-gnu`. `libbtbb.pc` follows whichever is chosen, so
`pkg-config --libs libbtbb` names the directory the library is really in.

If you have previous versions of libbtbb, libubertooth or the Ubertooth tools
installed, you can use the cleanup script to remove them:
```
$ sudo cmake/cleanup.sh -d
```

To list the installed files without removing them, use:
```
$ cmake/cleanup.sh
```
