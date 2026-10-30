libbtbb
=======

libbtbb decodes Bluetooth baseband. It turns the symbols a radio captured into
Bluetooth Classic (BR/EDR) and Low Energy packets, recovers a piconet's address
and clock from them, and writes captures in pcap and pcapng format. The
Ubertooth tools are what use it: `ubertooth-rx`, `ubertooth-btle`,
`ubertooth-scan`, `ubertooth-follow` and the rest link this library. It began
in the GR-Bluetooth project and also decodes symbols captured with
GR-Bluetooth/USRP. This repository carries Wireshark plugins and a Python
keyboard sniffer alongside the library.

This is [SqueakyFloors/libbtbb](https://github.com/SqueakyFloors/libbtbb), a
fork of [greatscottgadgets/libbtbb](https://github.com/greatscottgadgets/libbtbb)
that builds on the original with fixes and a test suite; after this fork's
release its tested fixes are offered back upstream. Bug reports and patches
for this fork go to its own tracker:
https://github.com/SqueakyFloors/libbtbb/issues

It builds on Linux and macOS, and is tested on both: see
[TESTING.md](TESTING.md).


Tested against the Bluetooth specification
==========================================

libbtbb was written and maintained by a small group of volunteers, and it has
served Ubertooth and other projects well for more than a decade. Like many
volunteer projects of its scope, it carried only a few tests, from 2012 and
2015. This fork builds on that work by adding a test suite to the normal build.

There are now 23 tests: 870 assertions in their source, 176,054 assertion
executions per run, driven in large part by the Bluetooth Core Specification
v5.4's own sample data: all 7680 hop frequencies of Vol 2, Part G, Section 2,
the access code, HEC, CRC, FEC and whitening vectors and the complete DH1 and
DM1 packets of Sections 3 to 9, and the Low Energy link layer rules of Vol 6,
Part B. CI builds and runs them on Linux with gcc and macOS with clang under
`-Wall -Wextra -Werror`, and once more on Linux with clang under
AddressSanitizer and UndefinedBehaviorSanitizer. Malformed radio packets are
this library's normal input, so the decoders and the capture writers are fuzzed
as well: five libFuzzer targets, which CI runs for a minute each.

The tests have helped find and fix defects in packet types and capture paths
that are rarely seen on the air. [TESTING.md](TESTING.md) says how to run the
suite, what each test covers, and where its vectors come from; `changelog`
lists the fixes, one entry per release.


Checked on hardware
===================

The Ubertooth fork runs the hardware side. Its bench measured this library
against the pin it replaced, on live Low Energy and Classic (BR/EDR) traffic:

| What | Previous pin | This library |
|---|---|---|
| `ubertooth-rx` peak memory on a Classic link | 140 to 142 MB | 12 MB |
| Non-packet lines mixed into `ubertooth-rx` standard output | 42 to 302 per window | 0 |
| BR/EDR pcapng frames whose header check is recorded | 0 of 1,108 | 105 of 105, all passed |
| Lines on standard error during a Low Energy capture | 5,000 to 10,000 per window | 4 |
| Low Energy capture rate | 323 to 363 packets/s | 336 to 366 packets/s |
| Time to announce a UAP | median 11.3 s | median 1.4 s |

The test plan and the raw output are in
[SqueakyFloors/ubertooth#736](https://github.com/SqueakyFloors/ubertooth/issues/736),
and the rest of the measured wins in
[SqueakyFloors/ubertooth#536](https://github.com/SqueakyFloors/ubertooth/issues/536).
What is still waiting on hardware, and what rests on the unit tests alone, is
in [TESTING.md](TESTING.md#what-only-hardware-can-check).


Concepts
========

What this library decodes, and why it has to work the way it does — address
parts, access code, whitening, clock recovery, the Low Energy connection
request — is explained in [How Bluetooth sniffing works](https://github.com/SqueakyFloors/ubertooth/blob/main/docs/concepts.md).


Capture files
=============

The library writes captures in pcap and pcapng format. A BR/EDR capture has
the `LINKTYPE_BLUETOOTH_BREDR_BB` link type and a Low Energy capture
`LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR`; `lell_pcap_ppi_create_file()` writes the
older `LINKTYPE_PPI` form of a Low Energy pcap as well.

Every record carries the channel, the signal and noise levels, and flags that
say which of its fields hold anything. A BR/EDR record also carries the
reference LAP and UAP the capture tool was following, and flags for what the
decoder verified: whether the header check and the payload CRC were checked,
and whether each passed, so a reader can tell a verified payload from a guess.
A Low Energy record carries the access address the tool was listening for.

A pcapng Low Energy capture also records the parameters of each CONNECT_IND,
the interval, latency, timeout, channel map, and hop increment, as an option
of the interface description block, which is what a reader needs to follow
those connections. The room for the options is set aside when the file is
created, so a long capture can fill it; the writer then returns
`LELL_PCAPNG_CONNECT_INFO_NOT_RECORDED`, positive because the packet itself
was written, and a capture tool warns once and carries on.

Wireshark dissects the Low Energy link type itself. For BR/EDR baseband it
needs the `btbb` and `btbredr` dissectors in `wireshark/`, which build
separately from the library; see [wireshark/README.md](wireshark/README.md). The
format of the pcapng options is specified in the Ubertooth fork's
https://github.com/SqueakyFloors/ubertooth/blob/main/docs/source/bluetooth_captures_PCAP.rst


Python tools
============

An install also carries `btaptap`, a Bluetooth keyboard sniffer, and the
`pcapdump` module it reads and writes capture files with.

Many Bluetooth keyboards work by default in "boot mode", a simplified HID
messaging a PC BIOS can read, and some of them send those messages
unencrypted. `btaptap` extracts the keystrokes from such a capture and writes
them to standard output:

```
$ btaptap -r keyboard.pcap
```

It reads a libpcap capture with `-r`, either baseband frames with the
`DLT_EN10MB` data link layer and the frame 20 bytes in, or HCI traffic with
`DLT_BLUETOOTH_HCI_H4`, and an Ellisys CSV export with `-e`. The Ubertooth
tools write their captures with a Bluetooth baseband data link layer, which
`btaptap` refuses rather than decoding. `python/pcaptools/README` describes
both tools and the rest of the options.

The build installs them when `ENABLE_PYTHON` is on, which it is by default,
and it finds Python 3; the `pcapdump` module needs pip as well, and the
configure step says so when pip is missing. Both can also be run straight out
of `python/pcaptools`, where the module sits beside the script.

An install under a prefix of its own works: `btaptap` looks for `pcapdump` in
the prefix it was installed in, so `cmake --install build --prefix /tmp/pfx`
followed by `/tmp/pfx/bin/btaptap` finds the module, which the interpreter's
own `sys.path` would not.

`python/utils` is development scripts, not part of an install: they generated
the tables that are committed in the library's source. `gen_check_tables.py`
produces the sync word syndrome tables in `lib/src/sw_check_tables.h`,
`gen_barker_correct.py` the barker distance and correction tables,
`encode_sw.py` the sync word of a given LAP, `le_whitening.py` the LE
whitening sequence and each channel's offset into it, and `gen_companies.py`
the company identifiers in `lib/src/companies.c` from the Bluetooth SIG's
published list. The test suite runs the first four and compares what they
print with what is committed, so a table and the script behind it cannot drift
apart; `gen_companies.py` needs the SIG's published file, so the `companies`
test covers that table instead.


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


What a program links
====================

`btbb.h` is the whole interface. The library exports exactly the functions that
header declares and nothing else. Its own helpers stay internal, so a program
that happens to have a function called `hop()` or `DH()` of its own cannot be
put in their place, and there is nothing outside the header for a program to
come to depend on. A program includes `btbb.h` and links `-lbtbb`;
`pkg-config --cflags libbtbb` and `pkg-config --libs libbtbb` give the flags
for the install that is there.

The shared library is `libbtbb.so.1` on Linux and `libbtbb.1.dylib` on macOS.
Its version is 1.3, which `pkg-config --modversion libbtbb` reports. The
soname stays at 1 for as long as a program built against `btbb.h` goes on
working: functions and constants may be added to the header, so the version's
second number moves and the soname does not. Removing one, or changing a
signature, would move the soname to 2 and every program linking libbtbb would
have to be rebuilt. The library's own release string, which
`btbb_get_release()` returns, is a separate thing: it is the git commit the
build came from, or the release it was cut at.

To build the static library instead, or as well, see
[README.cmake](README.cmake). On Windows a program linking the static library
defines `BTBB_STATIC` before including the header, so that the header asks for
neither of the `__declspec` declarations a DLL needs.


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
the `btbb.h` header, `libbtbb.pc`, the `btaptap` script and the `pcapdump`
module it reads captures with. `-DENABLE_PYTHON=OFF` leaves the last two out,
for a package that wants the library alone.
