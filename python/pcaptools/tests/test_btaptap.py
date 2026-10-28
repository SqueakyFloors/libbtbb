#!/usr/bin/env python3
#
# What btaptap gets out of a capture, and what it says when it cannot.
#
# Run it on its own with: python3 python/pcaptools/tests/test_btaptap.py
#
# The tool is run as a subprocess, the way a user runs it, so that what
# reaches stdout, what reaches stderr and the exit status are all covered.
# The captures are built here rather than recorded, except the Ellisys export
# beside this file, which is a recording of someone typing.

import os
import struct
import subprocess
import sys
import tempfile
import unittest

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
PCAPTOOLS_DIR = os.path.dirname(TESTS_DIR)
BTAPTAP = os.path.join(PCAPTOOLS_DIR, "btaptap")
ELLISYS_CSV = os.path.join(TESTS_DIR, "ellysis-keyboard.csv")

# The pcapdump module is beside btaptap, which is where btaptap itself finds
# it when it is run from the source tree.
sys.path.insert(0, PCAPTOOLS_DIR)
from pcapdump.pcapdump import PcapDumper, PcapReader

DLT_EN10MB = 1
DLT_BLUETOOTH_HCI_H4 = 187
DLT_BLUETOOTH_BREDR_BB = 255

PCAP_MAGIC = 0xa1b2c3d4
# The magic number of a capture whose records count nanoseconds, which is
# what libbtbb's own capture writer produces.
PCAP_MAGIC_NANO = 0xa1b23c4d


def pcap_file(path, datalink, frames, magic=PCAP_MAGIC, fraction=500000):
    '''
    Write a little-endian libpcap file holding the given frames.
    '''
    with open(path, "wb") as fh:
        fh.write(struct.pack("<IHHiIII", magic, 2, 4, 0, 0, 65535,
                             datalink))
        for frame in frames:
            fh.write(struct.pack("<IIII", 1700000000, fraction, len(frame),
                                 len(frame)))
            fh.write(frame)
    return path


def l2cap_keystroke(scancodes, modifiers=0, cid=0x0306):
    '''
    An L2CAP packet carrying an HID boot-mode keyboard report, as a keyboard
    that does not encrypt its link sends it: the report ID, the modifier
    byte, a reserved byte and room for six scancodes, of which btaptap reads
    the first three.
    '''
    TRANS_HDR_IN_DATA = 0xa1
    REPORT_ID_KEYBOARD = 0x01
    KEYCODES = 6

    report = bytes([REPORT_ID_KEYBOARD, modifiers, 0x00])
    report += bytes(scancodes) + bytes(KEYCODES - len(scancodes))
    body = bytes([TRANS_HDR_IN_DATA]) + report
    return struct.pack("<HH", len(body), cid) + body


def dm1_frame(l2cap, length=45):
    '''
    A DLT_EN10MB frame holding a DM1 packet, the shape btaptap reads: the
    baseband header 20 bytes in, the payload header after it, and the L2CAP
    packet after that.
    '''
    DM1 = 3
    LLID_L2CAP = 2

    frame = bytearray(length)
    frame[20] = DM1 << 3
    frame[23] = (len(l2cap) << 3) | LLID_L2CAP
    frame[24:24 + len(l2cap)] = l2cap
    return bytes(frame)


def h4_frame(l2cap):
    '''
    A DLT_BLUETOOTH_HCI_H4 frame holding an ACL data packet.
    '''
    HCI_TYPE_ACL_DATA = 2

    return (bytes([HCI_TYPE_ACL_DATA]) + struct.pack("<HH", 0x0001, len(l2cap))
            + l2cap)


def btaptap(*args):
    '''
    Run btaptap. Returns the completed process, with text output.
    '''
    return subprocess.run([sys.executable, BTAPTAP] + list(args),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, timeout=60)


class CaptureFileTestCase(unittest.TestCase):
    '''
    A test case with a temporary directory to write captures into.
    '''

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def path(self, name):
        return os.path.join(self.tmp.name, name)


