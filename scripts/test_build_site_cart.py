#!/usr/bin/env python3
"""Unit tests for the cart-matrix labels in build_site.py (issue #976).

Every label is derived from row data (stage, notes, the `<!-- id: ... -->`
identity stamp, same-title siblings); one case per label, plus the twin
collapse and the filedb display name.  Stdlib only, synthetic rows.
"""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_site as bs  # noqa: E402

REJECT = ('boot ROM halted at $0050B6 without handing off to the cart '
          '(red "Jaguar" reject screen)')
BLACK = "black video (headless - undetermined), silent"
TRAP_GUARD = ("68K stuck in an exception loop for the last 600 frames, no "
              "picture: fault PC $004498, handler $001000")
TRAP_RTE = ("68K stuck in an exception loop for the last 600 frames, no "
            "picture: fault PC $80B780, handler $000404")

BAD_DUMP = ("Rejected by the real boot ROM: altered or bad dump "
            "(a verified dump boots)")
UNVERIFIED = "Rejected by the real boot ROM (dump cannot be verified)"
UNSIGNED = "Unsigned image: the real boot ROM does not accept it"


def row(title, hle="GAME_CODE", hle_notes="video, audio",
        bios="BIOS_REJECT", bios_notes=REJECT, **ident):
    r = {"title": title, "hle_stage": hle, "hle_notes": hle_notes,
         "bios_stage": bios, "bios_notes": bios_notes}
    r.update(ident)
    return r


class IdentityStamp(unittest.TestCase):
    def test_full_stamp(self):
        got = bs.parse_identity_fields(
            'size=2097152 crc32=$4A08A2BD hdr_byte0=$F6 filedb=1 '
            'filedb_flags=$041 filedb_name="SuperCross 3D (World)" '
            'filedb_verified_sibling=$EC22F572')
        self.assertEqual(got, {"size": 2097152, "crc32": "4A08A2BD",
                               "hdr_byte0": 0xF6, "filedb_flags": 0x41,
                               "filedb_name": "SuperCross 3D (World)",
                               "filedb_sibling": "EC22F572"})

    def test_unknown_crc_has_no_filedb_keys(self):
        got = bs.parse_identity_fields(
            "size=2097152 crc32=$F7F3462B hdr_byte0=$F6 filedb=0")
        self.assertEqual(got, {"size": 2097152, "crc32": "F7F3462B",
                               "hdr_byte0": 0xF6})

    def test_zero_sibling_is_none(self):
        got = bs.parse_identity_fields(
            'size=1 crc32=$1 hdr_byte0=$F6 filedb=1 filedb_flags=$081 '
            'filedb_name="X" filedb_verified_sibling=$00000000')
        self.assertNotIn("filedb_sibling", got)
        self.assertEqual(got["crc32"], "00000001")


