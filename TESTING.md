# Testing libbtbb

libbtbb was built by volunteers and has decoded Bluetooth traffic for Ubertooth
and other projects for many years. It came with a few test programs; this fork
adds to that foundation with a test suite in the normal build.

There are 23 tests, and six more where the compiler can build a fuzz target,
driven in large part by the Bluetooth Core Specification's own published sample
data; the vectors of the original test programs were kept and ported.
Malformed radio packets are this library's normal input, so the decoders,
clock and UAP recovery, and the capture writers are fuzzed as well. The fixes the suite found and holds
down are listed in [CHANGELOG.md](CHANGELOG.md), one entry per release, with
the pull request that made each one.

## How to run them

```
$ cmake -S . -B build -DENABLE_PYTHON=OFF -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -Wno-error=implicit-fallthrough"
$ cmake --build build -j8
$ ctest --test-dir build
```

That leaves the python tools out, and with them the two tests that cover
`btaptap`. Drop `-DENABLE_PYTHON=OFF` for those, or run the keystroke one on
its own: `python3 python/pcaptools/tests/test_btaptap.py`.

For the sanitizer run, which is what CI's fourth job does, configure a second
tree with `-fsanitize=address,undefined -fno-sanitize-recover=all` added to
`CMAKE_C_FLAGS`.

One test, `ctest -R piconet --output-on-failure`. Each test is also a plain
program you can run on its own: `build/tests/test_piconet`. A failure prints
`FAIL <file>:<line>: <condition>` and the program exits non-zero, so the line
that failed names the vector.

CI (`.github/workflows/ci.yml`) runs five jobs: Ubuntu 24.04 with gcc and
macOS 15 with clang, both with `-Wall -Wextra -Werror` and an install into a
temporary prefix; Ubuntu 24.04 building the static library on its own; Ubuntu
24.04 with clang under both sanitizers; and Ubuntu 24.04 with clang running
each fuzz target for a minute.

## Fuzzing

The decoders take whatever the radio gave them, so six libFuzzer targets in
`tests/fuzz/` feed them bytes: the access code search, the BR/EDR header and
payload decoders with the packet dump, clock and UAP recovery over a run of
packets, the LE decoder with its dump, the PCAP writers, and the PCAPNG
writers. The writer targets decode a frame first and write the packet that
came out, which is what a capture tool does. The recovery target delivers a
whole run of packets to one piconet, because recovering the UAP, narrowing the
candidate clocks, and following the piconet all need more than one packet; two
of its seeds are runs of the specification's DH1 packet on the channels that
piconet's hop sequence really puts it on, which is what carries a run as far
as the follow.

The recovery target is much slower than the other five, because choosing
candidate clocks walks the whole CLK7-27 space once. An option bit in its
input decides whether an input starts that search, so only the inputs pointed
at it pay for it.

Each target is built only where the compiler can build a libFuzzer program, so
a gcc build, or one with the clang Apple ships, is unaffected and simply has no
fuzz targets. Under a clang that can:

```
$ CC=clang cmake -S . -B build-fuzz -DENABLE_PYTHON=OFF -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
$ cmake --build build-fuzz -j8
$ ./build-fuzz/tests/fuzz/fuzz_le -max_total_time=600 -max_len=512 \
        -close_fd_mask=1 tests/fuzz/corpus/le
```

`-close_fd_mask=1` closes the dumps, which are the point of those functions and
not of a fuzzing run, and leaves standard error open so that a sanitizer's
report of a real failure is in the log. `-close_fd_mask=3` closes the decoders'
complaints about bad packets too, and needs the sanitizers' `log_path`
(`ASAN_OPTIONS=log_path=/tmp/fuzz-asan`,
`UBSAN_OPTIONS=print_stacktrace=1:log_path=/tmp/fuzz-ubsan`) to hold the report.

A run that finds something writes the input it stopped on to a file, which the
target replays if given that file instead of a directory. Add the file to the
target's seed corpus in `tests/fuzz/corpus/` so that the input stays covered.
That corpus is the sample data the rest of the suite uses - the specification's
DH1 packet, the frames of the LE print test, and a data channel PDU claiming
all 255 octets - each with the parameter bytes the target reads in front of it,
which the comment at the top of each target lists. `ctest` replays the corpus
through every target, so the targets keep working rather than merely building.

