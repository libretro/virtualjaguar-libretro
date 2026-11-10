#!/usr/bin/env python3
"""Unit tests for test/tools/cart_matrix_identify.py (issue #976).

The backfill must stamp what the probe would have stamped: the byte at
$800000 after the loader mapped the image (not the file's first byte), and
its audit must fail when it could not look at a file.  Synthetic images and a
synthetic filedb.c; stdlib only.
"""
import importlib.util
import io
import os
import shutil
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "cart_matrix_identify", ROOT / "test" / "tools" / "cart_matrix_identify.py")
ci = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ci)

MIB = ci.MIB
MARKER_OFF = ci.CART_UNIVERSAL_MARKER_OFFSET
REJECT = ("BIOS_REJECT", 'boot ROM halted at $0050B6 without handing off to the cart')
NO_BOOT = ("GAME_CODE", "video, silent")
HANDOFF = ("GAME_CODE", "video, audio (after handoff f492)")
UNKNOWN = ("LOAD_FAIL", "probe could not load the ROM")


def cart(size, byte0=0xF6):
    """A cartridge image: the $400 universal marker, byte 0 as given."""
    d = bytearray(b"\x00" * size)
    d[0] = byte0
    d[MARKER_OFF:MARKER_OFF + 4] = ci.CART_UNIVERSAL_MARKER
    return bytes(d)


def plain(size, head):
    d = bytearray(b"\x00" * size)
    d[:len(head)] = head
    return bytes(d)


FILEDB_C = '''
struct RomIdentifier romList[] = {
\t{ 0x%08X, "Good Game (World)", FF_ROM | FF_VERIFIED },
\t{ 0x%08X, "Good Game (World)", FF_ROM | FF_BAD_DUMP },
\t{ 0xFFFFFFFF, "***END***", 0 }
};
'''


class Byte0(unittest.TestCase):
    def test_boot_rom_ran_reading(self):
        self.assertIs(ci.boot_rom_ran(*REJECT), True)
        self.assertIs(ci.boot_rom_ran(*HANDOFF), True)
        self.assertIs(ci.boot_rom_ran("? (bios_trap)", "final_pc=$E005DC"), True)
        self.assertIs(ci.boot_rom_ran(*NO_BOOT), False)
        self.assertIs(ci.boot_rom_ran("? (trap_loop)", "68K stuck"), False)
        self.assertIsNone(ci.boot_rom_ran(*UNKNOWN))

    def test_ram_program_headers_leave_the_slot_untouched(self):
        for head in (b"\x60\x1B", b"\x01\x50", b"\x60\x1A"):
            self.assertEqual(ci.mapped_byte0(plain(4096, head), None), 0x00)

    def test_marker_cart_is_its_own_byte0(self):
        self.assertEqual(ci.mapped_byte0(cart(2 * MIB, 0xF6), None), 0xF6)
        self.assertEqual(ci.mapped_byte0(cart(300000, 0x23), None), 0x23)

    def test_exact_mib_without_marker_follows_the_row(self):
        img = plain(2 * MIB, b"\x33\xFC")          # Rayman Demo's shape (#973)
        self.assertEqual(ci.mapped_byte0(img, False), 0xFF, "Alpine: $FF fill")
        self.assertEqual(ci.mapped_byte0(img, True), 0x33, "cartridge mapping")
        self.assertIsNone(ci.mapped_byte0(img, None))
        erased = plain(2 * MIB, b"\xFF\xFF")        # Phase Zero (PD): both read $FF
        self.assertEqual(ci.mapped_byte0(erased, None), 0xFF)

    def test_alpine_shapes_read_the_fill(self):
        self.assertEqual(ci.mapped_byte0(plain(2 * MIB - 8192, b"\x72\x00"), None), 0xFF)
        stub = plain(4096, b"\x20\x79\x00\x80\x20\x54\x20\x39\x00\x80\x20\x10")
        self.assertTrue(ci.alpine_header_stub(stub))
        self.assertEqual(ci.mapped_byte0(stub, None), 0xFF)

    def test_headerless_small_image_follows_the_row(self):
        img = plain(268096, b"\x23\x00")             # the BJL Assassin copy
        self.assertEqual(ci.mapped_byte0(img, True), 0x23, "sub-1-MiB cart fallback")
        self.assertEqual(ci.mapped_byte0(img, False), 0x00, "RAM-loaded, slot untouched")
        self.assertIsNone(ci.mapped_byte0(img, None))

    def test_copier_header_is_stripped_first(self):
        img = b"\x00" * 512 + cart(2 * MIB, 0xF6)
        self.assertEqual(ci.prepended_header_size(img), 512)
        self.assertEqual(ci.prepended_header_size(cart(2 * MIB)), 0)


