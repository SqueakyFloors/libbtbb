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

There are now 21 tests: 870 assertions in their source, 176,054 assertion
executions per run, driven in large part by the Bluetooth Core Specification
v5.4's own sample data: all 7680 hop frequencies of Vol 2, Part G, Section 2,
the access code, HEC, CRC, FEC and whitening vectors and the complete DH1 and
DM1 packets of Sections 3 to 9, and the Low Energy link layer rules of Vol 6,
Part B. CI builds and runs them on Linux with gcc and macOS with clang under
`-Wall -Wextra -Werror`, and again under AddressSanitizer and
UndefinedBehaviorSanitizer. Malformed radio packets are this library's normal
input, so the decoders and the capture writers are fuzzed as well: five
libFuzzer targets, which CI runs for a minute each.

The tests have helped find and fix 75 issues, many in packet types and capture
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

This installs under /usr/local. To install somewhere else, give the prefix
everything goes under:
```
$ cmake -DCMAKE_INSTALL_PREFIX=/path/to/install ..
```

Where it installs
-----------------

The header goes in `include` under the prefix, and the library in the
directory the system keeps libraries in, which CMake's `GNUInstallDirs`
works out:

| System | Library directory under the prefix |
|---|---|
| Debian, Ubuntu and the like, prefix `/usr` | `lib/x86_64-linux-gnu`, or the triplet of the architecture being built |
| Debian, Ubuntu and the like, any other prefix | `lib` |
| Other 64-bit Linux: Fedora, RHEL, SUSE, Gentoo and the rest | `lib64`, whatever the prefix |
| Arch, Alpine, a conda prefix, macOS, 32-bit Linux, a cross build | `lib` |

The rule is CMake's own and has changed a little between CMake versions, so
the configure step prints the two directories it settled on:

```
-- Installing the library to /usr/local/lib64 and the header to /usr/local/include
```

To choose the directory yourself, set it:
```
$ cmake -DCMAKE_INSTALL_LIBDIR=lib64 ..
$ cmake -DCMAKE_INSTALL_INCLUDEDIR=include/libbtbb ..
```

`LIB_SUFFIX` is the older way of asking for the same thing, and still decides
where it is set to something: `-DLIB_SUFFIX=64` installs the library to
`lib64` and `-DLIB_SUFFIX=/x86_64-linux-gnu` to `lib/x86_64-linux-gnu`,
whatever `CMAKE_INSTALL_LIBDIR` says, and the configure step says which of
the two it followed. It names the library's directory only; the header
follows `CMAKE_INSTALL_INCLUDEDIR` either way. An empty `-DLIB_SUFFIX=` asks
for no suffix, which says nothing about which directory is wanted, so
`CMAKE_INSTALL_LIBDIR` stays in charge there.

The configure step reports the two directories it will use, and `libbtbb.pc`
names them, so `pkg-config --libs libbtbb` and `pkg-config --cflags libbtbb`
point a program at the files that were really installed.

Either directory may be an absolute path, which is installed to exactly that
wherever the prefix is. One that leads out of the prefix, as `../elsewhere`,
is refused while configuring: `libbtbb.pc` could not then say where the files
had gone.

If you have previous versions of libbtbb, libubertooth or the Ubertooth tools
installed, you can use the cleanup script to remove them:
```
$ sudo cmake/cleanup.sh -d
```

To list the installed files without removing them, use:
```
$ cmake/cleanup.sh
```


Packaging
=========

This repository carries no packaging files. Distributions, MacPorts and
Homebrew each keep their own, next to the version they ship. The repository
used to carry an RPM spec, a MacPorts Portfile and a Homebrew formula from
earlier releases; they are gone, so nothing here describes a library the
build does not produce.

To package this library, follow [Build Instructions](#build-instructions)
above, and [Where it installs](#where-it-installs) for the directories an
install uses and how to choose them. An install writes the shared library,
the `btbb.h` header, `libbtbb.pc` and the `btaptap` script.
