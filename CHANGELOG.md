# Changelog

## 2026-10-R1

### Decoding (BR/EDR)

- Fixed: AUX1, EV3, EV4, and EV5 payloads did not decode; they now do, each
  at the longest length its type allows.
- Fixed: a DV packet's data field was dewhitened as the voice field; it now
  decodes with its own whitening.
- Fixed: an HV3 voice packet was reported as 32 bytes of EV3; it is now
  reported as HV3.
- Fixed: an eSCO payload's length was the first length whose CRC agreed; it
  now comes from btbb_packet_set_esco_length(), or the longest that agrees.
- Fixed: a packet whose payload did not decode, or is shorter than its own
  CRC, was given a payload and called CRC-checked; it is now given neither.
- Fixed: a packet cut short decoded a header and a payload out of symbols
  the capture did not hold; it now decodes neither.
- New flags BTBB_HEC_CHECKED, BTBB_HEC_VALID and BTBB_CRC_CHECKED join
  BTBB_CRC_CORRECT to say what the decoder checked and what it found.
- New btbb_find_ac_n() takes the symbol stream's length and reads no
  further; btbb_find_ac(), which carries no length, is unchanged.

### Decoding (LE)

- Fixed: an extended advertising PDU lost the end of its payload; the Length
  now comes from the whole header octet, as the Core specification defines.
- Fixed: a Length of 252 to 255 lost its last octets and its real CRC; the
  LE symbol buffer is now 264 bytes, so they are kept.
- Fixed: capture files held only the first 64 bytes of an LE data PDU; they
  now hold all 251 bytes Data Length Extension permits.
- New lell_allocate_and_decode_n() takes the bytes captured, so it cannot
  read past a caller's buffer; lell_get_payload_length() reads the length.
- Fixed: a malformed advertising payload was reported line by line; it is now
  counted by lell_malformed_count(); lell_set_malformed_verbose() asks for it.
- Fixed: a service UUID list whose length is not a whole number of UUIDs
  printed none of them; the whole ones now print, and the leftover is counted.
- A 32-bit Service UUID list (AD types 0x04 and 0x05) now prints as UUIDs;
  earlier releases showed its bytes under an unrecognized type.
- Fixed: lell_print() reported a Tx Power Level of -12 dBm as 244 dBm; it
  now prints the signed value the specification defines, on every host.
- Fixed: an advertising PDU too short for the fields read from it now says
  what is missing, and a data PDU with no payload shows no LL Control opcode.
- Fixed: a flags element with no flags in it had a flags byte read from past
  the end of the advertising data; it no longer does.
- Fixed: a frequency outside 2402 to 2480 MHz gave an unbounded channel
  index; the index is now bounded.
- Manufacturer-specific data names 4133 companies from the Bluetooth SIG's
  published Assigned Numbers, where BlueZ's gave 1038, and keeps the 73.
- New btbb_company_name() gives a program that same table: it returns the
  name the SIG assigns a company identifier, or NULL where the identifier is
  not assigned.
- Fixed: LE decode complaints went into the packet dump on standard output;
  they now go to standard error, unchanged in wording.

### Clock and hopping

- Fixed: a clock search on a piconet using adaptive frequency hopping (AFH)
  started over on the basic sequence; it now narrows toward a clock.
- Fixed: a clock tracked with AFH named the wrong channels; hopping now
  follows the Core specification (Vol 2, Part B, 2.6.3.1).
- Fixed: an adaptive piconet was handed the hopping sequence cached for the
  same address and a different channel map; it no longer is.
- Fixed: a clock and UAP that are not the piconet's were kept forever; 20
  failed headers, or 10 after two seconds, now give BTBB_PROCESS_LOCK_LOST.
- New btbb_piconet_drop_clock() hands back a clock whose only symptom is
  silence; a clock given up is not chosen again, and a caller's is kept.
- Fixed: clock recovery accepted whichever candidate was last standing,
  which against an adaptive piconet was often a wrong one; candidates are
  now scored on the hops they explain and a clock is accepted only while it
  is clearly ahead of every other, so a search that cannot tell two clocks
  apart ends with none rather than with the wrong one.
- Fixed: a hop predicted on a channel the observer had not yet heard ruled a
  candidate out, and so ruled out the piconet's own clock while its map was
  still being learned; such a hop now counts neither way, and the two clocks
  drifting apart is followed instead of costing the clock.
- The clock search costs more while it runs: it carries up to a few million
  candidates where it carried tens of thousands, which is tens of megabytes
  and up to a few hundred milliseconds of an attempt inside the caller's
  packet callback.
- Fixed: a piconet that nothing works for was reported on every loss; it is
  now reported once, after three clocks are given up.
- Fixed: btbb_piconet_get_clk_offset() read 0 until the whole clock was
  known; it now reports it in 625 us slots as soon as CLK1-6 is known.
- New btbb_survey_piconet(lap) hands back the live piconet for a LAP and
  leaves it in the survey, so a tool can read a recovered UAP.
- Fixed: btbb_init_hop_reversal() reversed hops where 79 channels are
  observed folded into 25; it now refuses, with a note on standard error.