## What each test covers

Several tests reach functions that are internal to the library, so they
`#include` the translation unit under test rather than linking the library:
that reaches a `static` function, which is not even a name the linker sees,
without widening what the library offers. `tests/CMakeLists.txt` says which
tests do it.

| Test | Covers | Where the vectors come from |
|---|---|---|
| `spec_bredr` | BR/EDR baseband: access codes and sync words, the header HEC, the payload CRC, both FEC codes, the whitening sequence, complete DH1 and DM1 packets, a payload at the longest length every ACL, SCO, eSCO, DV, AUX1 and FHS type allows, truncated captures, uncorrectable FEC blocks, and UAP and CLK6 recovery | Bluetooth Core Specification v5.4, Vol 2, Part G "Sample Data", Sections 3 to 9, with the definitions of Vol 2, Part B. Where the specification publishes no vector for something the library still has to get right, the test builds the packet with its own `spec_hec()`, `spec_whiten()` and `spec_fec23()`, each pinned by published sample data before it is used, so a built packet is the specification's answer and not libbtbb's |
| `spec_hop` | Connection-state hop selection: the basic kernel, the AFH re-mapping function, the AFH same channel mechanism, and the Nmin fallback | Vol 2, Part G, Section 2: three addresses, the basic sequence and four channel maps each, 512 clock values per table. 7680 vectors, transcribed from the published tables |
| `spec_le` | Low Energy link layer: physical channel indices, advertising and data PDU headers, every length the one-octet field can claim up to 255, including each of the four a 260-byte buffer could not hold, and a 255-octet PDU on a primary advertising channel and on two secondary ones, and the access address rules | Vol 6, Part B, Sections 1.4.1, 2.1.2, 2.3 and 2.4. The CRC-24, the per-channel whitening and the channel selection algorithms are not in this library, so they have no vectors here |
| `hop` | `single_hop()` with and without AFH, and `get_hop_pattern()`'s streams and cache | The Ubertooth project's `firmware/tests/test_bluetooth_hop.c`, an independent transcription of Vol 2, Part B, Section 2.6 computed outside either code base. Using them here pins the host's predictions and the channels the board tunes to against one source of truth |
| `piconet` | Clock recovery (hop reversal): the CLK1-27 candidate list, winnowing it against observed hops, the AFH detector, the confirmation rule, and giving up a clock whose headers keep failing without choosing it again. Also the piconet survey: reading a piconet by its LAP while the survey runs, a LAP the survey has not heard, what a destructive walk leaves behind, and a survey that cannot allocate | The hops are produced by `single_hop()`, which `hop` and `spec_hop` pin against the specification, so a "truth" piconet standing in for the device under observation hops the way a real one does |
| `clock_sweep` | What clock recovery does over a range of true-clock values: how often it locks the piconet's clock, a wrong one or none at all; what following each lock does with it; how long a clock whose headers all fail is kept, and whether the search that follows ends up back on it; and how often a header fails its HEC while the clock and the UAP are right | The hops come from `single_hop()`, as `piconet`'s do. The packets are built in the test and found by the library's own access code search, with bit errors applied symbol by symbol at a given rate |
| `internal` | The FEC 2/3 code, the sync word syndrome tables, and the packet header decode through both the pieces and `btbb_decode_header()` | The 20 FEC 2/3 blocks and two syndrome values from upstream's `test_fec23.c` and `test_syndromes.c`, plus the table of 20 UAP, header and HEC rows from upstream's `test_header.c` |
| `esco` | The length an eSCO payload is decoded at, and the HV3 voice packet that shares a type code with an EV3: a payload three lengths agree with, a payload whose own CRC ends in a zero byte so that the length one short agrees too, captures of noise and of zeros past the packet, a length the caller gives, and a voice payload with no CRC at all | Payloads built in the test, each carrying the CRC the library's own `crcgen()` computes - which `spec_bredr` pins against the specification's sample data first - so the lengths that agree are known before the decoder is asked |
| `capture` | What reaches a capture file: block and option layout, record fields, the four snapshot lengths, header block padding, the option-space limit, and a write to a closed descriptor and to a file at `RLIMIT_FSIZE` | The pcapng specification and the published `LINKTYPE_BLUETOOTH_BREDR_BB` and `LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR` layouts. Every record is read back out of the file by a small block and option walker rather than compared with the struct that wrote it, so a field at the wrong offset shows up |
| `capture_internal` | The two record assemblers called directly, so the test owns the record's memory and can set the padding to a known pattern before the record is built, and the option list an interface description is handed to the writer in, counted at every description length | The same layouts; this is the half `capture` cannot see, because a file cannot say what a padding byte held beforehand |
| `le_long` | A PDU's length read from all eight bits of the field, payload lengths 0, 1, 27, 31, 32, 55, 56, 100 and 251 to 255 preserved with their last byte intact, a header claiming 251 bytes in a 50-byte capture, and a 251-octet data PDU and a 255-octet advertising PDU on a primary and on a secondary advertising channel, each written to PCAP and PCAPNG and read back | Vol 6, Part B, Section 2.3 and 2.4 lengths and the two published record layouts |
| `le_print` | What `lell_print()` writes and on which stream: a `CONNECT_REQ`, advertising data carrying 16-bit service UUIDs, the four value lines that carry their own indentation, a Tx Power Level of -12 dBm, and twelve malformed packets, one per diagnostic message, each one counted and silent by default and its detail written for a caller that asked | Hand-built packets, each case comparing the whole of stderr against the expected text and checking stdout carries none of it |
| `companies` | The company identifier table: an identifier the Bluetooth SIG assigned since the table was last taken from BlueZ, the names the earlier table carried, the SIG's current name where one has changed, a name carrying double quotes or characters outside ASCII, a gap inside the assigned range, and an answer for every one of the 65,536 identifiers | The SIG's published Assigned Numbers, `company_identifiers.yaml`, which `python/utils/gen_companies.py` generates the table from |
| `le_print_unsigned_char` | The same source as `le_print`, with the printer compiled into the test and `-funsigned-char`. Plain `char` is unsigned on every ARM host, the Raspberry Pi included, and signed on the x86 and Apple ones, so a signed field read through one comes out differently on each; the Tx Power Level case is what that reaches | The same packets |
| `smoke` | The public API end to end: one known packet of each kind decoded, `btbb_gen_syncword()` and `btbb_find_ac()` with and without a bit error, and every frequency from 2400 to 2480 MHz decoded to a channel index and a PCAP `channel_k` | Sync words computed independently from the (64,30) block code in `python/utils/encode_sw.py`, not from the library's own tables |
| `release_version` | That `cmake/set_release.cmake` produces the version the build claims | The script itself |
| `py_generators` | The tables committed in the library's source against the scripts in `python/utils` that produced them: the four sync word syndrome tables, `BARKER_DISTANCE` and `barker_correct`, and `DEFAULT_CODEWORD`, `DEFAULT_AC` and `sw_matrix` over 28 LAPs. `le_whitening.py` has no committed table of its own, so its 40 channel offsets are checked against the specification and the run they index into against the BR/EDR `WHITENING_DATA` the library does carry | The generators themselves, re-run. The LE offsets come from Vol 6, Part B, Section 3.2, written out in the test as a shift register rather than taken from the script, and loaded the way that section's own worked example for channel index 23 loads it. The whole of the sync word path is pinned by one LAP with no bits set and one per LAP bit, which between them fix all 25 committed values |
| `btaptap` | The keystroke decoder end to end, run as a user runs it: keystrokes out of baseband and HCI captures, modifier keys, a held key, `-c`, `-h` beside a capture, a frame cut short, and every way a run can fail, each one an `error:` line on stderr and a non-zero exit. Also the `pcapdump` module the README offers on its own, which writes a capture file and reads it back | The Ellisys export recorded in `python/pcaptools/tests`, 864 keyboard reports of someone typing, and captures the test builds itself from the HID boot-mode report layout |
| `fuzz_find_ac_corpus`, `fuzz_bredr_corpus`, `fuzz_piconet_corpus`, `fuzz_le_corpus`, `fuzz_pcap_corpus`, `fuzz_pcapng_corpus` | The seed corpus of each fuzz target replayed through it: the access code search, the BR/EDR header and payload decoders, clock and UAP recovery over a run of packets, the LE decoder, and the PCAP and PCAPNG writers fed packets that were decoded from the same bytes. Present only where the compiler builds a libFuzzer program | The suite's own sample packets, with the parameter bytes each target reads in front of them, plus any input a run of that target once failed on. [Fuzzing](#fuzzing) says how to run a target for longer |
| `library_config` | The library configurations and install directories the build offers: that one building neither library is refused while configuring, that a static-only build builds a test program, that the library, the header and `libbtbb.pc` land in the directories the build was given - `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_INCLUDEDIR`, `LIB_SUFFIX`, each empty, a path, an absolute path, and the precedence between them - that the two directories `libbtbb.pc` names hold the library and the header, including after the install is staged into a second prefix, and that a directory leading out of the prefix is refused | Configures, builds and installs trees of its own, because a build is in one configuration and one prefix only |
| `uthash_oom` | What the library's two hash tables do when they cannot be allocated: each of the three allocations uthash makes for the piconet survey's table - the table, its bucket array, and a larger bucket array once a bucket chain has grown - and the table `btbb_init()` builds, with the message, the stream it goes on and the exit status checked, and the survey keeping its piconet while the allocator serves every request | An allocator of the test's own under `uthash_malloc`, so that only the tables' own allocations fail and the entries the library allocates and checks are left working. Each failure runs in a child process, since the library is expected to exit |
| `abi_surface` | What the shared library exports, against `btbb.h`: that every function the header declares is in the library, so a program including it links; that nothing else is exported, so no helper of the library's own reads as part of the interface; and that every declaration carries the `BTBB_API` macro, which is what gives a function default visibility in a library compiled with `-fvisibility=hidden` | The header and the built library, read with `nm`, where the build found one. Names beginning with an underscore are the linker's own and are left out |
| `uthash_direct_include` | That `lib/src/uthash.h` is reached through `btbb_uthash.h`, which sets what an allocation failure does, and not on its own | Two small sources of its own, compiled with the project's C compiler: one including the wrapper, which has to compile, and one including `uthash.h` directly, which has to be refused and to say what to include instead |

