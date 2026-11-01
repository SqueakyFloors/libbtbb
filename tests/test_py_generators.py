#!/usr/bin/env python3
#
# The tables in the library's C source were produced by the scripts in
# python/utils, and nothing has been checking that they still agree. This runs
# each script and compares what it prints with what is committed.
#
# Where a script's output cannot be compared with a table directly, the
# comparison is against this file's own reading of the Core specification
# instead: the LE whitening script's channel offsets are checked against a
# shift register written out here from the specification's description, not
# against the script's own way of arriving at them.

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UTILS = os.path.join(ROOT, "python", "utils")
SRC = os.path.join(ROOT, "lib", "src")

WHITENING_PERIOD = 127
LE_CHANNELS = 40

failures = []


def check(name, ok, detail=""):
    print("%-44s %s" % (name, "ok" if ok else "FAILED"))
    if not ok:
        failures.append(name if not detail else "%s: %s" % (name, detail))


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def run(script, *args):
    """Run a generator and return its standard output."""
    result = subprocess.run([sys.executable, os.path.join(UTILS, script)] + list(args),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True)
    if result.returncode != 0:
        sys.exit("%s exited %d:\n%s" % (script, result.returncode, result.stderr))
    return result.stdout


def c_array(text, name):
    """The initializer of a C array, as one string."""
    m = re.search(r"\b%s\[\] = \{(.*?)\};" % re.escape(name), text, re.S)
    if not m:
        sys.exit("no %s[] in the library source" % name)
    return m.group(1)


def hex_values(text):
    return [int(v, 16) for v in re.findall(r"0[xX]([0-9a-fA-F]+)", text)]


def decimals(text):
    return [int(v) for v in re.findall(r"\d+", text)]


def c_define(text, name):
    m = re.search(r"#define\s+%s\s+(0[xX][0-9a-fA-F]+)" % re.escape(name), text)
    if not m:
        sys.exit("no %s in the library source" % name)
    return int(m.group(1), 16)


def c_constant(text, name):
    """The value of a C variable initialized with one hex literal."""
    m = re.search(r"\b%s\s*=\s*(0[xX][0-9a-fA-F]+)" % re.escape(name), text)
    if not m:
        sys.exit("no %s in the library source" % name)
    return int(m.group(1), 16)


# ---------------------------------------------------------------------------
# sw_check_tables.h: the sync word syndrome tables.
#
# gen_check_tables.py prints eight lines, the syndrome contributions of the
# eight bytes of a sync word. The library takes the low four bytes as they
# stand and keeps a table for each of the high four, so lines 4 to 7 are the
# committed tables.
# ---------------------------------------------------------------------------
def check_syndrome_tables():
    lines = run("gen_check_tables.py").strip().split("\n")
    check("gen_check_tables.py prints 8 lines", len(lines) == 8, str(len(lines)))
    if len(lines) != 8:
        return
    # gen_syndrome() in the library reads the low four bytes of a sync word
    # straight into the syndrome, with no table, because the check matrix
    # leaves those 32 bits alone. That is what the first four lines say, and
    # the shortcut is wrong if they ever stop saying it.
    identity = all(hex_values(lines[byte]) == [i << (8 * byte) for i in range(256)]
                   for byte in range(4))
    check("the low four bytes are their own syndrome", identity)

    header = read(os.path.join(SRC, "sw_check_tables.h"))
    for byte in range(4, 8):
        name = "sw_check_table%d" % byte
        committed = hex_values(c_array(header, name))
        generated = hex_values(lines[byte])
        check("%s matches the generator" % name,
              committed == generated,
              "%d committed, %d generated" % (len(committed), len(generated)))


# ---------------------------------------------------------------------------
# The barker tables in bluetooth_packet.c.
# ---------------------------------------------------------------------------
def check_barker_tables():
    out = run("gen_barker_correct.py")
    distance_line, correct_line = out.strip().split("\n")
    src = read(os.path.join(SRC, "bluetooth_packet.c"))
    check("BARKER_DISTANCE matches the generator",
          decimals(c_array(src, "BARKER_DISTANCE")) == decimals(distance_line))
    check("barker_correct matches the generator",
          hex_values(c_array(src, "barker_correct")) == hex_values(correct_line))


