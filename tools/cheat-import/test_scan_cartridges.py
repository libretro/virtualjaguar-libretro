"""Synthetic cartridges only: no ROM bytes or paths from any real corpus."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location("scan_cartridges", HERE / "scan_cartridges.py")
scan = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = scan
spec.loader.exec_module(scan)
convert = scan.convert

ROM_A = bytes(range(256)) * 8
ROM_B = bytes(reversed(range(256))) * 8
# 512-byte copier header, then a payload whose 0x600 marker convert.rom_bytes detects.
HEADERED = bytes(0x600) + b"\x04\x04\x04\x04" + bytes(0x100000 + 512 - 0x604)


def row(data, title, name):
    return {"fnv1a64": f"{convert.fnv1a64(data):016X}", "crc32": f"{zlib.crc32(data):08X}",
            "size": len(data), "title": title, "name": name}


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = scan.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class ScanCartridgesTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.roms = self.tmp / "roms"
        self.roms.mkdir()

    def table(self, rows):
        path = self.tmp / "table.json"
        path.write_text(json.dumps({"format": 1, "cartridges": rows}))
        return path

    def test_scan_two_roms_sorted_with_hashes(self):
        (self.roms / "Zed Game (USA) [a1].J64").write_bytes(ROM_A)
        (self.roms / "sub").mkdir()
        (self.roms / "sub" / "Alpha (Europe) (Rev 1).bin").write_bytes(ROM_B)
        (self.roms / "notes.txt").write_bytes(b"ignored")
        (self.roms / "pack.7z").write_bytes(b"ignored")
        out = self.tmp / "new.json"
        code, _, err = run("--rom-dir", self.roms, "--output", out)
        self.assertEqual(code, 0, err)
        text = out.read_text()
        self.assertTrue(text.endswith("}\n"))
        doc = json.loads(text)
        want = sorted([row(ROM_A, "Zed Game", "Zed Game (USA) [a1]"),
                       row(ROM_B, "Alpha", "Alpha (Europe) (Rev 1)")], key=lambda r: r["fnv1a64"])
        self.assertEqual(doc, {"format": 1, "cartridges": want})
        self.assertNotIn(str(self.tmp), text)
        self.assertEqual(scan.load_cartridges(out)[want[0]["fnv1a64"]], want[0])

    def test_title_strips_bracket_tags_and_whitespace(self):
        self.assertEqual(scan.title_of("Foo [a1]"), "Foo")
        self.assertEqual(scan.title_of("Foo - Bar (USA) [b2] "), "Foo - Bar")
        self.assertEqual(scan.title_of("Plain"), "Plain")

    def test_copier_headered_rom_skipped_with_stderr_line(self):
        (self.roms / "Good.jag").write_bytes(ROM_A)
        (self.roms / "Headered.jag").write_bytes(HEADERED)
        out = self.tmp / "new.json"
        code, _, err = run("--rom-dir", self.roms, "--output", out)
        self.assertEqual(code, 0, err)
        self.assertEqual([r["name"] for r in json.loads(out.read_text())["cartridges"]], ["Good"])
        self.assertIn("Headered.jag", err)
        self.assertIn("skipped", err)

    def test_identical_bytes_dedupe_to_first_sorted_path(self):
        (self.roms / "b").mkdir()
        (self.roms / "a").mkdir()
        (self.roms / "b" / "Game (USA).jag").write_bytes(ROM_A)
        (self.roms / "a" / "Game (Europe).jag").write_bytes(ROM_A)
        out = self.tmp / "new.json"
        self.assertEqual(run("--rom-dir", self.roms, "--output", out)[0], 0)
        rows = json.loads(out.read_text())["cartridges"]
        self.assertEqual([r["name"] for r in rows], ["Game (Europe)"])

    def test_crc_collision_with_different_fnv_fails(self):
        a = row(ROM_A, "A", "A")
        b = row(ROM_B, "B", "B")
        b["crc32"] = a["crc32"]
        with self.assertRaises(ValueError):
            scan.merge_rows([a], [b])

    def test_merge_keeps_curated_title_and_appends_only(self):
        (self.roms / "Doomish (USA).jag").write_bytes(ROM_A)
        (self.roms / "Other (USA).jag").write_bytes(ROM_B)
        curated = row(ROM_A, "Curated Title", "Curated Name (USA)")
        existing = self.table([curated])
        out = self.tmp / "new.json"
        code, _, err = run("--rom-dir", self.roms, "--merge", existing, "--output", out)
        self.assertEqual(code, 0, err)
        rows = {r["fnv1a64"]: r for r in json.loads(out.read_text())["cartridges"]}
        self.assertEqual(rows[curated["fnv1a64"]], curated)
        self.assertEqual(rows[row(ROM_B, "", "")["fnv1a64"]]["title"], "Other")
        self.assertEqual(len(rows), 2)

    def test_merge_keeps_existing_rows_absent_from_scan(self):
        (self.roms / "Only (USA).jag").write_bytes(ROM_B)
        old = row(ROM_A, "Old", "Old (USA)")
        out = self.tmp / "new.json"
        self.assertEqual(run("--rom-dir", self.roms, "--merge", self.table([old]), "--output", out)[0], 0)
        self.assertEqual(len(json.loads(out.read_text())["cartridges"]), 2)

    def test_existing_output_refused_untouched(self):
        (self.roms / "G.jag").write_bytes(ROM_A)
        out = self.tmp / "new.json"
        out.write_text("keep")
        code, _, err = run("--rom-dir", self.roms, "--output", out)
        self.assertEqual(code, 2)
        self.assertEqual(out.read_text(), "keep")
        self.assertIn("exist", err)

    def test_missing_rom_dir_is_exit_2(self):
        self.assertEqual(run("--rom-dir", self.tmp / "nope", "--output", self.tmp / "x.json")[0], 2)

    def test_load_rejects_duplicate_crc32(self):
        a, b = row(ROM_A, "A", "A"), row(ROM_B, "B", "B")
        b["crc32"] = a["crc32"]
        with self.assertRaisesRegex(ValueError, "crc32"):
            scan.load_cartridges(self.table([a, b]))

    def test_load_rejects_duplicate_fnv(self):
        a = row(ROM_A, "A", "A")
        b = dict(row(ROM_B, "B", "B"), fnv1a64=a["fnv1a64"])
        with self.assertRaisesRegex(ValueError, "fnv1a64"):
            scan.load_cartridges(self.table([a, b]))

    def test_load_rejects_bad_hex_and_format(self):
        good = row(ROM_A, "A", "A")
        for field, bad in (("fnv1a64", "abc"), ("fnv1a64", "g" * 16), ("fnv1a64", good["fnv1a64"].lower()),
                           ("crc32", "12345"), ("crc32", "ZZZZZZZZ")):
            with self.subTest(field=field, bad=bad), self.assertRaises(ValueError):
                scan.load_cartridges(self.table([dict(good, **{field: bad})]))
        path = self.tmp / "v2.json"
        path.write_text(json.dumps({"format": 2, "cartridges": []}))
        with self.assertRaisesRegex(ValueError, "format"):
            scan.load_cartridges(path)

    def test_load_rejects_bad_shape(self):
        good = row(ROM_A, "A", "A")
        for bad in (dict(good, size=0), dict(good, size="9"), dict(good, extra=1),
                    {k: v for k, v in good.items() if k != "title"}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                scan.load_cartridges(self.table([bad]))


if __name__ == "__main__":
    unittest.main()