class BiosRejectLabels(unittest.TestCase):
    """One case per cause the issue lists; never 'Not yet verified'."""

    def classify(self, r, rows=()):
        fam = bs.verified_dump_families(list(rows) + [r])
        return bs.classify_cart_stage(r["bios_stage"], r["bios_notes"],
                                      "bios", r, fam)

    def test_filedb_bad_dump_flag(self):
        r = row("Super Cross 3D (1995)", crc32="4A08A2BD", hdr_byte0=0xF6,
                size=2097152, filedb_flags=0x41,
                filedb_name="SuperCross 3D (World)")
        self.assertEqual(self.classify(r), ("info", BAD_DUMP))

    def test_filedb_verified_sibling(self):
        r = row("Some Game [a1]", crc32="C6C7BA62", hdr_byte0=0xF6,
                size=4194304, filedb_flags=0x01, filedb_name="Some Game (alt)",
                filedb_sibling="B14C4753")
        self.assertEqual(self.classify(r), ("info", BAD_DUMP))

    def test_matrix_sibling_that_boots(self):
        good = row("Iron Soldier (1994)", bios="GAME_CODE",
                   bios_notes="video, audio (after handoff f492)",
                   crc32="08F15576", hdr_byte0=0xF6, size=2097152)
        r = row("Iron Soldier (1994) [a1]", crc32="F7F3462B", hdr_byte0=0xF6,
                size=2097152)
        self.assertEqual(self.classify(r, [good]), ("info", BAD_DUMP))

    def test_sibling_of_another_size_is_a_different_build(self):
        good = row("Fight For Your Life (1996)", bios="GAME_CODE",
                   bios_notes="video, audio (after handoff f611)",
                   crc32="B14C4753", hdr_byte0=0xF6, size=4194304)
        r = row("Fight For Your Life (1996) [a2]", hle="? (trap_loop)",
                hle_notes=TRAP_RTE, crc32="2ABADB3D", hdr_byte0=0xF6,
                size=2097152)
        self.assertEqual(self.classify(r, [good]), ("plain", UNVERIFIED))

    def test_signed_but_unverifiable(self):
        r = row("Ultra Vortek (Beta) (1995)", crc32="A27823D8",
                hdr_byte0=0xF6, size=4194304, filedb_flags=0x01,
                filedb_name="Ultra Vortek (World) (v0.94) (Beta)")
        self.assertEqual(self.classify(r), ("plain", UNVERIFIED))

    def test_unsigned_image_that_works_in_hle(self):
        # The signed sibling [a1] boots, but with the boot blocks erased the
        # cause is the missing signature, not the altered bytes.
        signed = row("Phase Zero (2000) (PD) [a1]", bios="GAME_CODE",
                     bios_notes="video, audio (after handoff f492)",
                     crc32="EA9B3FA7", hdr_byte0=0xF6, size=2097152)
        r = row("Phase Zero (2000) (PD)", crc32="4203E23D", hdr_byte0=0xFF,
                size=2097152)
        self.assertEqual(self.classify(r, [signed]),
                         ("plain", UNSIGNED + " (works in HLE)"))

    def test_curated_bad_dump_flag_wins_over_byte0(self):
        r = row("Brutal Sports Football (1994)", crc32="0FDCEB66",
                hdr_byte0=0x00, size=2097152, filedb_flags=0x41,
                filedb_name="Brutal Sports Football (World)")
        self.assertEqual(self.classify(r), ("info", BAD_DUMP))

    def test_unsigned_image_that_crashes_in_hle_claims_nothing(self):
        r = row("Assassin Demo, The Part 1 (for BJL) (1999) (PD)",
                hle="? (trap_loop)", hle_notes=TRAP_GUARD,
                crc32="D5C01E77", hdr_byte0=0x23, size=268096)
        self.assertEqual(self.classify(r), ("plain", UNSIGNED))

    def test_row_without_identity_states_the_fact(self):
        r = row("Old Row")
        cls, label = self.classify(r)
        self.assertEqual((cls, label), ("plain", "Rejected by the real boot ROM"))

    def test_legacy_call_without_row(self):
        cls, label = bs.classify_cart_stage("BIOS_REJECT", REJECT, "bios")
        self.assertEqual(cls, "plain")
        self.assertNotIn("Not yet verified", label)

    def test_never_not_yet_verified(self):
        for r in (row("a"), row("b", hdr_byte0=0xF6), row("c", hdr_byte0=0),
                  row("d", filedb_flags=0x40)):
            for cls, label in (self.classify(r),):
                self.assertNotIn("Not yet verified", label)
                self.assertNotEqual(cls, "warn")


class RunLabels(unittest.TestCase):
    def test_ram_loaded_executable_in_bios_column(self):
        self.assertEqual(
            bs.classify_cart_stage("GAME_CODE", BLACK, "bios"),
            ("plain", "Boot ROM not used (RAM-loaded executable)"))
        self.assertEqual(
            bs.classify_cart_stage("GAME_CODE", "static video, audio", "bios"),
            ("good", "Runs (boot ROM not used: RAM-loaded executable)"))

    def test_black_video_alive_loop(self):
        self.assertEqual(
            bs.classify_cart_stage("GAME_CODE", BLACK, "hle"),
            ("warn", "Runs; no picture in the test "
                     "(may be audio-only or need input)"))
        self.assertEqual(
            bs.classify_cart_stage("GAME_CODE", BLACK + " (after handoff f492)",
                                   "bios")[0], "warn")

    def test_trap_parked_in_the_illegal_guard(self):
        self.assertEqual(
            bs.classify_cart_stage("? (trap_loop)", TRAP_GUARD, "hle"),
            ("bad", "Known issue: crashes at start (illegal instruction)"))

    def test_trap_loop_elsewhere(self):
        self.assertEqual(
            bs.classify_cart_stage("? (trap_loop)", TRAP_RTE, "hle"),
            ("bad", "Known issue: crashes at start (68K exception loop)"))

    def test_boots_and_runs(self):
        self.assertEqual(
            bs.classify_cart_stage("GAME_CODE",
                                   "video, audio (after handoff f492)", "bios"),
            ("good", "Boots and runs"))