# ---------------------------------------------------------------------------
# The sync word path: DEFAULT_CODEWORD, DEFAULT_AC and sw_matrix.
#
# btbb_gen_syncword() starts from DEFAULT_CODEWORD, the sync word of the
# all-zero LAP, and adds a row of sw_matrix for each LAP bit that is set.
# encode_sw.py arrives at the same sync word the long way round, from the
# generator matrix of the (64,30) code, so one LAP with no bits set and one
# with each single bit set pin all 25 committed values.
# ---------------------------------------------------------------------------
def check_syncword():
    src = read(os.path.join(SRC, "bluetooth_packet.c"))
    default_codeword = c_define(src, "DEFAULT_CODEWORD")
    default_ac = c_define(src, "DEFAULT_AC")
    sw_matrix = hex_values(c_array(src, "sw_matrix"))
    pn = c_constant(src, "pn")
    check("sw_matrix has a row per LAP bit", len(sw_matrix) == 24, str(len(sw_matrix)))
    if len(sw_matrix) != 24:
        return

    def library_syncword(lap):
        codeword = default_codeword
        for i in range(24):
            if lap & (0x800000 >> i):
                codeword ^= sw_matrix[i]
        return codeword

    laps = [0] + [1 << i for i in range(24)] + [0xffffff, 0x2a96ef, 0x9e8b33]
    mismatched = []
    for lap in laps:
        printed = run("encode_sw.py", "0x%06x" % lap).split()
        got_lap, codeword, syncword = (int(v, 16) for v in printed)
        if (got_lap, syncword, codeword) != (lap, library_syncword(lap), library_syncword(lap) ^ pn):
            mismatched.append("0x%06x" % lap)
    check("encode_sw.py matches btbb_gen_syncword's tables",
          not mismatched, ", ".join(mismatched))
    check("DEFAULT_AC is the all-ones LAP's codeword",
          default_ac == library_syncword(0xffffff) ^ pn)


# ---------------------------------------------------------------------------
# le_whitening.py.
#
# The library does no LE dewhitening, so there is no LE table to compare with.
# What can be checked is the specification: the channel offsets the script
# prints are compared with a shift register written out below straight from
# Vol 6, Part B, Section 3.2, and the run it indexes into is compared with the
# BR/EDR WHITENING_DATA[] the library does carry, which Vol 2, Part B,
# Section 7.2 generates with the same polynomial.
# ---------------------------------------------------------------------------
def le_whitening_register(channel):
    """Vol 6, Part B, Section 3.2: a 7-bit register with position 0 set and
    positions 1 to 6 loaded with the channel index, most significant bit
    first. The section's own worked example is channel index 23, which loads
    as 1 0 1 0 1 1 1, and SPEC_EXAMPLE below holds the loading to it."""
    return [1] + [(channel >> (5 - i)) & 1 for i in range(6)]


SPEC_EXAMPLE_CHANNEL = 23
SPEC_EXAMPLE_REGISTER = [1, 0, 1, 0, 1, 1, 1]


def le_whitening_sequence(channel, length):
    """What that register puts out, with x^7 + x^4 + 1: position 6 is the
    output, and it is fed back into position 0 and added to position 4."""
    positions = le_whitening_register(channel)
    out = []
    for _ in range(length):
        bit = positions[6]
        out.append(bit)
        positions = [bit] + positions[0:6]
        positions[4] ^= bit
    return out


def check_le_whitening():
    check("the register loads as the specification's example does",
          le_whitening_register(SPEC_EXAMPLE_CHANNEL) == SPEC_EXAMPLE_REGISTER)

    out = run("le_whitening.py")
    sequence_text, offsets_text = out.split("Array index:")
    sequence = decimals(sequence_text)
    offsets = decimals(offsets_text)
    check("le_whitening.py prints the whole sequence",
          len(sequence) == WHITENING_PERIOD, str(len(sequence)))
    check("le_whitening.py prints an offset per channel",
          len(offsets) == LE_CHANNELS, str(len(offsets)))
    if len(sequence) != WHITENING_PERIOD or len(offsets) != LE_CHANNELS:
        return

    src = read(os.path.join(SRC, "bluetooth_packet.c"))
    whitening_data = decimals(c_array(src, "WHITENING_DATA"))
    rotations = [r for r in range(WHITENING_PERIOD)
                 if [sequence[(r + i) % WHITENING_PERIOD]
                     for i in range(WHITENING_PERIOD)] == whitening_data]
    check("WHITENING_DATA is the same register's run",
          len(rotations) == 1, "%d matching rotations" % len(rotations))

    wrong = []
    for channel in range(LE_CHANNELS):
        window = [sequence[(offsets[channel] + i) % WHITENING_PERIOD]
                  for i in range(WHITENING_PERIOD)]
        if window != le_whitening_sequence(channel, WHITENING_PERIOD):
            wrong.append(str(channel))
    check("every channel offset matches the specification",
          not wrong, "channels " + ", ".join(wrong))


check_syndrome_tables()
check_barker_tables()
check_syncword()
check_le_whitening()

if failures:
    sys.exit("\n%d check(s) failed:\n  %s" % (len(failures), "\n  ".join(failures)))
print("\nall checks passed")
