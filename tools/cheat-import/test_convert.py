"""Original synthetic sources: no downloaded community or proprietary fixtures."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("cheat_converter", Path(__file__).with_name("convert.py"))
converter = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = converter
spec.loader.exec_module(converter)

ROM = bytes(range(256)) * 8
DIGEST = converter.fnv1a64(ROM)


def source(body, extra="", load=None):
    # Deliberately not derived from an existing script; addresses are scratch RAM.
    return r'''
#include "bigpcrt.h"
#define MATCH_HASH HASH_VALUEULL
int flag;
int first = -1, second = -1;
int loaded_event, frame_event;
uint32_t load(int ignored, void *unused) {
    uint64_t digest = bigpemu_get_loaded_fnv1a64();
    LOAD_BODY
    return 0;
}
uint32_t frame(int ignored, void *unused) {
    if (flag) {
        int a, b;
        bigpemu_get_setting_value(&a, first);
        bigpemu_get_setting_value(&b, second);
        FRAME_BODY
    }
    return 0;
}
void bigp_init(void) {
    void *module = bigpemu_get_module_handle();
    int category = bigpemu_register_setting_category(module, "Synthetic Game");
    first = bigpemu_register_setting_bool(category, "Grouped freeze", 1);
    second = bigpemu_register_setting_bool(category, "Other freeze", 0);
    loaded_event = bigpemu_register_event_sw_loaded(module, load);
    frame_event = bigpemu_register_event_emu_thread_frame(module, frame);
}
EXTRA
'''.replace("HASH_VALUE", f"0x{DIGEST:016X}").replace(
        "LOAD_BODY", load or "flag = 0; if (digest == MATCH_HASH) { flag = 1; }").replace(
        "FRAME_BODY", body).replace("EXTRA", extra)


SIMPLE = "if (a) { bigpemu_jag_write8(0x1f0000, 0xab); } if (b) { bigpemu_jag_write16(0x1f0002, 0x1234); }"


class ConversionTests(unittest.TestCase):
    def convert(self, body=SIMPLE, **kwargs):
        return converter.Module(source(body, **kwargs)).convert(DIGEST)

    def skipped(self, statement, reason):
        result = self.convert("if (a) { " + statement + " } if (b) { bigpemu_jag_write8(0x1f1000, 1); }")
        self.assertIn(reason, result[0]["unsupported"])
        self.assertIn("code", result[1])

    def test_grouped_widths_and_loops(self):
        results = self.convert('''
            if (a) {
                bigpemu_jag_write8(0x1f0000, 0xab);
                bigpemu_jag_write16(0x1f0002, 0x1234);
                for (uint32_t i=0; i<2; ++i) {
                    bigpemu_jag_write32(0x1f0004 + i*4, 0x89abcdefU);
                }
            }
            if (b) { bigpemu_jag_write8(0x1f1000, 9); }
        ''')
        self.assertEqual(results[0]["writes"], 4)
        self.assertEqual(results[0]["code"], "001F0000:AB+001F0002:1234+001F0004:89ABCDEF+001F0008:89ABCDEF")
        self.assertEqual(results[1]["code"], "001F1000:09")

    def test_unsigned_semantics_and_promotions(self):
        results = self.convert('''
            if (a) {
                uint8_t small = 255;
                uint32_t wrap = 0xffffffffU;
                ++wrap;
                bigpemu_jag_write16(0x1f0000, small + 1);
                bigpemu_jag_write32(0x1f0002, wrap);
                if (-1 < 1U) { bigpemu_jag_write8(0x1f0006, 7); }
            }
            if (b) { bigpemu_jag_write8(0x1f1000, 1); }
        ''')
        self.assertEqual(results[0]["code"], "001F0000:0100+001F0002:00000000")

    def test_enums_macros_comments(self):
        text = source(SIMPLE.replace("0xab", "VAL"), "enum { ZERO, VAL = (3 << 2) }; // comment")
        self.assertEqual(converter.Module(text).convert(DIGEST)[0]["code"], "001F0000:0C")

    def test_wrong_hash(self):
        results = converter.Module(source(SIMPLE)).convert(DIGEST ^ 1)
        self.assertTrue(all("no writes activated" in r["unsupported"] for r in results))

    def test_hash_guard_required(self):
        results = self.convert(load="flag = 1;")
        self.assertTrue(all("ROM-hash guard" in r["unsupported"] for r in results))

    def test_ram_read_and_partial_write_rejected(self):
        self.skipped("bigpemu_jag_write8(0x1f0000, 1); bigpemu_jag_write8(0x1f0001, bigpemu_jag_read8(0x1f0002));", "unsupported call")

    def test_outside_ram_and_alignment(self):
        self.skipped("bigpemu_jag_write32(0x1ffffe, 1);", "outside")
        self.skipped("bigpemu_jag_write8(0xf00000, 1);", "outside")
        self.skipped("bigpemu_jag_write16(0x1f0001, 1);", "unaligned")

    def test_persistent_and_uninitialised_state(self):
        self.skipped("flag++; bigpemu_jag_write8(0x1f0000, 1);", "persistent")
        self.skipped("static int n = 0; bigpemu_jag_write8(0x1f0000, n);", "static local")
        self.skipped("int n; bigpemu_jag_write8(0x1f0000, n);", "uninitialised")

    def test_signed_overflow_and_unsupported_operations(self):
        self.skipped("int n = 0x7fffffff; ++n; bigpemu_jag_write32(0x1f0000, n);", "overflow")
        self.skipped("bigpemu_jag_write32(0x1f0000, a ? 1 : 2);", "conditional expression")
        self.skipped("return 0;", "early return")
        self.skipped("bigpemu_jag_write32(0x1f0000, 0xffffffffUL + 1);", "platform-dependent")

    def test_parameter_and_ambiguous_labels(self):
        text = source(SIMPLE).replace('bigpemu_register_setting_bool(category, "Other freeze", 0)',
                                     'bigpemu_register_setting_int(category, "Other freeze", 0, 0, 10, 1)')
        results = converter.Module(text).convert(DIGEST)
        self.assertIn("code", results[0])
        self.assertIn("parameter", results[1]["unsupported"])
        for label in ("Grouped freeze", "Something (Part 1)"):
            with self.assertRaisesRegex(converter.Unsupported, "ambiguous"):
                converter.Module(source(SIMPLE).replace("Other freeze", label))

    def test_setting_handle_arithmetic_refused(self):
        with self.assertRaisesRegex(converter.Unsupported, "registered global"):
            converter.Module(source(SIMPLE).replace("&a, first", "&a, first + 0")).convert(DIGEST)

    def test_copier_header_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            rom = Path(directory)/"header.j64"
            data = bytearray(1024*1024 + 512)
            data[0x600:0x604] = b"\x04"*4
            rom.write_bytes(data)
            with self.assertRaisesRegex(converter.Unsupported, "copier-headered"):
                converter.rom_bytes(rom)

    def test_write_and_execution_limits(self):
        self.skipped("for (int i=0; i<257; ++i) { bigpemu_jag_write8(0x1f0000+i, 1); }", "256-write")
        self.skipped("for (int i=0; i<100000; ++i) { }", "budget")

    def test_combined_only_extra_write_rejected(self):
        with self.assertRaisesRegex(converter.Unsupported, "interact"):
            self.convert(SIMPLE + "if (a && b) { bigpemu_jag_write8(0x1f0010, 7); }")

    def test_combined_only_ram_read_rejected(self):
        with self.assertRaisesRegex(converter.Unsupported, "unsupported call"):
            self.convert(SIMPLE + "if (a && b) { bigpemu_jag_read8(0x1f0010); }")

    def test_cross_toggle_local_changes_rejected(self):
        with self.assertRaisesRegex(converter.Unsupported, "interact"):
            self.convert("int n = 0; if (b) { n=7; bigpemu_jag_write8(0x1f0000, 1); } if (a) { bigpemu_jag_write8(0x1f0001,n); }")

    def test_no_includes_preprocessing_or_execution(self):
        with self.assertRaisesRegex(converter.Unsupported, "preprocessor"):
            converter.Module(source(SIMPLE).replace('"bigpcrt.h"', '"/etc/passwd"'))
        with self.assertRaisesRegex(converter.Unsupported, "initialisation call"):
            converter.Module(source(SIMPLE).replace("bigpemu_get_module_handle()", 'system("touch /tmp/never-execute-community-source")'))
        with self.assertRaisesRegex(converter.Unsupported, "initialisation assignment"):
            converter.Module(source(SIMPLE).replace("void bigp_init(void) {", "void bigp_init(void) { flag = 1;"))
        with self.assertRaisesRegex(converter.Unsupported, "initialisation call"):
            converter.Module(source(SIMPLE).replace("void bigp_init(void) {",
                "void bigp_init(void) { bigpemu_register_event_unhandled(module, frame);"))

    def test_cli_strict_partial_provenance_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            src, rom, out, report = [root/n for n in ("input.c", "game.j64", "output.cht", "report.json")]
            src.write_text(source(SIMPLE.replace("0x1234", "bigpemu_jag_read16(0x1f1000)")))
            rom.write_bytes(ROM)
            args = [str(src), "--rom", str(rom), "--output", str(out), "--report", str(report)]
            with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(converter.main(args), 1)
                self.assertFalse(out.exists())
                data = json.loads(report.read_text())
                self.assertIn("unsupported", data["cheats"][1])
                self.assertEqual(data["rom_fnv1a64"], f"{DIGEST:016X}")
                report.unlink()
                self.assertEqual(converter.main(args + ["--allow-partial"]), 0)
                text = out.read_text()
                self.assertIn('vj_rom_crc32 = "', text)
                self.assertIn("cheat0_enable = false", text)
                self.assertNotIn("cheat1_code", text)
                self.assertEqual(converter.main(args + ["--allow-partial"]), 2)
                self.assertEqual(out.read_text(), text)


if __name__ == "__main__":
    unittest.main()