One BR/EDR record field is written outside its published range on purpose, and
`spec_bredr` pins that case. The corrected-payload-bits count is recorded as
the decoder made it, which reaches 183 for a DM5 where
[LINKTYPE_BLUETOOTH_BREDR_BB](https://www.tcpdump.org/linktypes/LINKTYPE_BLUETOOTH_BREDR_BB.html)
describes the field as running to +180. The signed 16-bit field holds it, and
no dissector range-checks it, so a reader is better served by the real number.

## What only hardware can check

The Ubertooth fork runs the hardware side, and its bench now has a Classic
(BR/EDR) pair as well as Low Energy transmitters. Its results are in
[SqueakyFloors/ubertooth#736](https://github.com/SqueakyFloors/ubertooth/issues/736).
Checked there against live BR/EDR traffic: the library's progress and decode
commentary stays off standard output, the hopping sequence no longer costs
128 MB of memory a run, and a BR/EDR capture record carries the HEC result
flags and a reference UAP. Waiting on hardware the bench is still gaining: the
eSCO payload decode needs a voice link between the peers, and AFH hop selection
and clock recovery need a peer that adapts its hopping and one that reports its
true clock. The capture record's CRC result flag has not been seen either,
because no BR/EDR payload has come off the air yet. Resting on the unit tests
alone: the capture record's padding bytes, which are only visible by reading
the file back, and the payload reported for a packet that decoded none.

Some cases can only ever be unit tests, because they need packet types real
stacks rarely send, malformed input, a host out of memory, or an ARM host: the
AUX1, DV, EV4, and EV5 payload lengths, the buffer bounds, the signedness of a
Tx Power Level read through a plain `char`, the allocation failures, and a
255-octet PDU at the top of the Length range.

## Adding a test

A fix to this library comes with a test that fails without it, in the suite
that owns that area, and a row for the release in CHANGELOG.md. Add the vectors
to an existing test program where one covers the area; a new program goes in
`tests/CMakeLists.txt` beside the others. If the fix cannot be exercised on the
air yet, say so in [What only hardware can
check](#what-only-hardware-can-check).