class Stamp(unittest.TestCase):
    def test_fields_match_the_probe_format(self):
        ident = {"size": 2097152, "crc32": 0x4A08A2BD, "hdr_byte0": 0xF6,
                 "entry": ("SuperCross 3D (World)", 0x41), "sibling": 0xEC22F572}
        self.assertEqual(
            ci.format_stamp(ident),
            '<!-- id: size=2097152 crc32=$4A08A2BD hdr_byte0=$F6 filedb=1 '
            'filedb_flags=$041 filedb_name="SuperCross 3D (World)" '
            'filedb_verified_sibling=$EC22F572 -->')
        ident.update(hdr_byte0=None, entry=None)
        self.assertEqual(ci.format_stamp(ident),
                         "<!-- id: size=2097152 crc32=$4A08A2BD filedb=0 -->")

    def test_compare_skips_only_an_undecided_byte0(self):
        row = ' size=10 crc32=$00000001 hdr_byte0=$F6 filedb=0'
        self.assertEqual(ci.compare_stamp(row, " size=10 crc32=$00000001 filedb=0"), [])
        self.assertEqual(ci.compare_stamp(row, row), [])
        self.assertTrue(ci.compare_stamp(row, " size=10 crc32=$00000001 hdr_byte0=$FF filedb=0"))
        self.assertTrue(ci.compare_stamp(row, " size=11 crc32=$00000001 filedb=0"))
        self.assertTrue(ci.compare_stamp(" size=10 crc32=$00000001 filedb=1 filedb_flags=$001",
                                         " size=10 crc32=$00000001 filedb=0"))


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.roms = self.tmp / "ROMS"
        self.roms.mkdir()
        self.good = cart(2 * MIB, 0xF6)
        self.bad = cart(2 * MIB, 0xF6) + b""
        self.bad = bytes(bytearray(self.bad[:-1]) + b"\x01")
        (self.roms / "Good Game (1995).jag").write_bytes(self.good)
        (self.roms / "Good Game (1995).rom").write_bytes(self.good)
        (self.roms / "Good Game (1995) [a1].jag").write_bytes(self.bad)
        (self.roms / "Rayman Demo (1995).jag").write_bytes(plain(2 * MIB, b"\x33\xFC"))
        self.filedb = self.tmp / "filedb.c"
        self.filedb.write_text(FILEDB_C % (zlib.crc32(self.good), zlib.crc32(self.bad)))
        self.md = self.tmp / "m.md"

    def matrix(self, rows, stamp=lambda title: ""):
        text = "# M\n\n| Title | HLE | HLE notes | Real BIOS | BIOS notes |\n|---|---|---|---|---|\n"
        for t, b, bn in rows:
            text += "| %s | GAME_CODE | video | %s | %s |<!-- build:x -->%s\n" % (t, b, bn, stamp(t))
        self.md.write_text(text)

    def run_tool(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = ci.main([str(self.md), "--roms", str(self.roms),
                          "--filedb", str(self.filedb)] + list(args))
        return rc, out.getvalue() + err.getvalue()

    def test_backfill_then_check(self):
        rows = [("Good Game (1995)", *HANDOFF), ("Good Game (1995)", *HANDOFF),
                ("Good Game (1995) [a1]", *REJECT), ("Rayman Demo (1995)", *NO_BOOT)]
        self.matrix(rows)
        rc, log = self.run_tool()
        self.assertEqual(rc, 0, log)
        text = self.md.read_text()
        self.assertIn('crc32=$%08X hdr_byte0=$F6 filedb=1 filedb_flags=$081 '
                      'filedb_name="Good Game (World)" filedb_verified_sibling=$00000000'
                      % zlib.crc32(self.good), text)
        self.assertIn('crc32=$%08X hdr_byte0=$F6 filedb=1 filedb_flags=$041 '
                      'filedb_name="Good Game (World)" filedb_verified_sibling=$%08X'
                      % (zlib.crc32(self.bad), zlib.crc32(self.good)), text)
        self.assertIn("hdr_byte0=$FF filedb=0", text, "Rayman: the Alpine $FF fill")
        self.assertEqual(text.count("<!-- id:"), 4)
        self.assertEqual(text.count("<!-- build:x -->"), 4, "build stamps untouched")
        rc, log = self.run_tool("--check")
        self.assertEqual(rc, 0, log)
        self.assertIn("checked 4 stamped rows, 0 mismatched, 0 rows without a file", log)

    def test_undecided_byte0_is_left_to_the_probe(self):
        self.matrix([("Rayman Demo (1995)", *UNKNOWN)])
        rc, log = self.run_tool()
        self.assertEqual(rc, 0, log)
        text = self.md.read_text()
        self.assertNotIn("hdr_byte0", text)
        self.assertIn("1 with hdr_byte0 left to the probe", log)
        # A probe-stamped row that carries the byte still checks clean.
        self.matrix([("Rayman Demo (1995)", *UNKNOWN)],
                    lambda t: "<!-- id: size=2097152 crc32=$%08X hdr_byte0=$FF filedb=0 -->"
                    % zlib.crc32(plain(2 * MIB, b"\x33\xFC")))
        rc, log = self.run_tool("--check")
        self.assertEqual(rc, 0, log)

    def test_check_fails_on_mismatch(self):
        self.matrix([("Good Game (1995)", *HANDOFF)],
                    lambda t: "<!-- id: size=2097152 crc32=$DEADBEEF hdr_byte0=$F6 filedb=0 -->")
        rc, log = self.run_tool("--check")
        self.assertEqual(rc, 1)
        self.assertIn("MISMATCH Good Game (1995)", log)
        self.assertIn("crc32: row=$DEADBEEF", log)

    def test_check_fails_when_a_file_is_missing_or_empty(self):
        (self.roms / "Empty (PD).jag").write_bytes(b"")
        rows = [("Good Game (1995)", *HANDOFF), ("Not Here (PD)", *NO_BOOT),
                ("Empty (PD)", *NO_BOOT)]
        self.matrix(rows, lambda t: "<!-- id: size=1 crc32=$00000001 filedb=0 -->")
        rc, log = self.run_tool("--check")
        self.assertEqual(rc, 1)
        self.assertIn("no file for row 'Not Here (PD)'", log)
        self.assertIn("empty file for row 'Empty (PD)'", log)
        self.assertIn("2 rows without a file", log)

    def test_check_fails_when_nothing_was_checked(self):
        self.matrix([("Good Game (1995)", *HANDOFF)])   # no stamps at all
        rc, log = self.run_tool("--check")
        self.assertEqual(rc, 1)
        self.assertIn("FAIL: nothing was checked", log)
        empty = self.tmp / "none"
        empty.mkdir()
        out = io.StringIO()
        with redirect_stdout(out), redirect_stderr(out):
            rc = ci.main([str(self.md), "--roms", str(empty), "--filedb",
                          str(self.filedb), "--check"])
        self.assertEqual(rc, 1)

    def test_backfill_reports_missing_files_as_failure(self):
        self.matrix([("Good Game (1995)", *HANDOFF), ("Not Here (PD)", *NO_BOOT)])
        rc, log = self.run_tool()
        self.assertEqual(rc, 1)
        self.assertIn("FAIL: 1 rows could not be stamped", log)
        self.assertEqual(self.md.read_text().count("<!-- id:"), 1, "the rest is still written")


if __name__ == "__main__":
    unittest.main()