class Keystrokes(CaptureFileTestCase):

    def test_dm1_capture(self):
        # "h", "i", Return, with Shift held for the first.
        frames = [
            dm1_frame(l2cap_keystroke([0x0b], modifiers=0x02)),
            dm1_frame(l2cap_keystroke([0x0c])),
            dm1_frame(l2cap_keystroke([0x28])),
        ]
        cap = pcap_file(self.path("dm1.pcap"), DLT_EN10MB, frames)

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "Hi[Return]\n")
        self.assertEqual(done.stderr, "")
        self.assertEqual(done.returncode, 0)

    def test_hci_h4_capture(self):
        frames = [h4_frame(l2cap_keystroke([0x04]))]
        cap = pcap_file(self.path("h4.pcap"), DLT_BLUETOOTH_HCI_H4, frames)

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "a\n")
        self.assertEqual(done.returncode, 0)

    def test_modifiers(self):
        frames = [dm1_frame(l2cap_keystroke([0x06], modifiers=0x11))]
        cap = pcap_file(self.path("ctrl.pcap"), DLT_EN10MB, frames)

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "CTRL^c\n")
        self.assertEqual(done.returncode, 0)

    def test_held_key_is_not_repeated(self):
        report = l2cap_keystroke([0x04])
        cap = pcap_file(self.path("held.pcap"), DLT_EN10MB,
                        [dm1_frame(report), dm1_frame(report)])

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "a\n")

    def test_count_stops_early(self):
        frames = [
            dm1_frame(l2cap_keystroke([0x04])),
            dm1_frame(l2cap_keystroke([0x05])),
        ]
        cap = pcap_file(self.path("two.pcap"), DLT_EN10MB, frames)

        self.assertEqual(btaptap("-r", cap).stdout, "ab\n")
        self.assertEqual(btaptap("-r", cap, "-c", "1").stdout, "a\n")
        self.assertEqual(btaptap("-r", cap, "-c", "0").stdout, "")

    def test_nothing_to_decode_prints_nothing(self):
        # A frame of the right length that is not a DM1.
        cap = pcap_file(self.path("quiet.pcap"), DLT_EN10MB, [bytes(45)])

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "")
        self.assertEqual(done.stderr, "")
        self.assertEqual(done.returncode, 0)

    def test_frames_after_a_short_one_are_still_read(self):
        # A frame cut short mid-L2CAP-packet, as a snapshot length or a
        # capture that stopped leaves it: its length field claims a full
        # report, and the bytes the report needs are not there.
        short = dm1_frame(l2cap_keystroke([0x04]))[:30]
        frames = [short, dm1_frame(l2cap_keystroke([0x05]))]
        cap = pcap_file(self.path("short.pcap"), DLT_EN10MB, frames)

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "b\n")
        self.assertNotIn("Traceback", done.stderr)
        self.assertEqual(done.returncode, 0)

    def test_nanosecond_capture(self):
        # The capture files libbtbb itself writes.
        cap = pcap_file(self.path("nano.pcap"), DLT_EN10MB,
                        [dm1_frame(l2cap_keystroke([0x04]))],
                        magic=PCAP_MAGIC_NANO)

        done = btaptap("-r", cap)
        self.assertEqual(done.stdout, "a\n")
        self.assertEqual(done.returncode, 0)

    def test_ellisys_export(self):
        # A recording of someone typing, 864 HID reports.
        done = btaptap("-e", ELLISYS_CSV)
        self.assertIn("Enter the TCP sequence number:", done.stdout)
        self.assertIn("[Return]", done.stdout)
        self.assertEqual(done.stderr, "")
        self.assertEqual(done.returncode, 0)


class Failures(CaptureFileTestCase):
    '''
    Every one of these has to say what went wrong and exit non-zero: a tool
    that decodes nothing and exits 0 reads as a capture with no keystrokes
    in it.
    '''

    def assertFailed(self, done, *expected):
        self.assertEqual(done.returncode, 1)
        self.assertEqual(done.stdout, "")
        self.assertIn("error: ", done.stderr)
        self.assertNotIn("Traceback", done.stderr)
        for text in expected:
            self.assertIn(text, done.stderr)

    def test_missing_file(self):
        self.assertFailed(btaptap("-r", self.path("nothing.pcap")),
                          "nothing.pcap")

    def test_not_a_capture(self):
        path = self.path("text.pcap")
        with open(path, "wb") as fh:
            fh.write(b"this is not a capture at all, but it is long enough\n")

        self.assertFailed(btaptap("-r", path), "not a libpcap capture")

    def test_capture_shorter_than_its_header(self):
        path = self.path("stub.pcap")
        with open(path, "wb") as fh:
            fh.write(struct.pack("<I", PCAP_MAGIC))

        self.assertFailed(btaptap("-r", path), "too short")

    def test_corrupt_record_header(self):
        # A record claiming more bytes than the snapshot length allows, after
        # a packet that does decode: what is already decoded is kept, and the
        # capture is reported as not read through.
        path = self.path("corrupt.pcap")
        pcap_file(path, DLT_EN10MB, [dm1_frame(l2cap_keystroke([0x04]))])
        with open(path, "ab") as fh:
            fh.write(struct.pack("<IIII", 1700000000, 0, 999999, 999999))

        done = btaptap("-r", path)
        self.assertEqual(done.stdout, "a\n")
        self.assertIn("error: ", done.stderr)
        self.assertNotIn("Traceback", done.stderr)
        self.assertEqual(done.returncode, 1)

    def test_unsupported_datalink(self):
        # What the Ubertooth tools write: btaptap reads neither of the
        # Bluetooth baseband link types.
        frames = [dm1_frame(l2cap_keystroke([0x04]))]
        cap = pcap_file(self.path("bredr.pcap"), DLT_BLUETOOTH_BREDR_BB,
                        frames)

        self.assertFailed(btaptap("-r", cap),
                          "unsupported libpcap data link layer: 255")

    def test_not_an_ellisys_export(self):
        path = self.path("other.csv")
        with open(path, "w") as fh:
            fh.write("a,b,c\n1,2,3\n")

        self.assertFailed(btaptap("-e", path), "Ellisys export format")

    def test_missing_ellisys_file(self):
        self.assertFailed(btaptap("-e", self.path("nothing.csv")),
                          "nothing.csv")