- Fixed: reading a piconet's AFH map cost a 128 MB allocation and the wait a
  whole hopping sequence took; it now answers hops one at a time.
- The hopping sequence is gone, with its notes on standard output and the
  exported hop(), gen_hop_pattern() and get_hop_pattern().

### Capture files

- Fixed: the record of the packet that settles a piconet's address part held
  another packet's header; it now holds that packet's own, with the HEC flags.
- Fixed: a record could carry a trial clock's payload from guessing the upper
  address part; it now carries only what the decoder produced for the packet.
- BR/EDR records say whether the header's HEC and the payload's CRC were
  checked and whether they held, in both PCAP and PCAPNG files.
- Fixed: the forward error correction fields in BR/EDR records read 0
  whatever the reception; they now carry the number of bits put right.
- Fixed: a PCAPNG file recorded connection parameters a CONNECT_IND did not
  carry; it now records the packet's own, so a reader can follow it.
- Fixed: a long LE PCAPNG capture ran out of room for a CONNECT_IND's
  parameters in silence; lell_pcapng_append_packet() now says so with the
  positive LELL_PCAPNG_CONNECT_INFO_NOT_RECORDED, so a tool can warn once.
- Fixed: an LE capture file's reference access address flag followed the
  channel; it now follows the address the caller gave, as the layout defines.
- Fixed: a capture file declared a snapshot length shorter than its records;
  it is now the longest: 422 bytes for BR/EDR, 274 for LE, and 288 for PPI.
- Fixed: a failed or incomplete write went unreported; it is now reported
  and taken back out, so a full disk ends the capture on a whole record.
- Fixed: btbb_pcapng_close() and lell_pcapng_close() reported an error even
  when the file closed cleanly; they now return 0 on success.
- Fixed: the interface options a PCAPNG file records for a CONNECT_IND, a
  BD_ADDR, and a clock gave the wrong length; they now give their contents'.
- Fixed: creating a pcapng capture file took a few hundred milliseconds; it
  now takes a few, and the bytes written are unchanged.
- Fixed: the captured and original lengths in LE PCAP records were the wrong
  way round, and the PCAPNG writer copied more than the record holds.
- Fixed: a pcapng capture file that could not be written was left behind,
  empty or part written, for the user to tell apart from a capture that
  caught nothing; it is now taken away again, and a file that was already
  there is still left untouched.
- Fixed: a PCAP file whose header could not be written was left behind
  empty, for the user to tell apart from a capture that caught nothing; a
  file the writer made itself is now taken away again, and a name that was
  already in use is written over as before and left alone.

### Output

- Renamed the Bluetooth connection roles from master/slave to
  Central/Peripheral, as the Core specification now does. Printed packets
  read `LL_PERIPHERAL_FEATURE_REQ` and `Peripheral Connection Interval
  Range`, and the Wireshark plugins label LMP, LL, and SMP fields the same
  way. The filter names and every numeric value are unchanged.
- Fixed: decoding a packet printed it; only btbb_print_packet() prints now,
  as it always has.
- Fixed: progress and complaints from UAP and clock discovery went to
  standard output; they now go to standard error, apart from the AFH map.
- Fixed: an allocation the library could not get went unreported; the FEC
  2/3 decoder, tun_format(), and btbb_piconet_new() now return NULL, and
  btbb_init() returns -1.
- Fixed: a failed allocation in one of the library's hash tables exited 255
  in silence; it now prints "error: libbtbb: out of memory" and exits 1.
- Fixed: btbb_pcap_create_file() printed "PCAP error:" of its own beside
  the caller's report of the same failure; the caller, which knows the file
  name, now reports it alone, as it already did for every other writer.

### Build and install

- Fixed: the library did not build or install out of the box on current
  tools; the CMake minimum is 3.5, and the Python tools install from a wheel.
- Fixed: the library and the header did not go where the system keeps them;
  GNUInstallDirs now decides, and CMAKE_INSTALL_LIBDIR,
  CMAKE_INSTALL_INCLUDEDIR and LIB_SUFFIX choose otherwise.
- Fixed: libbtbb.pc named directories the files were not in; it now names
  the real ones and takes its prefix from its own location, so a staged or
  moved install still links.
- Fixed: the cleanup script looked only in lib and left files behind; it now
  looks in lib64 and a multiarch directory, and finds the pkg-config file, a
  static archive, a .dylib and btbb.h.
- Fixed: the library exported 128 names, one of which, bt_compidtostr(),
  clashed with BlueZ's; it now exports only the 99 btbb.h declares.
- Fixed: on Windows the BTBB_API macro each declaration in btbb.h carries
  was not the dllexport or dllimport declaration a shared library needs.
- The soname stays libbtbb.so.1 and the library version is 1.3, where
  2020-12-R1's was 1.0; abidiff finds ten functions gained, none lost.
- Existing callers see three changes behind that interface: two functions
  answer a positive value where they answered 0, the writers report a write
  that missed the file, and a decode says less where it knows less.
- Fixed: a build asking for no library, "cmake -DBUILD_SHARED_LIB=OFF", was
  accepted; it is now refused while configuring, naming both options it takes.
