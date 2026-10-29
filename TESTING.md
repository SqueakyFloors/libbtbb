# Testing libbtbb

libbtbb was built by volunteers and has decoded Bluetooth traffic for Ubertooth
and other projects for many years. It came with a few test programs, from 2012
and 2015; that is typical for a volunteer project of this size and scope. This
fork adds to that foundation with a test suite in the normal build.

There are now 21 tests, and five more where the compiler can build a fuzz
target, driven in large part by the Bluetooth Core
Specification's own published sample data, and they run on every push and pull
request on Linux and macOS, plain and under AddressSanitizer and
UndefinedBehaviorSanitizer. The vectors of the original test programs were
kept and ported (see #9). Malformed radio packets are this library's normal
input, so the decoders and the capture writers are fuzzed as well.

The tests have helped find and fix 78 issues, many of them in packet types and
capture paths that are rarely seen on the air.

- [How to run them](#how-to-run-them)
- [Fuzzing](#fuzzing)
- [What each test covers](#what-each-test-covers)
- [How big the suite is](#how-big-the-suite-is)
- [What the tests found](#what-the-tests-found)
- [Measured improvements](#measured-improvements)
- [Still unverified on air](#still-unverified-on-air)
- [Adding to this page](#adding-to-this-page)

## How to run them

```
$ cmake -S . -B build -DENABLE_PYTHON=OFF -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -Wno-error=implicit-fallthrough"
$ cmake --build build -j8
$ ctest --test-dir build
```

That leaves the python tools out, and with them the two tests that cover
`btaptap`. Drop `-DENABLE_PYTHON=OFF` for those, or run the keystroke one on
its own: `python3 python/pcaptools/tests/test_btaptap.py`.

Under the sanitizers, which is what CI's fourth job does:

```
$ cmake -S . -B build-asan -DENABLE_PYTHON=OFF \
        -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -Wno-error=implicit-fallthrough -fsanitize=address,undefined -fno-sanitize-recover=all"
$ cmake --build build-asan -j8
$ ctest --test-dir build-asan
```

One test, `ctest -R piconet --output-on-failure`. Each test is also a plain
program you can run on its own: `build/tests/test_piconet`. A failure prints
`FAIL <file>:<line>: <condition>` and the program exits non-zero, so the line
that failed names the vector.

CI (`.github/workflows/ci.yml`) runs five jobs: Ubuntu 24.04 with gcc,
macOS 15 with clang, both with `-Wall -Wextra -Werror` and an install into a
temporary prefix, Ubuntu 24.04 with gcc building the static library on its own,
Ubuntu 24.04 with clang under AddressSanitizer and
UndefinedBehaviorSanitizer with `-fno-sanitize-recover=all`, and Ubuntu 24.04
with clang running each fuzz target for a minute.

## Fuzzing

The decoders take whatever the radio gave them, so five libFuzzer targets in
`tests/fuzz/` feed them bytes: the access code search, the BR/EDR header and
payload decoders with the packet dump, the LE decoder with its dump, the PCAP
writers and the PCAPNG writers. The writer targets decode a frame first and
write the packet that came out, which is what a capture tool does.

Each target is built only where the compiler can build a libFuzzer program,
so a gcc build, or one with the clang Apple ships, is unaffected and simply
has no fuzz targets. Under a clang that can:

```
$ CC=clang cmake -S . -B build-fuzz -DENABLE_PYTHON=OFF -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
$ cmake --build build-fuzz -j8
$ ./build-fuzz/tests/fuzz/fuzz_le -max_total_time=600 -max_len=512 \
        -close_fd_mask=1 tests/fuzz/corpus/le
```

`-close_fd_mask=1` closes the dumps, which are the point of those functions
and not of a fuzzing run, and leaves standard error open so that a
sanitizer's report of a real failure is in the log. Add 2 to it, closing the
decoders' complaints about bad packets as well, only together with the
sanitizers' `log_path`, which then holds the report:

```
$ ASAN_OPTIONS=log_path=/tmp/fuzz-asan UBSAN_OPTIONS=print_stacktrace=1:log_path=/tmp/fuzz-ubsan \
        ./build-fuzz/tests/fuzz/fuzz_le -max_total_time=600 -close_fd_mask=3 tests/fuzz/corpus/le
```

A run that finds something writes the input it stopped on to a file, which the
target replays if it is given that file instead of a directory.

The first long run, 50 minutes for each of the five targets on 2026-10-29
(4 hours 10 minutes in all, from the seed corpus, Debug with
AddressSanitizer and UndefinedBehaviorSanitizer and
`-fno-sanitize-recover=all`, macOS 15 arm64), executed 87.3 million inputs:
19.3 million through the access code search, 12.3 million through the BR/EDR
decoders, 49.8 million through the LE decoder, 3.3 million through the PCAP
writers and 2.6 million through the PCAPNG writers. Nothing crashed, and
neither sanitizer reported anything.

Before that run, `fuzz_pcapng` stopped within seconds on the unterminated
interface-description option list of #23, independently of the audit that
found it, and PR #71 fixed it while this was being written. The input it
stopped on is a seed now - `corpus/pcapng/long_description`, a 255-character
description - so that it stays fixed.

The seed corpus in `tests/fuzz/corpus/` is the sample data the rest of the
suite uses - the specification's DH1 packet, the frames of the LE print test,
and a data channel PDU claiming all 255 octets - each with the parameter bytes
the target reads in front of it, which the comment at the top of each target
lists. `ctest` replays the corpus through every target, so the targets keep
working rather than merely building, and an input that once failed stays
covered.

## What each test covers

Several tests reach functions that are internal to the library, so they
`#include` the translation unit under test rather than linking the library.
The library exports only what `btbb.h` declares, and a `static` function is
not even a name the linker sees, so compiling the source is the way to reach
either without widening what the library offers. `tests/CMakeLists.txt` says
which tests do it.

| Test | Covers | Where the vectors come from |
|---|---|---|
| `spec_bredr` | BR/EDR baseband: access codes and sync words, the header HEC, the payload CRC, both FEC codes, the whitening sequence, complete DH1 and DM1 packets, a payload at the longest length every ACL, SCO, eSCO, DV, AUX1 and FHS type allows, truncated captures, uncorrectable FEC blocks, and UAP and CLK6 recovery | Bluetooth Core Specification v5.4, Vol 2, Part G "Sample Data", Sections 3 to 9, with the definitions of Vol 2, Part B. Where the specification publishes no vector for something the library still has to get right, the test builds the packet with its own `spec_hec()`, `spec_whiten()` and `spec_fec23()`, each pinned by published sample data before it is used, so a built packet is the specification's answer and not libbtbb's |
| `spec_hop` | Connection-state hop selection: the basic kernel, the AFH re-mapping function, the AFH same channel mechanism, and the Nmin fallback | Vol 2, Part G, Section 2: three addresses, the basic sequence and four channel maps each, 512 clock values per table. 7680 vectors, transcribed from the published tables |
| `spec_le` | Low Energy link layer: physical channel indices, advertising and data PDU headers, every length the one-octet field can claim up to 255, including each of the four a 260-byte buffer could not hold, and a 255-octet PDU on a primary advertising channel and on two secondary ones, and the access address rules | Vol 6, Part B, Sections 1.4.1, 2.1.2, 2.3 and 2.4. The CRC-24, the per-channel whitening and the channel selection algorithms are not in this library, so they have no vectors here |
| `hop` | `single_hop()` with and without AFH, and `get_hop_pattern()`'s streams and cache | The Ubertooth project's `firmware/tests/test_bluetooth_hop.c`, an independent transcription of Vol 2, Part B, Section 2.6 computed outside either code base. Using them here pins the host's predictions and the channels the board tunes to against one source of truth |
| `piconet` | Clock recovery (hop reversal): the CLK1-27 candidate list, winnowing it against observed hops, the AFH detector, the confirmation rule, and giving up a clock whose headers keep failing without choosing it again. Also the piconet survey: reading a piconet by its LAP while the survey runs, a LAP the survey has not heard, what a destructive walk leaves behind, and a survey that cannot allocate | The hops are produced by `single_hop()`, which `hop` and `spec_hop` pin against the specification, so a "truth" piconet standing in for the device under observation hops the way a real one does |
| `clock_sweep` | What clock recovery does over a range of true-clock values: how often it locks the piconet's clock, a wrong one or none at all; what following each lock does with it; how long a clock whose headers all fail is kept, and whether the search that follows ends up back on it; and how often a header fails its HEC while the clock and the UAP are right | The hops come from `single_hop()`, as `piconet`'s do. The packets are built in the test and found by the library's own access code search, with bit errors applied symbol by symbol at a given rate |
| `internal` | The FEC 2/3 code, the sync word syndrome tables, and the packet header decode through both the pieces and `btbb_decode_header()` | The 20 FEC 2/3 blocks and two syndrome values from upstream's `test_fec23.c` (2012) and `test_syndromes.c` (2012), plus the table of 20 UAP, header and HEC rows that was the only real content of upstream's `test_header.c` (2015), whose program never compiled |
| `esco` | The length an eSCO payload is decoded at, and the HV3 voice packet that shares a type code with an EV3: a payload three lengths agree with, a payload whose own CRC ends in a zero byte so that the length one short agrees too, captures of noise and of zeros past the packet, a length the caller gives, and a voice payload with no CRC at all | Payloads built in the test, each carrying the CRC the library's own `crcgen()` computes - which `spec_bredr` pins against the specification's sample data first - so the lengths that agree are known before the decoder is asked |
| `capture` | What reaches a capture file: block and option layout, record fields, the four snapshot lengths, header block padding, the option-space limit, and a write to a closed descriptor and to a file at `RLIMIT_FSIZE` | The pcapng specification and the published `LINKTYPE_BLUETOOTH_BREDR_BB` and `LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR` layouts. Every record is read back out of the file by a small block and option walker rather than compared with the struct that wrote it, so a field at the wrong offset shows up |
| `capture_internal` | The two record assemblers called directly, so the test owns the record's memory and can set the padding to a known pattern before the record is built, and the option list an interface description is handed to the writer in, counted at every description length | The same layouts; this is the half `capture` cannot see, because a file cannot say what a padding byte held beforehand |
| `le_long` | A PDU's length read from all eight bits of the field, payload lengths 0, 1, 27, 31, 32, 55, 56, 100 and 251 to 255 preserved with their last byte intact, a header claiming 251 bytes in a 50-byte capture, and a 251-octet data PDU and a 255-octet advertising PDU on a primary and on a secondary advertising channel, each written to PCAP and PCAPNG and read back | Vol 6, Part B, Section 2.3 and 2.4 lengths and the two published record layouts |
| `le_print` | What `lell_print()` writes and on which stream: a `CONNECT_REQ`, advertising data carrying 16-bit service UUIDs, the four value lines that carry their own indentation, a Tx Power Level of -12 dBm, and seven malformed packets, one per diagnostic message | Hand-built packets, each case comparing the whole of stderr against the expected text and checking stdout carries none of it |
| `companies` | The company identifier table: an identifier the Bluetooth SIG assigned since the table was last taken from BlueZ, the names the earlier table carried, the SIG's current name where one has changed, a name carrying double quotes or characters outside ASCII, a gap inside the assigned range, and an answer for every one of the 65,536 identifiers | The SIG's published Assigned Numbers, `company_identifiers.yaml`, which `python/utils/gen_companies.py` generates the table from |
| `le_print_unsigned_char` | The same source as `le_print`, with the printer compiled into the test and `-funsigned-char`. Plain `char` is unsigned on every ARM host, the Raspberry Pi included, and signed on the x86 and Apple ones, so a signed field read through one comes out differently on each; the Tx Power Level case is what that reaches | The same packets |
| `smoke` | The public API end to end: one known packet of each kind decoded, `btbb_gen_syncword()` and `btbb_find_ac()` with and without a bit error, and every frequency from 2400 to 2480 MHz decoded to a channel index and a PCAP `channel_k` | Sync words computed independently from the (64,30) block code in `python/utils/encode_sw.py`, not from the library's own tables |
| `release_version` | That `cmake/set_release.cmake` produces the version the build claims | The script itself |
| `py_generators` | The tables committed in the library's source against the scripts in `python/utils` that produced them: the four sync word syndrome tables, `BARKER_DISTANCE` and `barker_correct`, and `DEFAULT_CODEWORD`, `DEFAULT_AC` and `sw_matrix` over 28 LAPs. `le_whitening.py` has no committed table of its own, so its 40 channel offsets are checked against the specification and the run they index into against the BR/EDR `WHITENING_DATA` the library does carry | The generators themselves, re-run. The LE offsets come from Vol 6, Part B, Section 3.2, written out in the test as a shift register rather than taken from the script, and loaded the way that section's own worked example for channel index 23 loads it. The whole of the sync word path is pinned by one LAP with no bits set and one per LAP bit, which between them fix all 25 committed values |
| `btaptap` | The keystroke decoder end to end, run as a user runs it: keystrokes out of baseband and HCI captures, modifier keys, a held key, `-c`, `-h` beside a capture, a frame cut short, and every way a run can fail, each one an `error:` line on stderr and a non-zero exit. Also the `pcapdump` module the README offers on its own, which writes a capture file and reads it back | The Ellisys export recorded in `python/pcaptools/tests`, 864 keyboard reports of someone typing, and captures the test builds itself from the HID boot-mode report layout |
| `fuzz_find_ac_corpus`, `fuzz_bredr_corpus`, `fuzz_le_corpus`, `fuzz_pcap_corpus`, `fuzz_pcapng_corpus` | The seed corpus of each fuzz target replayed through it: the access code search, the BR/EDR header and payload decoders, the LE decoder, and the PCAP and PCAPNG writers fed packets that were decoded from the same bytes. Present only where the compiler builds a libFuzzer program | The suite's own sample packets, with the parameter bytes each target reads in front of them, plus any input a run of that target once failed on. [Fuzzing](#fuzzing) says how to run a target for longer |
| `library_config` | The library configurations and install directories the build offers: that one building neither library is refused while configuring, that a static-only build builds a test program, that the library, the header and `libbtbb.pc` land in the directories the build was given - `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_INCLUDEDIR`, `LIB_SUFFIX`, each empty, a path, an absolute path, and the precedence between them - that the two directories `libbtbb.pc` names hold the library and the header, including after the install is staged into a second prefix, and that a directory leading out of the prefix is refused | Configures, builds and installs trees of its own, because a build is in one configuration and one prefix only |
| `uthash_oom` | What the library's two hash tables do when they cannot be allocated: each of the three allocations uthash makes for the piconet survey's table - the table, its bucket array, and a larger bucket array once a bucket chain has grown - and the table `btbb_init()` builds, with the message, the stream it goes on and the exit status checked, and the survey keeping its piconet while the allocator serves every request | An allocator of the test's own under `uthash_malloc`, so that only the tables' own allocations fail and the entries the library allocates and checks are left working. Each failure runs in a child process, since the library is expected to exit |
| `abi_surface` | What the shared library exports, against `btbb.h`: that every function the header declares is in the library, so a program including it links; that nothing else is exported, so no helper of the library's own reads as part of the interface; and that every declaration carries the `BTBB_API` macro, which is what gives a function default visibility in a library compiled with `-fvisibility=hidden` | The header and the built library, read with `nm`, where the build found one. Names beginning with an underscore are the linker's own and are left out |

## How big the suite is

Counted on 2026-10-28, at `374a48c` on `master`, macOS 15 arm64 with Apple
clang, with the `esco` test counted the same way when it was added. The test
count is kept current as tests are added; the assertion figures date from that
count:

| | Count | How it was counted |
|---|---|---|
| Tests in the build | 22, and 27 with the fuzz targets | `ctest` case count; 16 C programs and six script tests, two of them only with the Python tools enabled, one only where a Python 3 interpreter is found, and one only in a shared build where `nm` is found, plus the five corpus replays a libFuzzer-capable compiler adds |
| Assertion sites | 870 | 856 `CHECK`, `CHECK_EQ`, `CHECK_EQUALS`, `CHECK_CONTAINS`, `CHECK_LACKS` and `XFAIL` call sites across the eleven programs that use those macros, plus the 14 explicit failure conditions in `test_le_long.c`, which checks with plain `if` |
| Assertions executed per run | 176,054 | a scratch copy of the tree with a counter added to each `CHECK` macro body and reported from a destructor, then every test binary run once and the counts summed. The macros do not nest, so nothing is double counted. `test_le_long` is not in this figure |
| Published spec vectors | 7680 hop frequencies | 15 tables of `TABLE_ENTRIES` (512) in `tests/test_spec_hop.c`: three addresses, the basic sequence and four channel maps each |

Where the assertions are:

| Test | Assertions executed |
|---|---|
| `piconet` | 131,575 |
| `spec_bredr` | 31,707 |
| `spec_hop` | 7,199 |
| `capture` | 3,474 |
| `spec_le` | 984 |
| `internal` | 457 |
| `hop` | 221 |
| `smoke` | 161 |
| `esco` | 116 |
| `le_print` | 82 |
| `capture_internal` | 78 |

The large counts are loops over vectors: `piconet` walks candidate clock lists,
`spec_bredr` walks every truncation of several packet types, `spec_hop` walks
the published tables.

## What the tests found

One row per defect a test exposed or holds down. "User impact" is what someone
running the Ubertooth tools would have seen. Most rows were confirmed by
reverting that fix alone and watching the suite fail; PRs #28, #30, #34, #36
and #49 carry the revert tables, and PR #11 a before and after count of wrong
vectors.

Rows are ordered by severity, most severe first.

🔴 memory safety (a read or write past a buffer, use of uninitialized data, a crash, an unbounded allocation), or wrong data delivered silently.  
🟠 a feature that did not work, a visible failure, or a large performance cost.  
🟡 cosmetic, wording, which stream a message went to, or a latent defect.

| Sev | Area | What the test found | User impact | Pinned by | PR | Issue |
|---|---|---|---|---|---|---|
| 🔴 | Decoders | An LE data PDU's length was read from 5 of the 8 bits of the field, and only 64 bytes of a packet were kept | a Bluetooth 4.2 Data Length Extension packet came out short and misdecoded; on air, 46 bytes where the whole packet had arrived | `le_long`, `spec_le` | #2 | — |
| 🔴 | Decoders | `lell_print()` and the PCAP writer read fixed offsets past the 64-byte buffer on a long or noisy packet | a read past the end of the packet's storage | `le_long`, `le_print` | #2, #49 | #35 |
| 🔴 | Decoders | `EV3()` and `EV5()` passed the start of the payload to `unwhiten()` for every byte | an eSCO packet's payload was the first byte repeated; no EV3 or EV5 payload ever decoded | `spec_bredr` | #28 | #16 |
| 🔴 | Decoders | `EV4()` checked the payload CRC at a length of 1, indexing `payload[-8]`, where `crcgen()` with a negative length returns a constant that reads as "CRC good" 1 time in 65536 | no EV4 payload decoded, and a packet could be reported as CRC-verified without being checked | `spec_bredr` | #28 | #17 |
| 🔴 | Decoders | A DV packet's data field was dewhitened from bit 18, where the payload header starts on every other type, instead of continuing past the 80-bit voice field at bit 98 | the length, the data and the CRC of every DV packet were wrong | `spec_bredr`, every clock | #28 | #24 |
| 🔴 | Decoders | `DM()` and `fhs()` compared a bit count against the symbols they had, and a rate 2/3 FEC payload arrives in half again as many symbols as it has bits | a capture that ended partway through a packet decoded a payload out of symbols that were not there | `spec_bredr`, every truncation of a DM1, DH1, DM3, DH5, FHS and DV | #28 | #13 |
| 🔴 | Decoders | `btbb_decode_header()` and `try_clock()` read the 122 symbols of the access code and header with no check that the packet held them, and the header's majority vote covers for the missing ones | a capture cut one symbol short still decoded to a header that looked good | `spec_bredr` | #28 | #13 |
| 🔴 | Decoders | A symbol count below zero, which a caller reaches by subtracting an offset from a capture's length, came back out of the unsigned field as 65535 | every one of the bound checks above was defeated by a negative count | `spec_bredr` | #28 | #13 |
| 🔴 | Decoders | The promiscuous access code search read the end of a sync word before checking it had any offset to search | a heap read past the end of the caller's buffer | `spec_bredr`, against streams allocated at exactly the length the header promises is enough (AddressSanitizer catches the revert) | #28 | #13 |
| 🔴 | Decoders | An uncorrectable payload FEC block, or a payload header that would not decode, left the length the payload header claimed in place, and `btbb_decode_payload()` marks a packet as having a payload whatever it returns | undecoded bytes reached the dump and the capture files; during clock recovery they were the previous candidate clock's decode | `spec_bredr` | #28 | #13, #33 |
| 🔴 | Decoders | `le_channel_index()` computed a negative index for a frequency below 2402 MHz and stored it in a `uint8_t` | 2400 MHz decoded as channel 254, which is no channel at all, and a frequency far enough below the band wrapped into the 0 to 36 that `lell_packet_is_data()` reads as a data channel | `smoke`, every frequency from 2400 to 2480 MHz | #30 | #23 |
| 🔴 | Decoders | The PCAP pseudo-header's `channel_k` wrapped the same way, at both ends | a reader turning `k` back into a frequency as 2402 + 2k saw 2400 MHz recorded as 2912 MHz | `smoke` | #30 | #23 |
| 🔴 | Decoders | `lell_print()` read a `CONNECT_REQ`'s connection parameters, and an advertising PDU's fields, at fixed offsets without checking the payload held them | a `CONNECT_REQ` carrying ten bytes printed a connection access address, a hop increment and a clock accuracy out of whatever followed in the buffer | `le_print` | #49 | #35 |
| 🔴 | Decoders | A flags element with no flags in it read its flags byte from past the end of the advertising data | the dump invented a flags byte | `le_print` | #49 | — |
| 🔴 | Decoders | An LE data PDU that arrived with no payload reported an LL Control opcode read from the first byte of its CRC | the dump invented an opcode | `le_print` | #49 | — |
| 🔴 | Decoders | The eSCO length search stopped at the first length whose CRC agreed, and one payload in 256 ends its CRC in a zero byte, which leaves the CRC register at zero a byte early and makes the length one short of the payload agree as well | an eSCO payload came out one byte short, its last information byte read as part of the CRC: 79 of 20,000 random EV3 payloads at the length a link normally sets up | `esco` | #61 | #40 |
| 🔴 | Decoders | `btbb_decode_payload()` chose between an HV3 and an EV3 by decoding as EV3 and falling back only when the capture ran out before the search had finished | an HV3 voice packet in a capture that holds 16 or more symbols past it, which a real capture does, was reported as 32 bytes of EV3: its 30 voice bytes and two bytes that were not part of the packet | `esco` | #61 | #39 |
| 🔴 | Decoders | The LE symbol buffer was 260 bytes, sized for a data PDU at its Data Length Extension maximum, where the one-octet Length field reaches 255 and so 264 bytes of frame | an `AUX_ADV_IND` or `AUX_CHAIN_IND` carrying 252 to 255 octets, which is what extended advertising uses the long lengths for, had its length reported as 251, and payload octets 252 to 254 were printed and recorded as its CRC; the real CRC was gone | `spec_le` and `le_long`, a 255-octet PDU on a primary advertising channel and on two secondary ones, since those arrive on the channel indices the decoder reads as data channels | #63 | #58 |
| 🔴 | Decoders | Three allocations were written through without being checked, and for one of them - an entry of the access code error correction table - checking it is not enough, since a table missing an entry has nothing to say so | a host out of memory crashed in `unfec23()`, `tun_format()` or `add_syndrome()`; the last of those would have gone on searching for sync words with error correction that no longer put right the patterns the missing entries describe, so `btbb_init()` now throws the part-built table away and returns -1 | `piconet`, through the counted allocator, refusing each one in turn | #71 | #23 |
| 🔴 | Hop and clock | AFH hop selection ran every slot through the reduced table of used channels, where the specification keeps a basic-sequence channel the map marks used and re-maps only an unused one | following an adaptive piconet, the host predicted channels the piconet was not on: 12 of 18 reference vectors wrong | `hop`, `spec_hop` | #11 | — |
| 🔴 | Hop and clock | The peripheral slot did not repeat the central slot's channel | the two slots of a pair disagreed in 96% of slots | `hop`, `spec_hop` | #11 | — |
| 🔴 | Hop and clock | A map with fewer used channels than Nmin (20), or no map at all, was adapted to, with a division by zero | a sequence over too few channels, or a crash | `hop`, `spec_hop` | #11 | — |
| 🔴 | Hop and clock | `gen_hops()` took F' from F, already reduced mod 79, rather than from 16 × CLK27-7 | wrong adapted channels | `spec_hop` | #11 | — |
| 🔴 | Hop and clock | The hopping-pattern cache compared only the low four bytes of its key | an adaptive piconet could be handed the basic sequence cached for the same address | `hop`, cache hit and miss | #11, #30 | — |
| 🔴 | Hop and clock | `init_candidates()` sized the candidate list as though the sequence visited all 79 channels evenly, and appended unbounded | starting recovery on an adaptive piconet wrote about 200 kB past the end of a 212 kB heap allocation; any map with 20 to 39 used channels overflowed | `piconet` (AddressSanitizer reports the revert) | #34 | #20 |
| 🔴 | Hop and clock | `channel_winnow()`'s look-back was guarded by `packets_observed > 0`, which is always true inside that loop, so the first packet read `pattern_indices[-1]` | about one attempt in 79 announced "Hopping pattern appears to be AFH" off the first packet alone and then searched a sequence the piconet was not using | `piconet` (UndefinedBehaviorSanitizer reports the revert) | #34 | #19 |
| 🔴 | Hop and clock | Predictions came from a hopping sequence generated once, when recovery started | every channel seen for the first time afterwards left the predictions describing a map the library no longer held, so the true clock was dropped with the wrong candidates and discovery started over | `piconet` | #34 | #12 |
| 🔴 | Hop and clock | `try_hop()` wrote `pattern_indices[packets_observed]` with no bound, unlike the sibling path that checks and resets | writing past the end of the array after 1000 packets | `piconet` | #34 | #23 |
| 🔴 | Hop and clock | One public `clk_offset` field carried 312.5 µs CLKN ticks from one writer and 6-bit 625 µs slots from another | a caller reading it between CLK1-6 and CLK1-27 being found got a number in the wrong unit | `piconet` | #34 | #23 |
| 🔴 | Hop and clock | Separating those two units left the CLK1-6 offset unreported: `btbb_piconet_get_clk_offset()` read 0 from the moment the search recovered CLK1-6 until hop reversal had the whole CLK1-27 | a capture tool reading a packet's header at the clock the library gave it read it at its own board's clock instead, since dewhitening a header takes CLK1-6, so the record of the packet that settled the address part carried 18 bits of a header nobody sent: check bits that did not come out, and a packet type that was never sent | `piconet`, the specification's own DH1 whitened at a clock away from the board's, decoded at the offset the search reports | #98 | #90 |
| 🔴 | Hop and clock | A candidate CLK1-27 was accepted as soon as it was the last one left | `ubertooth-rx` reported it had the clock, started following, and decoded nothing, with no way back, because while the AFH map is still being learned a clock that is not the piconet's can match every hop observed so far | `piconet`, a synthetic sequence where the observer is one channel short of the device's map | #49 | #44 |
| 🔴 | Hop and clock | `btbb_piconet_set_afh_map()` generated the whole 2^27-entry hopping sequence, and the allocation was unchecked and written through | `ubertooth-follow` read an adapter's AFH map and then stalled for about 0.6 s per address, holding 128 MB, building a sequence nothing had read since hops started being predicted one at a time; a failed allocation was written through and crashed | `piconet`, which replaces the library's `malloc` and asserts the call allocates nothing | #49 | #41, #38 |
| 🔴 | Capture files | A record's alignment padding was whatever the caller's memory held | up to three bytes of unrelated process memory in every record | `capture_internal`, which owns the record's memory and sets it to a known pattern first | #2, #36 | #22 |
| 🔴 | Capture files | The captured and original lengths in LE PCAP and PCAPNG records were the wrong way round | a reader saw the two lengths swapped | `le_long`, both files read back | #2 | — |
| 🔴 | Capture files | The PCAPNG CONNECT_IND option was filled from `symbols[0]` where the 34 bytes of the payload start at `symbols[6]` | every connection parameter in the capture belonged to another part of the packet: the access address was the first four bytes of the advertiser's address, the hop increment came out of the supervision timeout, and the last six bytes were not recorded. A reader given that option cannot follow the connection, which is the only reason the option exists | `capture` | #36 | #21 |
| 🔴 | Capture files | `pcapng_append_packet()` reported a failed or short write as success | a capture that ran out of disk space was invisible, and the file ended mid-block | `capture`, against a closed descriptor and a file at `RLIMIT_FSIZE` | #36 | #22 |
| 🔴 | Capture files | The plain PCAP writers ignored `fwrite()` and always returned success | the same, on that path | `capture` | #36 | #22 |
| 🔴 | Capture files | The CONNECT_IND, BD_ADDR and clock interface options declared a length four bytes too long | a reader walking the options falls out of step and `memcpy` reads past the struct | `capture` | #36 | — |
| 🔴 | Capture files | Appending an interface option past the space set aside for it wrote beyond the end of the mmapped header block | a long capture with many connections corrupted the file and crashed the writer | `capture` (the revert dies with SIGSEGV) | #36 | #43 |
| 🔴 | Capture files | The BR/EDR record struct was 450 bytes for a block of up to 460 | a capture length of 391 or more wrote past the object | `capture_internal`, plus a compile-time assertion | #36 | #23 |
| 🔴 | Capture files | A padding length was narrowed from `size_t` to `uint16_t` | at a 64 kB page size the section header written was malformed | `capture`, with a block too large for one padding option | #36 | #23 |
| 🔴 | Capture files | Every file declared a snapshot length of 400 | short of the 422 a BR/EDR record reaches, so a reader sizing a buffer from it had too little room, and wrong for the 274 and 288 an LE and a PPI record need | `capture`, all four writers | #36 | — |
| 🔴 | Capture files | `btbb_decode_header()` did not clear the previous frame's decoded state, and a short frame returned before the clear | a short frame was recorded with the earlier frame's HEC and CRC results, payload and length | `capture` | #36 | — |
| 🔴 | Capture files | Records written while the library was still guessing a piconet's upper address part carried a rejected candidate clock's payload and CRC answer | a capture claimed results about traffic that had not been decoded | `capture` | #36 | — |
| 🔴 | Capture files | The interface description was handed to the pcapng writer as an option list with no terminating option, and the writers copy up to 255 characters | a description of 253 characters or more left both the writer and the timestamp-resolution check reading an option header from the stack after the list, and writing what they found into the capture file. Ubertooth's tools pass nine characters, so their captures were never affected | `capture`, a 255-character description given to both writers under AddressSanitizer; `capture_internal` guards the list's shape from here on | #71 | #23 |
| 🔴 | Python and build | `PcapReader` built a record's timestamp by writing the fraction-of-a-second field after the decimal point, `float("%s.%s")`, which is only right when the field is written in as many digits as it is wide | a fraction starting with a zero came out up to a second late: 5 microseconds past the second read as half a second, and one record in ten of a nanosecond capture, where the fraction is below 0.1 s, was wrong | `btaptap`'s timestamp cases, a microsecond and a nanosecond capture | #64 | #55 |
| 🔴 | Python and build | `PcapReader` answered "end of capture" for a record header it had only part of, and handed back a frame shorter than the record said it was | a capture a tool was killed in the middle of writing was reported as read right through: `btaptap` stopped there, said nothing and exited 0 | `btaptap`, captures cut off mid-record-header and mid-record | #64 | #55 |
| 🔴 | Python and build | `PcapDumper` wrote a record's two lengths the wrong way round, the length the packet was in the field that says how much of it is in the file | a capture holding the first bytes of longer packets, which is the one thing that field is for, claimed more bytes per record than it held, and a reader walking it either read into the next record or rejected it as corrupt | `btaptap`'s snapshot-length case, two records read back | #64 | #55 |
| 🔴 | Python and build | `PcapReader` never looked at which libpcap magic number a capture holds: it took any file whose first four bytes were not the byte-swapped one as a little-endian microsecond capture, so its "not a libpcap capture" could not be reached and a nanosecond capture, which is what this library's own writer produces, was not told apart | a file that is not a capture was read as one and failed later with a traceback out of the reader, and scaling the fraction field at all needed the unit the magic number carries | `btaptap`, a text file, a file shorter than a capture header, and a nanosecond capture read back | #64 | #55 |
| 🔴 | Piconet survey | `btbb_next_survey_result()` freed a survey entry while the hash table's buckets still pointed at it, left the table's item count as it was, and never freed the table or its buckets | a survey that carried on after some of its results had been read looked that LAP up again for every further packet of that piconet, and wrote through a piconet that had been released; and every survey read to the end left its table behind | `piconet`, which counts the library's `free()` calls as well as walking the table (AddressSanitizer reports the revert as a heap-use-after-free; in a plain build the item counts and the count of blocks the last result gives back fail too) | #69 | — |
| 🔴 | Piconet survey | Neither of the two allocations the survey's own code makes for a LAP it has just heard - its entry for that LAP, and the piconet, in `btbb_piconet_new()` - had its result checked (uthash's own table allocation ends the process, which is #89) | an allocation failure while surveying wrote through a null pointer, so `ubertooth-rx -z` died with SIGSEGV where it could have dropped a packet | `piconet`, which replaces the library's `malloc` and its `calloc`, fails each of the two in turn, and counts the block the failed one gives back (either revert dies with SIGSEGV) | #69 | — |
| 🟠 | Decoders | `decode_payload_header()`'s maximum-length table had no AUX1 case, so the length it had just read was clamped to zero | `ubertooth-rx` showed every AUX1 packet as `payload length: 0` | `spec_bredr` | #28 | #18 |
| 🟠 | Decoders | All three eSCO types only tried a candidate length once a further byte had been decoded | no eSCO payload at the longest length its type allows could ever be found | `spec_bredr`, each type at its shortest, longest and a middle length | #28 | #16 |
| 🟠 | Decoders | The company identifier table came from BlueZ around 2020 and ended at identifier 1037 | manufacturer-specific advertising data from any company registered since then printed `Company: not assigned`, and 124 of the names the table did carry had since been renamed or changed hands | `companies` | #75 | #72 |
| 🟠 | Decoders | A Tx Power Level, which the specification defines as signed, was read through a plain `char` | plain `char` is unsigned on every ARM host and signed on the x86 and Apple ones, so an advertiser at -12 dBm printed as 244 dBm on a Raspberry Pi and correctly on a laptop: the same capture read differently depending on where it was read | `le_print_unsigned_char`, the `le_print` checks with the printer compiled into the test and `-funsigned-char` | #71 | #23 |
| 🟠 | Hop and clock | `btbb_piconet_unref()` did not free the candidate list, and `btbb_init_hop_reversal()` leaked a previous call's | a piconet released during recovery leaked its candidates | `piconet` | #34 | — |
| 🟠 | Hop and clock | The UAP was shifted into a piconet's address as a signed `int` | undefined behaviour for any UAP of 0x80 or more, which the sanitizers report | `piconet` under UndefinedBehaviorSanitizer | #49 | #23 |
| 🟠 | Hop and clock | Discovery starting over had no memory of the clock it had just given up | a piconet could lock the same wrong clock, decode nothing, give it up and go round that loop for as long as the capture ran: in the sweep below, 90 of 90 restarts ended back on the clock that had just failed | `piconet`, `clock_sweep` | #65 | #44 |
| 🟠 | Hop and clock | Giving a clock up took a fixed count of failed headers, however long they took to arrive | on a link at four packets a second the receiver stayed on an offset that had never decoded anything for 4.75 s, and proportionally longer on a slower one. Half the count is now enough once the run has been failing for two seconds, which halves that; it cannot be faster than the packets arrive, and a lock that hears nothing at all still needs the caller (#66) | `piconet`, `clock_sweep` | #65 | #44 |
| 🟠 | Hop and clock | A clock could only be given up on headers that failed their HEC, and a wrong CLK1-27 carries the CLK1-6 discovery confirmed, so it checks out every header it receives and gets only the channel wrong | following a wrong clock, `ubertooth-rx` stayed on it for as long as the capture ran, because its symptom is silence rather than failure: of the 90 locks in the sweep, the 22 wrong ones heard 65 of the next 1000 slot pairs on average and 142 at most, against all 1000 for every clock that was right. The library is called per packet and keeps no timer, so `btbb_piconet_drop_clock()` now lets a tool that knows how long it has been following hand the clock back, and the clock is ruled out and discovery starts again | `piconet`, `clock_sweep`, which hands back every lock the library kept: 22 of 22 wrong and 68 of 68 right, each ruled out | #82 | #66 |
| 🟠 | Capture files | BR/EDR records never set the four flag bits the published layout defines for the HEC and CRC results | every packet read as "neither checked", so nothing downstream could tell a CRC-verified payload from a guess | `capture`, `capture_internal` | #36 | #27 |
| 🟠 | Capture files | `ref_uap << 24` shifted into the sign bit of an `int` | undefined behaviour, which a sanitizer traps | `capture` under UndefinedBehaviorSanitizer | #36 | #23 |
| 🟠 | Capture files | A packet with no payload declared a zero-length array | undefined behaviour, which a sanitizer traps | `capture` | #36 | #27 |
| 🟠 | Capture files | `btbb_pcapng_close()` and `lell_pcapng_close()` reported a failure on every close and could not report a real one | a caller could not tell a closed file from a failed one | `capture` | #36 | — |
| 🟠 | Capture files | The trial clock whose CRC came out was not reported as the packet's own decode, so the record of the packet that settles a piconet's upper address part carried a correct-CRC result and no payload | the record a reader most needs, the first evidence the address part was derived from, asserted a correct CRC over a payload the file did not hold: in a four-record replay, `caplen=22 flags=0x0c97 payload=none` where the packet had decoded 8 bytes | `capture`: the search's own pass, a four-record replay through one piconet, and the payload read back out of both writers | #85 | #78 |
| 🟠 | Capture files | The trial clock that settles a piconet's address part read the packet's header into a scratch buffer and recorded nothing of it, so the packet came out of the search with its payload and no header | the record a reader most needs carried eight payload bytes under 18 zero bits, which read as LT_ADDR 0 and a NULL packet, a type that carries no payload at all. A capture tool could read the header by handing the packet its symbols again at the piconet's clock, but that is a new frame to the library and the payload goes with it, so a record held one half or the other and never both | `capture`, the search's own pass and the record it writes, and a packet object reused for a second frame; `piconet`, a packet whitened at a clock 32 slots from the board's coming out of the search with both halves, and an idle link of NULL and POLL packets settling the clock by elimination with no CRC to settle it | #99 | #86 |
| 🟠 | Capture files | A candidate clock the search rules out because the address part it reads is not the piconet's was decoded anyway, and the trial's payload and CRC answer stayed on the packet | following a piconet by LAP and UAP, a record of another piconet's packet on the same LAP could carry a payload that trial decoded. The clock is now ruled out without decoding it | `capture`, a packet whose address part is not the piconet's | #85 | #78 |
| 🟠 | Python and build | `btaptap` read every byte of a capture through `ord()`, which python 3 raises a `TypeError` for, and the read loop took any `TypeError` as the end of the capture | no capture ever gave up a keystroke: the tool printed nothing, said nothing and exited 0, since the changelog claimed python 3 support in 2020-12-R1 | `btaptap`, keystrokes out of a baseband and an HCI capture and out of the recorded Ellisys export | #64 | #55 |
| 🟠 | Python and build | The Ellisys path opened the CSV in text mode, so the CR LF line endings its header comparison expects never matched, and past that point it called `xreadlines()`, converted hex with `str.decode("hex")` and built its faked L2CAP packet with `chr()` | `-e` refused the repository's own example export as not an Ellisys export, and exited 0 | `btaptap`, the recorded export decoded, and a CSV that is not one refused | #64 | #55 |
| 🟠 | Python and build | An unsupported data link layer was reported once per packet, and the run still exited 0 | a capture from the Ubertooth tools, whose link type `btaptap` does not read, printed one line per packet and reported success | `btaptap`, a `DLT_BLUETOOTH_BREDR_BB` capture | #64 | #55 |
| 🟠 | Python and build | `PcapDumper` joined the bytes of its file and record headers with `''.join()` | the half of the `pcapdump` module the README offers could not write a capture at all | `btaptap`'s module cases, which write a capture and read it back | #64 | #55 |
| 🟠 | Python and build | `PcapReader` called a record corrupt when the length the packet was exceeded the capture's snapshot length, which is the one thing a snapshot length produces | a capture taken at a snapshot length, as a long-running capture is, was refused at its first record: no keystrokes, one error line, exit 1 | `btaptap`, a capture whose snapshot length is 40 holding a 50-byte packet | #64 | #55 |
| 🟠 | Python and build | `lib/src/CMakeLists.txt` guarded "no library at all" with an option name nothing sets | `cmake -DBUILD_SHARED_LIB=OFF`, the obvious way to ask for a static build, configured a tree with no library in it and failed halfway through the build with `ld: library 'btbb-static' not found` | `library_config`, and a static-only job in CI | #64 | #62 |
| 🟠 | Python and build | `libbtbb.pc` took its `libdir` and `includedir` from GNUInstallDirs, while the install writes the library to `lib${LIB_SUFFIX}` and the header to `include`, and its prefix was the one the build was configured with rather than where the files ended up | `pkg-config --libs libbtbb` gave a `-L` for a directory holding no library - `/usr/lib/x86_64-linux-gnu` where the prefix is `/usr` on a multiarch distribution, `lib64` on some others, and the configure-time prefix for any install staged with `cmake --install --prefix`, which is what this repository's CI does - so a program linked by luck where the compiler's own search path covered for it and not at all where it did not | `library_config`, an install whose libdir and includedir are set to names the install does not use, and the same build installed into a second prefix with the first one taken away | #80 | #73 |
| 🟠 | Python and build | The install wrote the library to `lib${LIB_SUFFIX}` under the prefix and the header to `include`, so the only way to install into the directory a system keeps its libraries in was to pass `-DLIB_SUFFIX=<suffix>` for it; `CMAKE_INSTALL_LIBDIR` and `CMAKE_INSTALL_INCLUDEDIR`, the standard settings, were read for `libbtbb.pc` and then not used | a plain build on a 64-bit Fedora, RHEL or SUSE put the library in `lib` where the rest of the machine looks in `lib64`, and one into `/usr` on a multiarch Debian or Ubuntu put it in `/usr/lib` rather than `/usr/lib/x86_64-linux-gnu`, so a program linked it where the compiler's own search path covered for it and not at all where it did not, and a packager's `-DCMAKE_INSTALL_LIBDIR=` was ignored. Installed with it, `install.sh` in the Ubertooth fork then built tools that would not start | `library_config`, eight installs into prefixes of their own: the directories left to the system, the two settings given names of their own, `LIB_SUFFIX` carrying a path, `LIB_SUFFIX` set alongside a `CMAKE_INSTALL_LIBDIR` asking for somewhere else, an empty `LIB_SUFFIX` beside the same, both settings empty so that the prefix itself is asked for, a libdir with a `.` in it beside an absolute includedir, and a libdir given as an absolute path. Each checks where the library, the header and `libbtbb.pc` landed and what `libbtbb.pc` says, and two of them install a second time into another prefix. Two more cases need no build: a suffix CMake reads as false is still a suffix, and a directory leading out of the prefix is refused | #94 | #88 |
| 🟠 | Python and build | The library exported 32 helpers of its own that no installed header declares - `single_hop()`, `precalc`, `pcapng_create()`, `crc_check()` and the rest. One of them, `bt_compidtostr()`, is also a function in BlueZ's `libbluetooth`, declared there taking an `int` where libbtbb defines it taking a `uint16_t` | a program linking both libraries - which `ubertooth-scan` and `ubertooth-follow` do - called whichever the link order chose, so the manufacturer name it printed came from either library's table without saying which. The other 31 each read as part of the interface to anyone looking at the symbol table, and become names the library cannot change once a program has taken them up | `abi_surface`, the exported names against the 96 functions `btbb.h` declares, on Linux and on macOS | #92 | #79 |
| 🟠 | Capture files | A new pcapng file was grown to the room its first two blocks are given by writing a single zero byte per `write()` call: about 65,000 system calls to create one file on a host with 16 kB pages | a tool starting a capture stalled before it recorded anything: 300 ms a file on an Apple laptop, where the same file now takes 7 ms | `capture` and `capture_internal`, which read every block and option back out of the file, pin the bytes; `fuzz_pcapng`, which managed 3 runs a second before the fix and about 1,300 after, is what exposed the cost | #93 | #74 |
| 🟡 | Decoders | `btbb_find_ac()`'s documented bound said it reads `search_length + 72` symbols where it reads `search_length + 63` | a caller sizing its buffer from the header over-allocated, and could not tell what was really required | `spec_bredr`, several offsets with the match at the last of them | #28 | #32 |
| 🟡 | Hop and clock | `fill_hop_pattern()` and `get_hop_pattern()` wrote "Calculating complete hopping sequence.", "Hopping sequence calculated." and "Found hopping sequence in cache." to stdout | a tool streaming its capture to stdout had them mixed into the stream, where a script cannot tell them from capture data | `hop`, both streams captured, on a cache miss and a cache hit | #30 | #14 |
| 🟡 | Output streams | `lell_print()` complained about malformed packets with `printf` | the six diagnostic messages landed on stdout in the middle of the packet dump, so a tool writing its capture to stdout had them mixed in and a script could not tell them from packet fields | `le_print`, seven malformed packets, one per message, each comparing the whole of stderr and checking stdout carries none of it | #8 | #4 |
| 🟡 | Output streams | `lell_print()` wrote a line to standard error for every advertising payload whose length fields describe more than the payload holds, and began it `Error:` | malformed advertising data is ordinary on the air, so those lines arrive by the hundred: 8 seconds of ambient traffic produced 34 kB of them in the run recorded on the issue. A capture of 4915 good packets ended in `tshark: Error from extcap pipe` filled with 1024 bytes of them, which both reads as though the capture had failed and leaves no room for the reason a real failure gives. A packet off the air that cannot be read is counted now, by kind, and the detail of one payload is written only for a caller that has asked for verbose output | `le_print`, the seven kinds of malformed payload in both modes: nothing on either stream by default with the payload counted under its own kind and no other, the detail on stderr for a caller that asked, and the counts adding up over a run and starting again when read with a reset | #103 | #101 |
| 🟡 | Output streams | Four advertising-data fields printed their indentation before deciding whether the field could be read | with the complaint moved to stderr, a blank indented line would have been left in the dump | `le_print`, a packet whose four fields are all readable | #8 | #4 |
| 🟡 | Survey and init | An allocation failure inside either of the library's hash tables - the piconet survey's, and the access code error correction table `btbb_init()` builds - ended the process with nothing printed and exit status 255 | a tool short of memory stopped without saying why, under a status no other failure in the library uses | `uthash_oom`, each of the three allocations uthash makes failed in a child process, and the same for the table `btbb_init()` builds | #97 | #89 |
| 🟡 | Python and build | The four table generators in `python/utils` were python 2 only, so none of them ran, and `gen_barker_correct.py` printed its two tables indexed the opposite way round from the library's, the bit reversal its own comment asks for never having been written | a developer regenerating a table got a `SyntaxError`, and the barker tables came out in an order the library does not index with. Pasted into the source, that order puts a distance of 3 at both of the indices a real sync word arrives on, which is above `MAX_BARKER_ERRORS`, so the any-LAP search would have rejected every genuine sync word and found no packets at all; the 16 indices it would have accepted are not sync words. The committed tables are right - the specification's own encoding puts distance 0 at indices 39 and 88, which is where they have it - and every one of them now comes back out of its generator unchanged | `py_generators`, each generator re-run and compared with the committed table | #102 | — |
| 🟡 | Python and build | `btbb_pcap_open()` was exported under the library's public prefix while no installed header declared it | a name that reads as part of the interface, in a library whose other `btbb_` names are the interface, and that nothing following the header could call. Withdrawing it later would break whoever had taken it up from the symbol table | `abi_surface`, the exported names against the 96 functions `btbb.h` declares | #80 | #73 |

One BR/EDR record field is written outside the range its published
description gives, on purpose. The corrected-payload-bits count is recorded as
the decoder made it: the rate 2/3 code corrects one bit per 15-symbol block,
and the longest payload it carries is a DM5's 228 bytes, which is 183 blocks,
so a reception marginal enough to need a correction in every block records
183 where
[LINKTYPE_BLUETOOTH_BREDR_BB](https://www.tcpdump.org/linktypes/LINKTYPE_BLUETOOTH_BREDR_BB.html)
describes the field as running to +180. The field is a signed 16-bit integer
that holds it, and neither Wireshark's dissector nor this repository's own
plugin range-checks it, so a reader is better served by the real number than
by one capped to a description three short of the format's own definition.
`spec_bredr` pins that case; decided in #53.

The vectors ported from the original test programs in #9 — the FEC 2/3
blocks, the syndrome values and the 20-row header table — all passed. They
found nothing, which is the result: that part of the library was right and is
now held down.

## Measured improvements

Numbers marked **re-measured** were taken again for this page on 2026-10-28,
on macOS 15 arm64 with Apple clang and `-DCMAKE_BUILD_TYPE=RelWithDebInfo`,
comparing `master` at `374a48c` with `53dbf50`, the commit before #49, which is
where the hopping sequence was removed. The rest are quoted from the PR or
issue that recorded them, with the source named.

### Memory and time

| | Before | After | Source |
|---|---|---|---|
| `btbb_piconet_set_afh_map()`, first call for an address: the `ubertooth-follow` path | 0.61 s | 43 µs | **re-measured**, a program that calls it three times, timed with `CLOCK_MONOTONIC`. The second and third calls were already a microsecond, because the sequence was cached per address |
| Peak resident memory of that program | 136,347,648 bytes (130 MiB) | 1,540,096 bytes (1.5 MiB) | **re-measured**, `/usr/bin/time -l` |
| Peak resident memory of `test_hop`, which reached the sequence twice | 270,336,000 bytes (258 MiB) | 2,113,536 bytes (2.0 MiB) | **re-measured** |
| `test_hop` wall time | 5.76 s | 0.05 s | **re-measured** |
| Whole `ctest` suite | 16.9 s and 18.9 s, 12/12 | 7.5 s and 6.5 s, 12/12 | **re-measured**, two runs each |
| Creating and closing one pcapng capture file | 299.3 ms and 301.2 ms | 6.6 ms and 6.3 ms | **measured for PR #93**, a program that creates and closes 100 files, timed with `CLOCK_MONOTONIC`, Apple clang `-O2`, two runs each, on a host with 16 kB pages. The 65,000 one-byte writes a file took are now 16 writes of 4 kB |
| `fuzz_pcapng` runs a second, over the same corpus | 3 | about 1,300 | **measured for PR #93**, the same 50 inputs replayed: 31 s before, under 1 s after, of which 13.7 s and 0.09 s were system time |
| Largest single allocation `btbb_piconet_set_afh_map()` requests | 134,217,728 bytes | 0 | `tests/test_piconet.c`, which replaces the library's `malloc` and asserts the call allocates nothing at all |

The 128 MB is `SEQUENCE_LENGTH`, 2^27 bytes: one entry per CLK1-27 value. It
was built per piconet address and cached, so a tool following several piconets
paid for it again with each new address. Hops are predicted one at a time now.

`test_piconet` is the one test that got slower, 0.71 s to 1.03 s, because it
covers more: the confirmation rule, the candidates being chosen again when the
map changes, and the replaced allocator.

### Correctness

| | Before | After | Source |
|---|---|---|---|
| AFH reference hop vectors wrong | 12 of 18 | 0 of 18 | PR #11, the same program against each build |
| Peripheral slot differs from the central slot it repeats | 63,104 of 65,536 | 0 of 65,536 | PR #11 |
| Clock recovery against a device whose map the observer is one channel short of, over 400 true-clock values: a wrong CLK1-27 accepted | 99 of 400 | 10 of 400 | PR #49 review, reproduced independently by both reviewers. It is not impossible, and a piconet followed on a wrong clock still has no way back |
| The same 400: no lock within the hops observed, which is recoverable | 194 | 276 | PR #49. This is what the wrong locks were traded for |
| Payload bytes in the record of the packet that settles a piconet's address part | 0 of the 8 it decoded | 8 of 8 | PR #85, the four-record replay of #78 through the fork's `ubertooth-rx -i` against each library. The other three records are unchanged, and 95 records replayed from 30 s of lab air are byte-identical between the two, since no packet there settled an address part |
| The header in the record of that same packet | another header: `0x0c373`, its check bits failing under the recorded UAP, its type field reading 14 | the packet's own: `0x35da3`, LT_ADDR 3, a DH1, its check bits recomputing under that UAP and no other | PR #98, the four-record replay of #90 through the fork's `ubertooth-rx -i` against each library. The other three records are unchanged |
| The header and the payload in that record together | one or the other: `header=0x35da3 payload=none` with the tool reading the header back itself, or `header=0x00000 payload=22010203048097` without | both: `header=0x35da3 flags=0x0fb7 payload=22010203048097`, the HEC and the CRC valid under the recorded UAP | PR #99, the four-record replay of #86 through the fork's `ubertooth-rx -i` against each library, built in a private prefix |
| The same 400: the true clock found | 84 | 88 | PR #49 |
| Cost of the confirmation rule where the predictions are exact | — | four more packets to lock (10.7 → 14.7, 45.3 → 49.3, 34.2 → 38.2 over three scenarios) | PR #49, both reviewers independently |
| A clock given up, then the same search run again: restarts that ended back on the clock that had just failed, 90 locks over 400 true-clock values | 90 of 90 | 0 of 90 | PR #65, the recovery sweep in `tests/test_clock_sweep.c` at 400 trials. The "forgotten" rows of that run are the old behavior, reproduced by clearing the clocks the piconet remembers giving up |
| Headers needed to give up a clock whose headers all fail, once the run has been failing two seconds | 20 | 10 | PR #65. At four packets a second that is 2250 ms where the old rule took 4750, the "before" figure being what 20 headers at that rate comes to rather than a second run of the program. On a packet every slot pair both rules take 20 headers, 24 ms |
| Which clocks that sweep locks: true, wrong, dropped, undecided of 400 | 68, 22, 280, 30 | the same | PR #65, `test_clock_sweep 400` on `master` at `622b6c2` and on the branch: winnowing is untouched |
| Of the 90 locks, those still followed after 1000 slot pairs of the traffic the receiver would really hear | — | 68 of 68 correct locks kept, 0 given up; 22 of 22 wrong ones kept as well | PR #65. The packets are whitened at the device's clock and stamped with the locked one, so whether a header decodes is decided by the clock under test. A wrong CLK1-27 carries discovery's CLK1-6, which is all the dewhitening and the HEC use, so it decodes every header it hears and no run of failures ever arrives — the residual #44 has measured all along, and what #66 is about. The same follow phase against a clock one slot out, which the AFH same channel mechanism hides from the channel comparison, does give the clock up, so these numbers are a measurement rather than a construction |
| Of those 22 wrong locks, the ones a caller can take back and have ruled out | 0 of 22 | 22 of 22 | PR #82, `test_clock_sweep 400`, measured for that PR. What a caller has to go on is the packets the follow heard over those 1000 slot pairs: 64.7 on average and 142 at most under a wrong clock, 1000 under every one of the 68 right ones, so a threshold between the two exists for a tool to pick. The sweep hands back every lock the library kept, right ones included, and all 90 are ruled out; before, there was no entry point to hand one back with |
| An allocation failure inside one of the library's hash tables: what the process writes, and the status it ends with | nothing, 255 | `error: libbtbb: out of memory` on standard error, 1 | PR #97, the four cases of `tests/test_uthash_oom.c` against the branch and against the same tree with uthash's own `uthash_fatal` default put back, where all four report `exit status 255, expected 1` and an empty standard error |
| Company identifiers the table names | 1038 | 4133 | PR #75, `bt_compidtostr()` walked over 0x0000 to 0x113D on each build |
| Names a shared `libbtbb` exports, where `btbb.h` declares 96 | 128 | 96 | PR #92, `nm -D --defined-only libbtbb.so.1.2` on Ubuntu 24.04 and `nm -g libbtbb.1.2.dylib` on macOS 15 arm64, both against the same two builds |
| Of the 4060 identifiers in the Bluetooth SIG's current list, those the table names with the name the SIG gives it | 841 | 4060 | PR #75, the same walk compared with `company_identifiers.yaml`. A further 73 identifiers the SIG no longer lists are named by both tables |
| Candidate lists rebuilt during a four-minute follow of a non-adaptive piconet, 26,634 candidates each time | 12 | 0 | PR #34, lab board against live traffic |
| LE symbol storage, and the length the decoder reads | 64 bytes, 5 bits of the field | 260 bytes, all 8 bits, clamped to the 251 a data PDU allows | PR #2. On air, a packet whose length field claimed more than one record's worth came out whole, where the old library showed 46 bytes |
| A 255-octet advertising PDU: the length reported, and the three bytes taken as its CRC | 251, and payload octets 252 to 254 | 255, and the real CRC | PR #63, a throwaway program decoding one 264-byte frame against each build: payload octet *n* set to *n*, CRC `aa bb cc`. Before, it reported 251 with `fc fd fe` as the CRC. The committed `spec_le` and `le_long` vectors check the same thing with their own bytes |
| The longest LE record each capture writer produces | 270 and 284 bytes | 274 and 288 | PR #63, the snapshot length read out of live captures on a lab board: a plain PCAP and a pcapng interface block over 28,979 real advertising packets, and a PPI PCAP over 11,462. The `capture` test pins the same two numbers |
| Wrong length reported for an eSCO payload at the longest length its type carries, 20,000 random payloads per type | EV3 79, EV5 132, EV4 117 | EV3 1, EV5 0, EV4 0 | PR #61, `test_length_sweep()` in `tests/test_esco.c` with `SWEEP_TRIALS` raised to 20,000, run on this tree and on a copy with the three decode hunks reverted. The sweep builds payloads the way the air carries them: whitened, FEC encoded for an EV4, at a clock that changes from one to the next, with a tail of noise after the packet. The committed test runs the same sweep at 2,000 and pins its counts |
| The same for a payload well short of that length (EV5, 20 information bytes) | 78 one byte short | 101 one byte long | PR #61, the same sweep. Short of the type's longest length the guess changes direction rather than going away; `btbb_packet_set_esco_length()` removes it |
| CONNECT_IND option bytes recorded | `symbols[0..33]` | `symbols[6..39]`, confirmed on 8438 real records in one lab run | PR #36. Reverting the offset alone fails 39 checks, every connection parameter among them |

### The python tools, and the build

| | Before | After | Source |
|---|---|---|---|
| Keystrokes `btaptap` recovered from the recorded Ellisys export, 864 keyboard reports | 0, and the export refused | 543 characters, the paragraph that was typed | PR #64, `python3 btaptap -e python/pcaptools/tests/ellysis-keyboard.csv` on each build |
| The 33 cases of the new `btaptap` test, run against the tree before the fix | 38 failures and 6 errors | all pass | PR #64, the test file copied into a checkout of `master` at `622b6c2` |
| Test programs a `-DBUILD_SHARED_LIB=OFF` build links | none: the build stopped at the first one | the configuration is refused, and `-DBUILD_SHARED_LIB=OFF -DBUILD_STATIC_LIB=ON` builds and passes all 15 | PR #64, `ctest` in a static-only build |
| Symbols exported under the library's `btbb_` or `lell_` prefix that `btbb.h` does not declare | 1 | 0 | PR #80, the exported names against the header's 95 declarations, which is the comparison `abi_surface` makes wherever the build finds `nm` |
| Of the two directories `libbtbb.pc` names, how many hold what it says they hold | 0 of 2, both with `CMAKE_INSTALL_LIBDIR` and `CMAKE_INSTALL_INCLUDEDIR` set the way a multiarch system sets them, and after an install staged into a second prefix | 2 of 2 | PR #80, the two installs the new `library_config` cases make |
| Checks that fail in the new `library_config` cases - where the library, the header and `libbtbb.pc` landed, what `libbtbb.pc` names, and the directories settled while configuring | 18, against the tree before the fix: a libdir or an includedir the build was given was ignored, in the configured prefix and in one staged with `--prefix`, and a directory leading out of the prefix was accepted | 0 | PR #94, the new cases run against `7bce148~1` |
| Whether the Ubertooth tools `install.sh` installs run, with libbtbb in `lib64` | they do not start: `dyld: Library not loaded: @rpath/libbtbb.1.dylib`, exit 134, and the script's own check says "the installed ubertooth-util would not run" | `ubertooth-util -V` reports `libbtbb loaded from: <prefix>/lib64/libbtbb.1.2.dylib`, exit 0 | SqueakyFloors/ubertooth PR for #88, `install.sh --prefix <scratch>` on macOS 15 with a libbtbb installing to `lib64` |
| Public functions whose signature changed since 2020-12-R1, and structures `btbb.h` defines whose layout changed | not known | 0 and 0, with six functions and five constants added | PR #80, `abidiff` 2.4 on both shared libraries, reported on #73 |
| Table generators in `python/utils` that run | 1 of 5, `gen_companies.py` | 5 of 5 | PR #102, each script run with python 3.12 |
| Committed tables and constants a generator reproduces | 0 of 9, since the four scripts that produce them did not run | 9 of 9: `sw_check_table4` to `7`, `BARKER_DISTANCE`, `barker_correct`, `DEFAULT_CODEWORD`, `DEFAULT_AC` and `sw_matrix` | PR #102. With the python 3 port alone it is 7 of 9; the two barker tables are the pair that also needed the bit reversal |
| Mutations of a committed table or a generator the new test catches | — | 14 of 14 | PR #102, one change per copy of the tree: a value in each of `sw_check_table4`, `BARKER_DISTANCE`, `barker_correct` and `WHITENING_DATA`, `sw_matrix[0]`, `DEFAULT_CODEWORD`, `DEFAULT_AC`, `pn`, the channel offsets and the polynomial in `le_whitening.py`, the bit reversal in `gen_barker_correct.py`, the polynomial and the syndrome parity in `gen_check_tables.py`, and the barker branch in `encode_sw.py`. A review of the PR tried 19 of its own, all caught |

Each capture-file fix was also reverted on its own: 1 to 8 failing checks each,
two compile errors, one SIGSEGV and two sanitizer reports, 20 rows in PR #36.
Each clock recovery fix likewise: an AddressSanitizer heap overflow, an
UndefinedBehaviorSanitizer index of -1, a stack overflow and four check
failures, 7 rows in PR #34.

## Still unverified on air

Most of the fixes above are in the Classic (BR/EDR) decoders, and the lab has
Low Energy transmitters only. So these rest on the unit tests until Classic
test hardware is on the bench. The full test plan for the pin bump, including
what has been checked on hardware, is
[SqueakyFloors/ubertooth#736](https://github.com/SqueakyFloors/ubertooth/issues/736).

Waiting on Classic hardware (for example a phone and a headset):

- AFH hop selection and clock recovery: PR #11 and PR #34, the confirmation
  rule in PR #49, and giving a clock up and not choosing it again in PR #59
  and PR #65. The results are statistical, so this needs many follow runs
  compared against the previous pin rather than one pass or fail.
- BR/EDR capture records: the HEC and CRC flags (#27) and the padding (#22).
- The eSCO payload decode (#16) over a headset link.
- The stdout and stale-payload behaviour of the Classic path (#33, #31, #14).

Unit tests only, because they need packet types real stacks rarely send,
malformed input, or a rare chance event:

- AUX1 (#18), DV (#24), EV4 (#17) and EV5 (#16) payloads.
- The length an eSCO payload is decoded at (#40) and the HV3 told apart from
  an EV3 (#39), which need a voice link the lab has no transmitter for.
- The buffer bounds of #13 and #32, and the channel index bound in PR #30.
- A survey that cannot allocate room for a piconet (PR #69), which cannot be
  produced on air. The survey itself, and the teardown that empties it, were
  run on a board.
- The interface-description option list (PR #71): no tool passes a description
  long enough to reach it, which is why it was never seen on air.
- The Tx Power Level signedness (PR #71), which needs an ARM host rather than
  an advertiser: `le_print_unsigned_char` stands in for one.
- The allocation failures of PR #71, which need a host out of memory: the
  counted allocator in `piconet` stands in for one.
- The over-claimed LE advertising length (#35).
- A 255-octet PDU (#58), which needs a transmitter sending extended
  advertising, or an encrypted connection, at the top of the Length range.
  Live advertising capture on a lab board was checked against the widened
  buffer and the new snapshot lengths; the long PDU itself was not.

Checked on Low Energy hardware already: the stream split of PR #8 under live
traffic, the CONNECT_IND option read back from a real connection's capture
(PR #36), the LE dump bounds of PR #49, and the capture-file layout over
thousands of real records.

## Adding to this page

If you fix a defect in this library, add its row to the table in
[What the tests found](#what-the-tests-found) in the same PR: a severity glyph,
the area, what the test found, what a user saw, the test that holds it down, your PR
and the issue. Pick the glyph from the legend above that table, judging the row
by what the test found and what a user saw, and take the higher of two when you are
unsure. Put the row with the others of its severity, 🔴 first, then 🟠, then 🟡,
and within a severity next to the other rows of its area, in PR order. If your PR
measured something — memory, time per call, a sweep of clock values, a count of
vectors that were wrong before and right after — add it to
[Measured improvements](#measured-improvements) with the source, and say
whether the figure was measured for that PR or quoted. If the fix cannot be
exercised on air yet, add it to
[Still unverified on air](#still-unverified-on-air).

The assertion counts in [How big the suite is](#how-big-the-suite-is) go stale
as tests are added. They are worth re-counting when a release is cut; the
method is in that section.