class CommandLine(CaptureFileTestCase):

    def test_help_is_output(self):
        done = btaptap("-h")
        self.assertIn("Usage: btaptap", done.stdout)
        self.assertEqual(done.stderr, "")
        self.assertEqual(done.returncode, 0)

    def test_no_arguments_gives_the_usage_text(self):
        done = btaptap()
        self.assertIn("Usage: btaptap", done.stderr)
        self.assertEqual(done.stdout, "")
        self.assertEqual(done.returncode, 1)

    def test_bad_command_lines(self):
        cap = pcap_file(self.path("one.pcap"), DLT_EN10MB,
                        [dm1_frame(l2cap_keystroke([0x04]))])
        cases = (
            # A described error gets the error line and no usage text.
            (("-c", "3"), "must specify"),
            (("-r",), "-r needs a value"),
            (("-c",), "-c needs a value"),
            (("-r", cap, "-c", "many"), "-c needs a number"),
            (("-r", cap, "-c", "-2"), "-c needs a count"),
            (("-r", cap, "-r", cap), "-r given twice"),
            (("-c", "1", "-c", "2", "-r", cap), "-c given twice"),
            (("-r", cap, "-e", ELLISYS_CSV), "two captures"),
            (("--count", "1"), "unknown option"),
            (("-r", cap, "extra"), "unknown option"),
        )
        for args, expected in cases:
            with self.subTest(args=args):
                done = btaptap(*args)
                self.assertEqual(done.returncode, 1)
                self.assertEqual(done.stdout, "")
                self.assertIn("error: ", done.stderr)
                self.assertIn(expected, done.stderr)
                self.assertNotIn("Usage:", done.stderr)


class PcapModule(CaptureFileTestCase):
    '''
    The module btaptap reads captures with, which the README offers on its
    own and which also writes them.
    '''

    def test_write_and_read_back(self):
        path = self.path("written.pcap")
        packets = [b"\x01\x02\x03", bytes(range(40))]

        dumper = PcapDumper(DLT_EN10MB, path)
        dumper.pcap_dump(packets[0], ts_sec=1700000000, ts_usec=5)
        dumper.pcap_dump(packets[1])
        dumper.close()

        reader = PcapReader(path)
        self.assertEqual(reader.datalink(), DLT_EN10MB)
        for expected in packets:
            header, packet = reader.pnext()
            self.assertEqual(packet, expected)
            self.assertEqual(header[1], len(expected))
            self.assertEqual(header[2], len(expected))
        self.assertEqual(reader.pnext(), [None, None])
        reader.close()

    def test_timestamps(self):
        path = self.path("stamped.pcap")

        dumper = PcapDumper(DLT_EN10MB, path)
        # Five microseconds past the second, which is 0.000005 s and not
        # five tenths of one.
        dumper.pcap_dump(b"\x00", ts_sec=1700000000, ts_usec=5)
        dumper.close()

        reader = PcapReader(path)
        self.addCleanup(reader.close)
        header, _ = reader.pnext()
        self.assertAlmostEqual(header[0], 1700000000.000005, places=6)

    def test_nanosecond_timestamps(self):
        # The second field counts nanoseconds in these, so the same number
        # is a thousandth of the time it is in a microsecond capture.
        path = pcap_file(self.path("nano.pcap"), DLT_EN10MB, [b"\x00"],
                         magic=PCAP_MAGIC_NANO, fraction=5000)

        reader = PcapReader(path)
        self.addCleanup(reader.close)
        header, _ = reader.pnext()
        self.assertAlmostEqual(header[0], 1700000000.000005, places=6)

    def test_a_written_capture_is_read_by_the_tool(self):
        path = self.path("tool.pcap")

        dumper = PcapDumper(DLT_EN10MB, path)
        dumper.pcap_dump(dm1_frame(l2cap_keystroke([0x04])))
        dumper.close()

        self.assertEqual(btaptap("-r", path).stdout, "a\n")

    def test_not_a_capture(self):
        path = self.path("text.pcap")
        with open(path, "wb") as fh:
            fh.write(b"this is not a capture at all, but it is long enough\n")

        with self.assertRaises(ValueError):
            PcapReader(path)


if __name__ == '__main__':
    unittest.main()
