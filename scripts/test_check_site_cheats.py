#!/usr/bin/env python3
"""Unit tests for check_site.check_cheats (stdlib only)."""
import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_site  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
CHT = ('cheats = 1\n\nvj_rom_crc32 = "0509C85E"\n'
       'cheat0_desc = "x"\ncheat0_enable = false\n').encode()


def build(root):
    """A minimal valid <root>/cheats tree."""
    c = root / "cheats"
    (c / "packs").mkdir(parents=True)
    (c / "packs" / "0509C85E.cht").write_bytes(CHT)
    manifest = {"format": 1, "packs": [{
        "file": "packs/0509C85E.cht", "crc32": "0509C85E",
        "sha256": hashlib.sha256(CHT).hexdigest(), "bytes": len(CHT),
        "messages": ["hello"]}]}
    (c / "manifest.json").write_text(json.dumps(manifest))
    (c / "unsupported.json").write_text("{}")
    (c / "index.html").write_text(
        '<a href="packs/0509C85E.cht">p</a><a href="https://x.test/">x</a>')
    return c


class CheckCheats(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.c = build(self.tmp)

    def problems(self):
        return check_site.check_cheats(self.tmp)

    def rewrite_manifest(self, fn):
        p = self.c / "manifest.json"
        m = json.loads(p.read_text())
        fn(m)
        p.write_text(json.dumps(m))

    def test_absent_is_skipped(self):
        self.assertEqual(check_site.check_cheats(self.tmp / "nope"), [])

    def test_valid(self):
        self.assertEqual(self.problems(), [])

    def test_corrupt_pack_names_file(self):
        f = self.c / "packs" / "0509C85E.cht"
        b = bytearray(f.read_bytes())
        b[-3] ^= 1
        f.write_bytes(bytes(b))
        out = self.problems()
        self.assertTrue(any("0509C85E.cht" in p and "sha256" in p for p in out), out)

    def test_wrong_size(self):
        self.rewrite_manifest(lambda m: m["packs"][0].update(bytes=1))
        self.assertTrue(any("bytes" in p for p in self.problems()))

    def test_crc_mismatch(self):
        self.rewrite_manifest(lambda m: m["packs"][0].update(crc32="DEADBEEF"))
        self.assertTrue(any("vj_rom_crc32" in p for p in self.problems()))

    def test_missing_pack(self):
        (self.c / "packs" / "0509C85E.cht").unlink()
        self.assertTrue(any("missing" in p for p in self.problems()))

    def test_unlisted_cht(self):
        (self.c / "packs" / "AAAAAAAA.cht").write_bytes(CHT)
        self.assertTrue(any("AAAAAAAA.cht" in p for p in self.problems()))

    def test_bad_format(self):
        self.rewrite_manifest(lambda m: m.update(format=2))
        self.assertTrue(any("format" in p for p in self.problems()))

    def test_manifest_not_json(self):
        (self.c / "manifest.json").write_text("{")
        self.assertTrue(any("does not parse" in p for p in self.problems()))

    def test_unsupported_not_json(self):
        (self.c / "unsupported.json").write_text("{")
        self.assertTrue(any("unsupported.json" in p for p in self.problems()))

    def test_broken_index_link(self):
        (self.c / "index.html").write_text('<a href="packs/NOPE.cht">p</a>')
        self.assertTrue(any("NOPE.cht" in p for p in self.problems()))

    def test_messages_must_be_strings(self):
        self.rewrite_manifest(lambda m: m["packs"][0].update(messages=[1]))
        self.assertTrue(any("messages" in p for p in self.problems()))

    def test_messages_optional(self):
        self.rewrite_manifest(lambda m: m["packs"][0].pop("messages"))
        self.assertEqual(self.problems(), [])

    def test_manifest_too_big(self):
        self.rewrite_manifest(lambda m: m.update(pad="x" * (256 * 1024)))
        self.assertTrue(any("manifest.json is" in p for p in self.problems()))

    def test_tree_too_big(self):
        (self.c / "packs" / "pad.bin").write_bytes(b"\0" * (1024 * 1024))
        self.assertTrue(any("tree is" in p for p in self.problems()))

    def test_committed_tree_is_valid(self):
        stage = self.tmp / "real"
        stage.mkdir()
        shutil.copytree(REPO / "site" / "cheats", stage / "cheats")
        (stage / "app.html").write_text("")  # index.html links ../app.html
        self.assertEqual(check_site.check_cheats(stage), [])


if __name__ == "__main__":
    unittest.main()