- The RPM spec, the MacPorts Portfile and the Homebrew formula in packaging/
  are gone; each of those projects keeps its own packaging.

### Python tools

- Fixed: btaptap decoded no keystrokes; it does again, and the recorded
  Ellisys export, 864 keyboard reports, gives up the paragraph typed into it.
- Fixed: btaptap exited 0 or raised a traceback on a capture it cannot read
  through; it now reports one "error:" line and a non-zero exit.
- Fixed: btaptap took a bad command line; an unknown option, a missing value,
  a repeated value, -r with -e, and -c without -r now each exit 1.
- Fixed: btaptap's -h canceled a capture given beside it; it now prints the
  usage text on standard output and leaves the capture alone.
- Fixed: the pcapdump module wrote no capture file; it writes one again, and
  takes a timestamp's unit from the capture's magic number, so nanosecond
  files read as such.
- Fixed: the pcapdump module took a file that is not a capture, misread one
  taken at a snapshot length, and wrote a record's two lengths out of order.
- Fixed: btaptap did not find the pcapdump module of the install it belongs
  to; it now does, and installing into a second prefix puts the module there.
- Fixed: the scripts in python/utils that produced the committed tables did
  not run on python 3; they now do, and gen_barker_correct.py reproduces the
  barker tables.

### Under the hood

- Clock recovery no longer writes about 200 kB past its candidate list under
  adaptive hopping.
- The first packet of a recovery attempt no longer adapts to an AFH map the
  library never observed.
- Hop predictions came from a 128 MB table; they now follow the AFH map as
  it is learned, one hop at a time.
- Choosing the CLK1-27 candidates walked the 2^21 clock values more than
  once; it now walks them once, and btbb_winnow() stops once the clock is
  known.
- The hop pattern remembered while reversing hops was unbounded, and a
  piconet released during clock recovery leaked its candidate list.
- btbb_next_survey_result() left the piconet's entry in the survey's hash
  table; it now takes it out of the table as well as the list, and the table
  is freed.
- The 625 us slot offset and the 312.5 us CLKN tick offset shared one field,
  and the UAP was shifted into an address before it was widened.
- An allocation failure in the LE decoder is no longer dereferenced.
- An interface description of 253 characters or more is no longer read past
  the end of the option list that carries it.
- One padding option could not cover a PCAPNG header block's spare room; as
  many as it takes are now written, and options past that room refused.
- A PCAPNG record no longer carries three bytes of the caller's memory in
  the padding that rounds it to a 4-byte boundary, in either writer.
- A no-payload record, a high reference UAP, the LE decoder, the dump, and
  an unaligned read of 16-bit UUIDs no longer rely on undefined behavior.
- A capture file name already in use was reported as a failed write; it is
  now reported as the name being taken, and the reason a write did fail is
  no longer lost while the file is closed and removed.

_Russell Wagner &lt;152923636+SqueakyFloors@users.noreply.github.com&gt;, 30 Oct 2026 09:30:00 -0700_

## 2020-12-R1

- Python 3 support
- Minor bugfixes

_Mike Ryan &lt;mikeryan@lacklustre.net&gt;, 25 Dec 2020 13:30:00 -0800_

## 2018-12-R1

- No changes, just keeping version parity with Ubertooth 2018-12-R1

_Mike Ryan &lt;mikeryan@lacklustre.net&gt;, 04 Dec 2018 21:50:00 -0800_

## 2018-08-R1

- No changes, just keeping version parity with Ubertooth 2018-08-R1

_Mike Ryan &lt;mikeryan@lacklustre.net&gt;, 07 Aug 2018 15:36:00 -0700_

## 2018-06-R1

- Minor bugfix and version bump for 2018-06-R1 Ubertooth release

_Mike Ryan &lt;mikeryan@lacklustre.net&gt;, 26 Jun 2018 13:20:00 -0700_

## 2017-03-R2

- Remove dependency on libpcap, simplifying build process

_Dominic Spill &lt;dominicgs@gmail.com&gt;, 01 Mar 2017 10:39:00 -0700_

## 2015-10-R1

- This is a minor bug fix release. It includes fixes to clock
  recovery and piconet survey mode

_Dominic Spill &lt;dominicgs@gmail.com&gt;, 12 Oct 2015 03:10:00 -0700_

## 2015-09-R2

- New PCAP format for BR and EDR packets
- Wireshark dissector for BR/EDR PCAP format
- BLE PCAPNG support
- Improved support for AFH
- Numerous bug fixes

_Dominic Spill &lt;dominicgs@gmail.com&gt;, 05 Sep 2015 05:30:00 -0700_

## 2014-02-R2

- This release adds new features while including a complete rewrite of the
  Basic Rate API.
- Bluetooth Low Energy support (aka Bluetooth Smart)
- Limited support for AFH
- Cleaned up Basic Rate API - btbb.h
- Wireshark plugins support v1.10
- Numerous bug fixes

_Dominic Spill &lt;dominicgs@gmail.com&gt;, 20 Feb 2014 06:06:00 -0700_