class Twins(unittest.TestCase):
    def test_same_bytes_same_verdict_collapse(self):
        a = row("Rayman Demo (1995) (UBI Soft)", bios="GAME_CODE",
                bios_notes="video, silent", crc32="E6BD422C")
        b = dict(a)
        out = bs.collapse_twin_rows([a, b])
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0]["files"], 2)

    def test_same_bytes_under_two_names_keep_both_names(self):
        a = row("Iron Soldier (1994)", bios="GAME_CODE", bios_notes="video",
                crc32="08F15576", filedb_name="Iron Soldier (World) (v1.04)")
        b = dict(a, title="Iron Soldier")
        out = bs.collapse_twin_rows([a, b, dict(a)])
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0]["files"], 3)
        self.assertEqual(out[0]["titles"], ["Iron Soldier (1994)", "Iron Soldier"])
        self.assertEqual(bs.cart_title_html(out[0]),
                         'Iron Soldier (World) (v1.04)<br><small class="dim">'
                         'Iron Soldier (1994) / Iron Soldier</small>')
        self.assertEqual(bs.cart_title_html(row("Plain")), "Plain")

    def test_same_bytes_different_verdict_stay(self):
        a = row("X", crc32="E6BD422C")
        b = row("X", bios="GAME_CODE", bios_notes="video", crc32="E6BD422C")
        self.assertEqual(len(bs.collapse_twin_rows([a, b])), 2)

    def test_rows_without_crc_never_collapse(self):
        self.assertEqual(len(bs.collapse_twin_rows([row("X"), row("X")])), 2)

    def test_family_key(self):
        k = bs.dump_family_key
        self.assertEqual(k("Iron Soldier (1994) [a1]"), k("Iron Soldier (1994)"))
        self.assertEqual(k("Raiden (World) (alt)"), k("Raiden (World)"))
        self.assertNotEqual(k("Iron Soldier (1994)"), k("Iron Soldier 2 (1994)"))


MATRIX = """# Cartridge boot matrix

prose

| Title | HLE | HLE notes | Real BIOS | BIOS notes |
|---|---|---|---|---|
| Legacy Row | GAME_CODE | video, audio | BIOS_REJECT | %(rej)s |<!-- build:aaaaaaaaaaaa -->
| Rayman Demo (1995) (UBI Soft) | GAME_CODE | video, silent | GAME_CODE | video, silent |<!-- build:aaaaaaaaaaaa --><!-- id: size=2097152 crc32=$E6BD422C hdr_byte0=$FF filedb=0 -->
| Rayman Demo (1995) (UBI Soft) | GAME_CODE | video, silent | GAME_CODE | video, silent |<!-- build:aaaaaaaaaaaa --><!-- id: size=2097152 crc32=$E6BD422C hdr_byte0=$FF filedb=0 -->
| Super Cross 3D (1995) [a1] | GAME_CODE | video, audio | GAME_CODE | video, audio (after handoff f492) |<!-- build:aaaaaaaaaaaa --><!-- id: size=2097152 crc32=$EC22F572 hdr_byte0=$F6 filedb=1 filedb_flags=$081 filedb_name="SuperCross 3D (World)" filedb_verified_sibling=$00000000 -->
| Super Cross 3D (1995) | GAME_CODE | video, audio | BIOS_REJECT | %(rej)s |<!-- build:aaaaaaaaaaaa --><!-- id: size=2097152 crc32=$4A08A2BD hdr_byte0=$F6 filedb=1 filedb_flags=$041 filedb_name="SuperCross 3D (World)" filedb_verified_sibling=$EC22F572 -->
| Music Demo (2002) | GAME_CODE | %(black)s | GAME_CODE | %(black)s |<!-- build:aaaaaaaaaaaa --><!-- id: size=1161856 crc32=$33D8C132 hdr_byte0=$00 filedb=0 -->
""" % {"rej": REJECT, "black": BLACK}


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.md = self.tmp / "cart-boot-matrix.md"
        self.md.write_text(MATRIX, encoding="utf-8")

    def test_parse_render(self):
        rows, builds = bs.parse_cart_matrix(self.md)
        self.assertEqual(builds, ["aaaaaaaaaaaa"])
        self.assertEqual(len(rows), 6)
        self.assertNotIn("crc32", rows[0])
        self.assertEqual(rows[4]["filedb_flags"], 0x41)
        html, n_titles, n_good = bs.render_cart_table(rows)
        self.assertEqual(n_titles, 5, "the Rayman twins collapse")
        self.assertNotIn("Not yet verified", html)
        self.assertIn("2 files, same bytes (CRC32 E6BD422C)", html)
        self.assertIn(BAD_DUMP, html)
        self.assertIn('class="badge info"', html)
        self.assertIn("Rejected by the real boot ROM</span>", html)
        self.assertIn("SuperCross 3D (World)<br><small class=\"dim\">"
                      "Super Cross 3D (1995)</small>", html)
        self.assertIn("Boot ROM not used (RAM-loaded executable)", html)
        self.assertIn("(may be audio-only or need input)", html)
        # The twins count once in the run counters too.
        self.assertEqual(n_good["hle_run"], 5)
        self.assertEqual(n_good["bios_run"], 3)


if __name__ == "__main__":
    unittest.main()
