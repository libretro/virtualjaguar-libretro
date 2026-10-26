"""Pack generator tests: synthetic sources only (test_convert.source), no network, no ROMs."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import urllib.error
import zlib

import test_convert
from test_convert import DIGEST, ROM, SIMPLE, source

spec = importlib.util.spec_from_file_location("build_packs", Path(__file__).with_name("build_packs.py"))
bp = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = bp
spec.loader.exec_module(bp)

HERE = Path(__file__).parent
CREDIT = ("Cheats by jguffer (https://github.com/jguffer/BigPEmu_Scripts), converted by the Virtual Jaguar project "
          "with the author's permission.")
CRC = f"{zlib.crc32(ROM):08X}"
OTHER = 0x1122334455667788


def cart(digest=DIGEST, crc=CRC, title="Synthetic Game", name="Synthetic Game (Test)"):
    return {"fnv1a64": f"{digest:016X}", "crc32": crc, "size": len(ROM), "title": title, "name": name}


TABLE = {f"{DIGEST:016X}": cart()}


def src(source_id="s", **over):
    doc = {"id": source_id, "kind": "github", "repo": "owner/repo", "branch": "main", "commit": "a" * 40,
           "commit_date": "2026-04-15", "include": ["scripts/cheat_*.c"], "exclude": [], "notes": {},
           "author": "someone", "credit": CREDIT, "permission": "Permission was granted.", "license": None}
    doc.update(over)
    return doc


def make(files, table=TABLE, sources=None, **kwargs):
    """files: {source id: {path: text}} -> build() output dict."""
    sources = sources or [src(i) for i in files]
    fetched = {i: {p: t.encode() if isinstance(t, str) else t for p, t in fs.items()} for i, fs in files.items()}
    return bp.build({"format": 1, "sources": sources}, table, fetched, **kwargs)


def load(files, key):
    return json.loads(files[key])


def parse_cht(data):
    text = data.decode()
    names = dict(re.findall(r'^cheat(\d+)_desc = "(.*)"$', text, re.M))
    codes = dict(re.findall(r'^cheat(\d+)_code = "(.*)"$', text, re.M))
    enables = re.findall(r"^cheat\d+_enable = (\w+)$", text, re.M)
    return [names[i] for i in sorted(names, key=int)], [codes[i] for i in sorted(codes, key=int)], enables


def comments(data):
    return [line for line in data.decode().splitlines() if line.startswith("#")]


def variant(old, new, body=SIMPLE, **kwargs):
    return source(body, **kwargs).replace(old, new)


class PackTests(unittest.TestCase):
    def test_single_pack(self):
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE)}})
        manifest = load(files, "manifest.json")
        pack = manifest["packs"][0]
        data = files[f"packs/{CRC}.cht"]
        self.assertEqual((pack["crc32"], pack["file"], pack["title"], pack["name"]), (CRC, f"packs/{CRC}.cht", "Synthetic Game", "Synthetic Game (Test)"))
        self.assertEqual((pack["bytes"], pack["sha256"]), (len(data), bp.sha256(data)))
        self.assertEqual((pack["cheats"], pack["writes"], pack["partial"], pack["updated"], pack["exceeds_core_limit"]),
                         (2, 2, False, "2026-04-15", False))
        self.assertEqual(pack["credit"], CREDIT)
        self.assertEqual(pack["sources"], [{"source": "s", "path": "scripts/cheat_a.c", "sha256": bp.sha256(source(SIMPLE).encode()),
                                            "converted": 2, "omitted": 0}])
        self.assertEqual((pack["notes"], pack["overlaps"], pack["messages"]), ([], [], []))
        names, codes, enables = parse_cht(data)
        self.assertEqual((names, enables), (["Grouped freeze", "Other freeze"], ["false", "false"]))
        text = data.decode()
        self.assertIn("# Credit: " + CREDIT, text)
        self.assertIn(f'vj_rom_crc32 = "{CRC}"\ncheats = 2\n', text)
        self.assertIn("# Source: owner/repo@" + "a" * 40 + " scripts/cheat_a.c", text)
        self.assertEqual(manifest["counts"], {"packs": 1, "cheats": 2, "partial_packs": 0, "unsupported_entries": 0})
        self.assertEqual(sorted(files), [".vj-cheat-packs", "README.md", "index.html", "manifest.json", f"packs/{CRC}.cht", "unsupported.json"])

    def test_header_title_not_duplicated(self):
        def first(table):
            files = make({"s": {"scripts/cheat_a.c": source(SIMPLE)}}, table=table)
            return comments(files[f"packs/{CRC}.cht"])[0]
        tail = "; nothing is enabled automatically."
        for title, name, label in (("Synthetic Game", "Synthetic Game (Test)", "Synthetic Game (Test)"),
                                   ("Synthetic Game", "Synthetic Game", "Synthetic Game"),
                                   ("Synthetic", "Other Release (1994)", "Synthetic (Other Release (1994))")):
            with self.subTest(title=title, name=name):
                table = {f"{DIGEST:016X}": cart(title=title, name=name)}
                self.assertEqual(first(table), "# Community cheats for " + label + tail)

    def test_partial_notes(self):
        text = variant('bigpemu_register_setting_bool(category, "Other freeze", 0)',
                       'bigpemu_register_setting_int_full(category, "Start At Level", 0, 0, 100, 1)')
        files = make({"s": {"scripts/cheat_a.c": text}})
        pack = load(files, "manifest.json")["packs"][0]
        self.assertTrue(pack["partial"])
        self.assertEqual(pack["cheats"], 1)
        self.assertEqual((pack["notes"][0]["setting"], pack["notes"][0]["reason"]), ("Start At Level", "parameter setting exceeds 64-value limit"))
        self.assertIn('# Omitted: "Start At Level" (scripts/cheat_a.c): parameter setting', files[f"packs/{CRC}.cht"].decode())
        (row,) = load(files, "unsupported.json")["entries"]
        self.assertEqual((row["stage"], row["crc32"], row["setting"], row["calls"]), ("setting", CRC, "Start At Level", []))

    def test_bounded_integer_pack_choices(self):
        text = variant('bigpemu_register_setting_bool(category, "Other freeze", 0)',
                       'bigpemu_register_setting_int_full(category, "Start At Level", 2, 0, 3, 1)',
                       body=SIMPLE.replace("0x1234", "b"))
        inputs = {"s": {"scripts/cheat_a.c": text}}
        files = make(inputs)
        self.assertEqual(files, make(inputs))
        names, codes, enables = parse_cht(files[f"packs/{CRC}.cht"])
        self.assertEqual(names, ["Grouped freeze", "Start At Level: 1", "Start At Level: 2", "Start At Level: 3"])
        self.assertEqual(codes, ["001F0000:AB", "001F0002:0001", "001F0002:0002", "001F0002:0003"])
        self.assertEqual(enables, ["false"]*4)
        pack = load(files, "manifest.json")["packs"][0]
        self.assertEqual(pack["overlaps"], [[1, 2], [1, 3], [2, 3]])
        self.assertFalse(pack["partial"])
        self.assertEqual(load(files, "unsupported.json")["entries"], [])

    def test_merge_dedupe(self):
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE), "scripts/cheat_b.c": source(SIMPLE)}})
        pack = load(files, "manifest.json")["packs"][0]
        self.assertEqual(pack["cheats"], 2)
        self.assertEqual([s["path"] for s in pack["sources"]], ["scripts/cheat_a.c", "scripts/cheat_b.c"])
        self.assertEqual(parse_cht(files[f"packs/{CRC}.cht"])[0], ["Grouped freeze", "Other freeze"])

    def test_same_name_suffix(self):
        other_title = variant("Synthetic Game", "Synthetic Virtual", SIMPLE.replace("0xab", "0xac"))
        same_title = source(SIMPLE.replace("0xab", "0xad"))
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE), "scripts/cheat_b.c": other_title, "scripts/cheat_c.c": same_title}})
        names, codes, _ = parse_cht(files[f"packs/{CRC}.cht"])
        self.assertEqual(names, ["Grouped freeze", "Other freeze", "Grouped freeze (Synthetic Virtual)", "Grouped freeze (c)"])
        self.assertEqual(len({n.casefold() for n in names}), len(names))
        self.assertEqual(len(codes), 4)

    def test_overlap_recorded(self):
        body = "if (a) { bigpemu_jag_write8(0x1f0000, 1); } if (b) { bigpemu_jag_write8(0x1f0000, 2); }"
        files = make({"s": {"scripts/cheat_a.c": source(body)}})
        pack = load(files, "manifest.json")["packs"][0]
        self.assertEqual(pack["overlaps"], [[0, 1]])
        self.assertIn("# Overlap: cheat0 and cheat1 write the same bytes; later wins (app order).", files[f"packs/{CRC}.cht"].decode())

    def test_two_revisions(self):
        load_two = f"flag = 0; if (digest == MATCH_HASH || digest == 0x{OTHER:016X}ULL) {{ flag = 1; }}"
        table = dict(TABLE)
        table[f"{OTHER:016X}"] = cart(OTHER, "0000ABCD", name="Synthetic Game (Rev 2)")
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE, load=load_two)}}, table=table)
        manifest = load(files, "manifest.json")
        self.assertEqual([p["crc32"] for p in manifest["packs"]], ["0000ABCD", CRC])
        self.assertEqual([p["name"] for p in manifest["packs"]], ["Synthetic Game (Rev 2)", "Synthetic Game (Test)"])
        self.assertIn('vj_rom_crc32 = "0000ABCD"', files["packs/0000ABCD.cht"].decode())

    def test_no_table_hit(self):
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE), "scripts/cheat_b.c": "int main(void) { return 0; }",
                            "scripts/cheat_c.c": "#define H 0x1234567890abcdefULL\n"}})
        rows = {r["path"]: r for r in load(files, "unsupported.json")["entries"]}
        self.assertEqual(set(rows), {"scripts/cheat_b.c", "scripts/cheat_c.c"})
        self.assertEqual((rows["scripts/cheat_b.c"]["stage"], rows["scripts/cheat_b.c"]["reason"]), ("identity", "no 64-bit literal found"))
        self.assertEqual(rows["scripts/cheat_c.c"]["reason"], "no known cartridge for literals [0x1234567890ABCDEF]")
        self.assertEqual(len(load(files, "manifest.json")["packs"]), 1)

    def test_unmatched_literal_beside_hit(self):
        text = source(SIMPLE) + "\n#define OTHER_CART 0x962CCBEDFAAC5F28ULL\n"
        files = make({"s": {"scripts/cheat_a.c": text}})
        self.assertEqual(len(load(files, "manifest.json")["packs"]), 1)
        (row,) = load(files, "unsupported.json")["entries"]
        self.assertEqual((row["stage"], row["crc32"], row["reason"]),
                         ("identity", None, "no known cartridge for literals [0x962CCBEDFAAC5F28]"))

    def test_module_error(self):
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE), "scripts/cheat_b.c": f"#define H 0x{DIGEST:016X}ULL\nint ;;; {{",
                            "scripts/cheat_c.c": source(SIMPLE) + "\nint *pointer;"}})
        rows = {r["path"]: r for r in load(files, "unsupported.json")["entries"]}
        self.assertEqual({r["stage"] for r in rows.values()}, {"module"})
        self.assertTrue(rows["scripts/cheat_b.c"]["reason"].startswith("parse error"), rows["scripts/cheat_b.c"]["reason"])
        self.assertIsNone(rows["scripts/cheat_b.c"]["crc32"])
        self.assertEqual(len(load(files, "manifest.json")["packs"]), 1)

    def test_zero_toggles_is_one_module_row(self):
        text = source("if (a) { bigpemu_jag_write8(0x1f0000, bigpemu_jag_read8(1)); } if (b) { bigpemu_jag_foo(); }")
        files = make({"s": {"scripts/cheat_a.c": text, "scripts/cheat_b.c": source(SIMPLE)}})
        (row,) = load(files, "unsupported.json")["entries"]
        self.assertEqual((row["stage"], row["crc32"], row["setting"]), ("module", CRC, None))
        self.assertTrue(row["reason"].startswith("no toggle converted: Grouped freeze: unsupported call bigpemu_jag_read8"))
        self.assertEqual(row["calls"], ["bigpemu_jag_foo", "bigpemu_jag_read8"])

    def test_api_usage(self):
        module_level = source(SIMPLE + "if (a && b) { bigpemu_jag_read8(0x1f0010); }")
        setting_level = source("if (a) { bigpemu_jag_write8(0x1f0000, 1); bigpemu_jag_made_up(); } if (b) { bigpemu_jag_write8(0x1f1000, 1); }")
        quoted = "// printf in a comment\nint x; /* memcpy */ const char *s = \"bigpemu_jag_write8 printf\";\n"
        files = make({"s": {"scripts/cheat_m.c": module_level, "scripts/cheat_s.c": setting_level,
                            "scripts/cheat_z.c": quoted + "#define H 0x1234567890abcdefULL\n"}})
        doc = load(files, "unsupported.json")
        rows = {r["path"]: r for r in doc["entries"]}
        self.assertEqual(rows["scripts/cheat_m.c"]["calls"], ["bigpemu_jag_read8"])
        self.assertEqual(rows["scripts/cheat_m.c"]["stage"], "module")
        self.assertEqual(rows["scripts/cheat_s.c"]["calls"], ["bigpemu_jag_made_up"])
        usage = doc["api_usage"]
        self.assertEqual(usage["bigpemu_jag_read8"], {"status": "unsupported", "sources": ["scripts/cheat_m.c"]})
        self.assertEqual(usage["bigpemu_jag_made_up"], {"status": "unmapped", "sources": ["scripts/cheat_s.c"]})
        self.assertEqual(usage["bigpemu_jag_write8"]["status"], "supported")
        self.assertEqual(usage["bigpemu_jag_write8"]["sources"], ["scripts/cheat_m.c", "scripts/cheat_s.c"])
        self.assertNotIn("printf", usage)
        self.assertNotIn("memcpy", usage)

    def test_messages(self):
        load_body = r'flag = 0; if (digest == MATCH_HASH) { printf_notify("Supported.\n"); printf_notify("x %d", flag); flag = 1; }'
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE + ' printf("log");', load=load_body)}})
        pack = load(files, "manifest.json")["packs"][0]
        self.assertEqual(pack["messages"], ["Supported."])
        self.assertIn("# Message: Supported.", comments(files[f"packs/{CRC}.cht"]))
        reasons = [(n["setting"], n["reason"]) for n in pack["notes"]]
        self.assertEqual(reasons, [(None, "script message not shown: x %d"), (None, "ignored call printf (log output is not reproduced)")])
        self.assertFalse(pack["partial"])

    def test_excluded_never_parsed(self):
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE), "scripts/cheat_x.c": b"\xff\xfe not C at all {{{ \x00"}},
                     sources=[src(exclude=[{"path": "scripts/cheat_x.c", "reason": "not ours"}])])
        (row,) = load(files, "unsupported.json")["entries"]
        self.assertEqual((row["stage"], row["reason"], row["path"]), ("excluded", "not ours", "scripts/cheat_x.c"))
        self.assertEqual(row["sha256"], bp.sha256(b"\xff\xfe not C at all {{{ \x00"))
        self.assertEqual(load(files, "unsupported.json")["api_usage"].get("bigpemu_get_module_handle", {}).get("sources"), ["scripts/cheat_a.c"])

    def test_notes_attach_to_rows(self):
        files = make({"s": {"scripts/cheat_d.c": "int x;"}, "t": {"scripts/cheat_a.c": source(SIMPLE)}},
                     sources=[src("s", notes={"scripts/cheat_d.c": "disc title"}), src("t")])
        (row,) = load(files, "unsupported.json")["entries"]
        self.assertEqual(row["note"], "disc title")

    def test_include_must_be_c(self):
        with self.assertRaises(ValueError):
            make({"s": {"scripts/cheat_a.c": source(SIMPLE)}}, sources=[src(include=["scripts/*.bigpcvm"])])

    def test_wrong_hash_activation_fails(self):
        with self.assertRaisesRegex(ValueError, "different cartridge hash"):
            make({"s": {"scripts/cheat_a.c": source(SIMPLE, load="flag = 0; if (digest != 0) { flag = 1; }")}})

    def test_core_limit_flag(self):
        # One source cannot exceed 256 writes (the converter checks every toggle combination); two can.
        def loops(count, base):
            return f"if (a) {{ for (int i=0; i<{count}; ++i) {{ bigpemu_jag_write8({base}+i, 1); }} }}"
        first = source(loops(200, "0x1f0000"))
        second = source(loops(57, "0x1f1000")).replace("Grouped freeze", "Second freeze").replace("Other freeze", "Third freeze")
        files = make({"s": {"scripts/cheat_a.c": first, "scripts/cheat_b.c": second}})
        pack = load(files, "manifest.json")["packs"][0]
        self.assertEqual((pack["cheats"], pack["writes"], pack["exceeds_core_limit"]), (2, 257, True))
        self.assertEqual(pack["notes"][0]["setting"], "Other freeze")

    def test_deterministic(self):
        args = {"s": {"scripts/cheat_a.c": source(SIMPLE), "scripts/cheat_b.c": "int x;"}}
        first, second = make(args), make(args)
        self.assertEqual(first, second)
        for key in ("manifest.json", "unsupported.json"):
            self.assertTrue(first[key].endswith(b"\n") and not first[key].endswith(b"\n\n"))
            self.assertEqual(first[key], bp.dumps(json.loads(first[key])).encode())
        for data in first.values():
            self.assertNotIn(b"\r", data)

    def test_provenance_passed_through(self):
        prov = {"converter_sha256": "1", "generator_sha256": "2", "cartridges_sha256": "3"}
        gen = load(make({"s": {"scripts/cheat_a.c": source(SIMPLE)}}, provenance=prov), "manifest.json")["generated_from"]
        self.assertEqual({k: gen[k] for k in prov}, prov)
        self.assertEqual(gen["sources"][0]["url"], "https://github.com/owner/repo/tree/" + "a" * 40)

    def test_budgets(self):
        args = {"s": {"scripts/cheat_a.c": source(SIMPLE)}}
        with self.assertRaisesRegex(ValueError, "limit"):
            make(args, max_pack_bytes=100)
        with self.assertRaisesRegex(ValueError, "limit"):
            make(args, max_total_bytes=100)
        make(args, max_pack_bytes=len(make(args)[f"packs/{CRC}.cht"]))

    def test_zero_packs_fails(self):
        with self.assertRaisesRegex(ValueError, "no packs"):
            make({"s": {"scripts/cheat_a.c": "int x;"}})

    def test_missing_fetch_and_bad_sources(self):
        with self.assertRaisesRegex(ValueError, "no files fetched"):
            bp.build({"format": 1, "sources": [src()]}, TABLE, {})
        for bad in ({"kind": "local"}, {"commit": "abc"}, {"id": "Bad Id"}, {"extra": 1}):
            with self.assertRaises(ValueError):
                make({"s": {"scripts/cheat_a.c": source(SIMPLE)}}, sources=[src(**bad)])

    def test_index_and_readme(self):
        table = {f"{DIGEST:016X}": cart(title="<b>Game</b> & \"Co\"", name="N<script>alert(1)</script>")}
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE)}}, table=table)
        page = files["index.html"].decode()
        self.assertIn('<meta name="robots" content="noindex">', page)
        self.assertNotIn("<script>", page)
        self.assertNotIn("<b>Game</b>", page)
        self.assertIn("&lt;b&gt;Game&lt;/b&gt; &amp;", page)
        self.assertIn(f'<a href="packs/{CRC}.cht">', page)
        self.assertIn('href="unsupported.json"', page)
        readme = files["README.md"].decode()
        self.assertIn(CREDIT, readme)
        self.assertIn("Permission was granted.", readme)
        self.assertIn("tools/cheat-import", readme)


class OutputTests(unittest.TestCase):
    def test_write_tree_marker(self):
        files = make({"s": {"scripts/cheat_a.c": source(SIMPLE)}})
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "cheats"
            bp.write_tree(out, files)
            (out / "packs" / "STALE.cht").write_bytes(b"x")
            bp.write_tree(out, files)
            self.assertFalse((out / "packs" / "STALE.cht").exists())
            self.assertEqual({p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()}, set(files))
            other = Path(directory) / "other"
            other.mkdir()
            (other / "keep.txt").write_text("x")
            with self.assertRaisesRegex(ValueError, "marker"):
                bp.write_tree(other, files)
            self.assertTrue((other / "keep.txt").exists())


class CliTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = Path(self.dir.name)
        (self.root / "checkout" / "scripts").mkdir(parents=True)
        (self.root / "checkout" / "scripts" / "cheat_a.c").write_text(source(SIMPLE))
        (self.root / "checkout" / "scripts" / "readme.txt").write_text("ignored")
        (self.root / "checkout" / "descriptions.txt").write_text("ignored")
        self.sources = self.root / "sources.json"
        self.sources.write_bytes(bp.dumps({"format": 1, "sources": [src()]}).encode())
        self.cartridges = self.root / "cartridges.json"
        self.cartridges.write_text(json.dumps({"format": 1, "cartridges": list(TABLE.values())}))

    def run_cli(self, *extra, out="out"):
        argv = ["--sources", str(self.sources), "--cartridges", str(self.cartridges), "--out", str(self.root / out), *extra]
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            code = bp.main(argv)
        return code, err.getvalue()

    def test_local_run_and_provenance(self):
        code, err = self.run_cli("--source-dir", f"s={self.root / 'checkout'}", "--no-network")
        self.assertEqual((code, err), (0, ""))
        manifest = json.loads((self.root / "out" / "manifest.json").read_text())
        self.assertEqual(manifest["generated_from"]["converter_sha256"], bp.sha256((HERE / "convert.py").read_bytes()))
        self.assertEqual(manifest["generated_from"]["generator_sha256"], bp.sha256((HERE / "build_packs.py").read_bytes()))
        self.assertEqual(manifest["generated_from"]["cartridges_sha256"], bp.sha256(self.cartridges.read_bytes()))
        self.assertEqual(manifest["counts"]["packs"], 1)
        code, _ = self.run_cli("--source-dir", f"s={self.root / 'checkout'}")  # marker present: regenerates in place
        self.assertEqual(code, 0)
        self.assertEqual(self.run_cli("--source-dir", f"s={self.root / 'checkout'}", "--max-pack-bytes", "10", out="small")[0], 1)

    def test_exit_codes(self):
        self.assertEqual(self.run_cli("--no-network")[0], 2)
        with mock.patch.object(bp.urllib.request, "urlopen", side_effect=urllib.error.URLError("down")):
            self.assertEqual(self.run_cli()[0], 2)
        (self.root / "checkout" / "scripts" / "cheat_a.c").write_text("int x;")
        code, err = self.run_cli("--source-dir", f"s={self.root / 'checkout'}")
        self.assertEqual(code, 1)
        self.assertIn("no packs", err)
        self.assertEqual(self.run_cli("--source-dir", "nope", "--no-network")[0], 1)

    def test_bump(self):
        sha = "b" * 40
        self.assertEqual(bp.main(["--sources", str(self.sources), "--bump", f"s={sha}:2026-05-01"]), 0)
        doc = json.loads(self.sources.read_text())
        expect = {"format": 1, "sources": [src()]}
        expect["sources"][0].update(commit=sha, commit_date="2026-05-01")
        self.assertEqual(doc, expect)
        self.assertEqual(self.sources.read_text(), bp.dumps(expect))
        for bad in ("s=abc:2026-05-01", f"missing={sha}:2026-05-01", f"s={sha}:May"):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(bp.main(["--sources", str(self.sources), "--bump", bad]), 1)

    def test_bump_committed_file_changes_only_the_pin(self):
        text = (HERE / "sources.json").read_text()
        doc = json.loads(text)
        self.assertEqual(text, bp.dumps(doc))
        with tempfile.TemporaryDirectory() as directory:
            copy_path = Path(directory) / "sources.json"
            copy_path.write_text(text)
            self.assertEqual(bp.main(["--sources", str(copy_path), "--bump", "jguffer=" + "c" * 40 + ":2026-06-01"]), 0)
            changed = [(a, b) for a, b in zip(text.splitlines(), copy_path.read_text().splitlines()) if a != b]
            self.assertEqual(len(changed), 2)
            self.assertTrue(all(("commit" in a and "commit" in b) for a, b in changed))

    def test_committed_sources(self):
        doc = json.loads((HERE / "sources.json").read_text())
        (entry,) = bp.check_sources(doc)
        # The pin itself moves with every cheat-packs-bump PR, so only its shape is asserted.
        self.assertEqual((entry["repo"], entry["branch"]), ("jguffer/BigPEmu_Scripts", "main"))
        self.assertRegex(entry["commit"], r"^[0-9a-f]{40}$")
        self.assertRegex(entry["commit_date"], r"^\d{4}-\d{2}-\d{2}$")
        self.assertEqual(entry["credit"], CREDIT)
        self.assertEqual(entry["include"], ["scripts/cheat_*.c"])
        self.assertEqual([e["path"] for e in entry["exclude"]], ["scripts/cheat_aliens_vs_predator.c"])
        self.assertIn("jguffer, the author of BigPEmu_Scripts and of #1050, gave the maintainer permission to convert and use the scripts",
                      entry["permission"])
        self.assertEqual(sorted(entry["notes"]), ["scripts/cheat_baldies.c", "scripts/cheat_battlemorph.c", "scripts/cheat_highlander.c"])


class FetchTests(unittest.TestCase):
    def tarball(self, members):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
            for name, payload in members:
                if payload is None:
                    info = tarfile.TarInfo(name)
                    info.type, info.linkname = tarfile.SYMTYPE, "/etc/passwd"
                    archive.addfile(info)
                else:
                    info = tarfile.TarInfo(name)
                    info.size = len(payload)
                    archive.addfile(info, io.BytesIO(payload))
        return buffer.getvalue()

    def fetch(self, blob, include=("scripts/cheat_*.c",)):
        response = mock.MagicMock()
        response.__enter__.return_value.read = lambda n=-1: blob[:n] if n >= 0 else blob
        with mock.patch.object(bp.urllib.request, "urlopen", return_value=response) as opened:
            result = bp.fetch_github("owner/repo", "a" * 40, list(include))
        self.assertEqual(opened.call_args[0][0].full_url, "https://codeload.github.com/owner/repo/tar.gz/" + "a" * 40)
        self.assertEqual(opened.call_args[1]["timeout"], 60)
        return result

    def test_filtering_and_prefix(self):
        blob = self.tarball([("repo-aaa/scripts/cheat_a.c", b"A"), ("repo-aaa/scripts/cheat_b.bigpcvm", b"B"),
                             ("repo-aaa/README.md", b"R"), ("repo-aaa/scripts/cheat_link.c", None),
                             ("repo-aaa/scripts/cheat_big.c", b"x" * ((1 << 20) + 10))])
        got = self.fetch(blob)
        self.assertEqual(sorted(got), ["scripts/cheat_a.c", "scripts/cheat_big.c"])
        self.assertEqual(got["scripts/cheat_a.c"], b"A")
        self.assertEqual(len(got["scripts/cheat_big.c"]), (1 << 20) + 1)  # one past the cap: the converter refuses it

    def test_incomplete_read_is_fetch_error(self):
        for error in (bp.http.client.IncompleteRead(b"x"), bp.http.client.BadStatusLine("x")):
            with mock.patch.object(bp.urllib.request, "urlopen", side_effect=error):
                with self.assertRaises(bp.FetchError):
                    bp.fetch_github("owner/repo", "a" * 40, ["scripts/cheat_*.c"])

    def test_member_count_cap(self):
        blob = self.tarball([(f"repo-aaa/scripts/e{i}.txt", b"") for i in range(20)])
        with mock.patch.object(bp, "MAX_MEMBERS", 10):
            with self.assertRaisesRegex(bp.FetchError, "too many members"):
                self.fetch(blob)
        self.assertEqual(self.fetch(blob), {})
        with mock.patch.object(bp, "SCAN_CAP", 512 * 5):  # empty members still cost a header block each
            with self.assertRaises(bp.FetchError):
                self.fetch(blob)

    def test_archive_cap_and_garbage(self):
        with mock.patch.object(bp, "FETCH_CAP", 10):
            with self.assertRaises(bp.FetchError):
                self.fetch(b"x" * 50)
        with self.assertRaises(bp.FetchError):
            self.fetch(b"not a tarball")

    def test_control_character_paths_refused(self):
        for bad in ("scripts/cheat_a\nb.c", "scripts/cheat_a\x1b[0m.c", "scripts/cheat_a\x7f.c", "scripts/\x1fx/note.txt"):
            with self.subTest(path=bad):
                with self.assertRaisesRegex(bp.FetchError, "control character"):
                    self.fetch(self.tarball([("repo-aaa/scripts/cheat_ok.c", b"A"), ("repo-aaa/" + bad, b"B")]))

    @unittest.skipIf(sys.platform == "win32", "control characters are not valid Windows file names")
    def test_local_dir_control_character_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "scripts").mkdir()
            (Path(directory) / "scripts" / "cheat_ok.c").write_bytes(b"A")
            self.assertEqual(sorted(bp.read_dir(Path(directory), ["scripts/cheat_*.c"])), ["scripts/cheat_ok.c"])
            (Path(directory) / "scripts" / "note\x1b[31m.txt").write_bytes(b"N")  # not matched by include, still refused
            with self.assertRaisesRegex(ValueError, "control character"):
                bp.read_dir(Path(directory), ["scripts/cheat_*.c"])

    def test_local_dir_matches_fetch(self):
        members = [("repo-aaa/scripts/cheat_a.c", b"A"), ("repo-aaa/scripts/note.txt", b"N"), ("repo-aaa/README.md", b"R")]
        with tempfile.TemporaryDirectory() as directory:
            for name, payload in members:
                path = Path(directory) / name.split("/", 1)[1]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)
            self.assertEqual(bp.read_dir(Path(directory), ["scripts/cheat_*.c"]), self.fetch(self.tarball(members)))


class SummaryTests(unittest.TestCase):
    def tree(self, directory, text):
        files = make({"s": {"scripts/cheat_a.c": text}})
        bp.write_tree(directory, files)
        return str(directory / "manifest.json")

    def test_summary(self):
        old_text = source(SIMPLE)
        new_text = variant("0xab", "0xac", SIMPLE).replace("Other freeze", "Renamed freeze")
        with tempfile.TemporaryDirectory() as directory:
            old = self.tree(Path(directory) / "old", old_text)
            new = self.tree(Path(directory) / "new", new_text)
            markdown = bp.summarize(old, new)
            self.assertIn('added "Renamed freeze"', markdown)
            self.assertIn('changed "Grouped freeze"', markdown)
            self.assertIn('removed "Other freeze"', markdown)
            self.assertIn("Synthetic Game", markdown)
            self.assertIn("Packs: 1 -> 1", markdown)
            self.assertIn("No pack changes", bp.summarize(old, old))
            (Path(directory) / "new" / f"packs/{CRC}.cht").unlink()
            self.assertIn("sha256", bp.summarize(old, new))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(bp.main(["--summary", old, new]), 0)
            self.assertIn("Community cheat pack changes", out.getvalue())


if __name__ == "__main__":
    unittest.main()
