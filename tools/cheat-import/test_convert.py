"""Original synthetic sources: no downloaded community or proprietary fixtures."""
import contextlib
import importlib.util
import io
import itertools
import json
import re
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

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

    def table_source(self, table="static const uint32_t digits[4] = {0, 0, 0x1234, 0x5678};", body=None):
        body = body or '''
            if (a) { bigpemu_jag_write8(0x1f0000, 9); }
            if (b > 1) {
                uint32_t password = digits[b];
                uint32_t dest = 3;
                for (uint32_t i = 0; i < 4; ++i, --dest) {
                    bigpemu_jag_write8(0x1f0100 + dest, '0' + ((password >> (i*4)) & 15));
                }
            }
        '''
        return source(body, extra=table).replace(
            'bigpemu_register_setting_bool(category, "Other freeze", 0)',
            'bigpemu_register_setting_int_full(category, "Password", -1, -1, 3, 1)')

    def test_unused_static_const_table_does_not_block_freezes(self):
        table = 'static const uint8_t reference[80][14] = {"REFERENCE", "FOR DISPLAY"};'
        module = converter.Module(source(SIMPLE, extra=table))
        self.assertEqual([r["code"] for r in module.convert(DIGEST)], ["001F0000:AB", "001F0002:1234"])
        self.assertTrue(all("code" not in r for r in module.convert(DIGEST ^ 1)))

    def test_unused_table_is_not_materialized_or_charged_to_array_budget(self):
        text = source("if (a) { bigpemu_jag_write8(0x1f0000, active[0]); }", extra=
                      'static const uint8_t reference[80][14] = {"REFERENCE"}; '
                      'const uint8_t active[1024] = {9};')
        self.assertEqual(converter.Module(text).convert(DIGEST)[0]["code"], "001F0000:09")

    def test_table_references_in_macros_and_unreachable_code_prevent_skip(self):
        table = 'static const uint8_t reference[80][14] = {"ABC", "DEF"};'
        for body in ('if (0) { bigpemu_jag_write8(0x1f0000, reference[0][0]); }',
                     'if (a) { bigpemu_jag_write8(0x1f0000, ALIAS[0][0]); }',
                     'if (a) { bigpemu_jag_sysmemwrite(0x1f0000, &reference[0], 4); }'):
            text = source(body + SIMPLE, extra=table + '\n#define ALIAS reference\n')
            with self.subTest(body=body), self.assertRaisesRegex(converter.Unsupported, "1024-element"):
                converter.Module(text).convert(DIGEST)
        text = source(SIMPLE, extra=table + '\nuint32_t helper(void) { return reference[0][0]; }')
        with self.assertRaisesRegex(converter.Unsupported, "1024-element"):
            converter.Module(text).convert(DIGEST)
        text = source(SIMPLE, extra=table + '\nconst int dependent = reference[0][0];')
        with self.assertRaisesRegex(converter.Unsupported, "1024-element"):
            converter.Module(text).convert(DIGEST)

    def test_unused_table_requires_static_const_integer_storage(self):
        for table in ('const uint8_t reference[80][14] = {"ABC"};',
                      'static uint8_t reference[2][4] = {"ABC"};',
                      'static const volatile uint8_t reference[2][4] = {"ABC"};',
                      'static const uint8_t *reference[2] = {0};',
                      'static const uint8_t reference[2][4];'):
            with self.subTest(table=table), self.assertRaises(converter.Unsupported):
                converter.Module(source(SIMPLE, extra=table)).convert(DIGEST)

    def test_unused_table_rejects_hidden_effects_and_invalid_dimensions(self):
        for table in ('static const uint8_t reference[2][4] = {{BUMP}};',
                      'static const uint8_t reference[BUMP][4] = {{0}};',
                      'static const uint8_t reference[2][4] = {{bigpemu_jag_read8(0)}};',
                      'static const uint8_t reference[0][4] = {{0}};',
                      'static const uint8_t reference[-1][4] = {{0}};',
                      'static const uint8_t reference[missing][4] = {{0}};',
                      'static const uint8_t reference[2][4] = {{missing}};',
                      'static const int8_t reference[2][4] = {{999}};',
                      'static const uint8_t reference[2][] = {{0}};'):
            text = source(SIMPLE, extra='int counter = 1;\n#define BUMP (++counter)\n' + table)
            with self.subTest(table=table), self.assertRaises(converter.Unsupported):
                converter.Module(text).convert(DIGEST)

    def test_unused_table_does_not_skip_other_global_declarations(self):
        text = source(SIMPLE, extra='static const uint8_t reference[2][4] = {"ABC"}; int *other;')
        with self.assertRaisesRegex(converter.Unsupported, "pointer/array"):
            converter.Module(text).convert(DIGEST)

    def test_unused_table_name_does_not_enable_skipping_local_arrays(self):
        text = source('if (a) { const uint8_t reference[2][4] = {"ABC"}; }' + SIMPLE,
                      extra='static const uint8_t reference[2][4] = {"DEF"};')
        result = converter.Module(text).convert(DIGEST)
        self.assertIn("pointer/array", result[0]["unsupported"])
        self.assertIn("code", result[1])

    def test_constant_table_passwords_and_sequenced_loop_update(self):
        module = converter.Module(self.table_source())
        results = module.convert(DIGEST)
        self.assertEqual(results[0]["code"], "001F0000:09")
        self.assertEqual(results[3]["name"], "Password: 2")
        self.assertEqual(results[3]["code"], "001F0103:34+001F0102:33+001F0101:32+001F0100:31")
        self.assertEqual(results[4]["code"], "001F0103:38+001F0102:37+001F0101:36+001F0100:35")
        self.assertTrue(all("code" not in r for r in module.convert(DIGEST ^ 1)))

    def test_nested_tables_preserve_rows_padding_and_integer_types(self):
        text = source('''if (a) {
            bigpemu_jag_write16(0x1f0000, digits[0][0][0] + 256);
            bigpemu_jag_write8(0x1f0002, digits[0][0][1]);
            bigpemu_jag_write8(0x1f0003, digits[1][1][1]);
            bigpemu_jag_write8(0x1f0004, digits[1][0][1]);
        }''', extra='const uint8_t digits[2][2][2] = {{{258}}, {{3, 4}}};')
        self.assertEqual(converter.Module(text).convert(DIGEST)[0]["code"],
                         "001F0000:0102+001F0002:00+001F0003:00+001F0004:04")

    def test_nested_string_passwords_and_c_terminator_rules(self):
        for declaration, expected in (
                ('const uint8_t digits[2][4] = {"ABCD", "X"};', "41+42+43+44+58+00+00+00"),
                ('const uint8_t digits[][3] = {"A\\x42", "C\\101"};', "41+42+00+43+41+00")):
            width = 4 if '[4]' in declaration else 3
            text = source(f'''if (a) {{
                for (int i=0; i<2; ++i) {{
                    for (int j=0; j<{width}; ++j) {{
                        bigpemu_jag_write8(0x1f0000+i*{width}+j, digits[i][j]);
                    }}
                }}
            }}''', extra=declaration)
            result = converter.Module(text).convert(DIGEST)[0]
            self.assertEqual([w.split(':')[1] for w in result['code'].split('+')], expected.split('+'))

    def test_nested_table_index_dependencies_and_bounds(self):
        table = 'const uint8_t digits[2][2] = {{1,2},{3,4}};'
        for access in ('digits[-1][0]', 'digits[0][2]', 'digits[2][0]', 'digits[0][-1]',
                       'digits[0]', 'digits[0][0][0]'):
            result = converter.Module(source(f'if (a) {{ bigpemu_jag_write8(0x1f0000, {access}); }}', extra=table)).convert(DIGEST)[0]
            self.assertNotIn('code', result, access)
        for access in ('digits[b][0]', 'digits[0][b]'):
            result = converter.Module(source(f'if (a) {{ bigpemu_jag_write8(0x1f0000, {access}); }}', extra=table)).convert(DIGEST)[0]
            self.assertIn('exactly one independent setting', result['unsupported'])

    def test_nested_indices_do_not_choose_an_order_for_side_effects(self):
        table = 'const int digits[2][2] = {{1,2},{3,4}};'
        for access in ('digits[i++][i++]', 'digits[0][++i]'):
            text = source(f'if (a) {{ int i=0; bigpemu_jag_write8(0x1f0000, {access}); }}', extra=table)
            self.assertIn('unsupported', converter.Module(text).convert(DIGEST)[0])

    def test_nested_tables_share_total_budget_and_zero_fill_missing_rows(self):
        text = source('''if (a) {
            bigpemu_jag_write16(0x1f0000, digits[0][0]);
            bigpemu_jag_write16(0x1f0002, digits[31][31]);
        }''', extra='const int16_t digits[32][32] = {{-2}};')
        self.assertEqual(converter.Module(text).convert(DIGEST)[0]['code'], '001F0000:FFFE+001F0002:0000')

    def test_nested_tables_refuse_ambiguous_initializers_and_excessive_sizes(self):
        for declaration in (
                'const int digits[2][2] = {1,2,3,4};',
                'const int digits[2][2] = {{1,2,3}};',
                'const int digits[1][2] = {{1},{2}};',
                'const int digits[2][] = {{1}};',
                'const int digits[33][33] = {{1}};',
                'const int digits[2][2] = {{1}}; const int more[1021] = {0};',
                'const uint8_t digits[1][2] = {"ABC"};',
                'const uint16_t digits[1][2] = {"A"};',
                'const uint8_t digits[1][2] = {L"A"};',
                'const uint8_t digits[1][2] = {"\\x123"};',
                'const uint8_t digits[1][2] = {"é"};',
                'const volatile uint8_t digits[2][2] = {{1}};',
                'const int digits[2][2] = {{BUMP}};\n#define BUMP (++flag)\n'):
            with self.subTest(declaration=declaration), self.assertRaises(converter.Unsupported):
                converter.Module(source(SIMPLE, extra=declaration)).convert(DIGEST)

    def test_constant_table_sizes_padding_and_integer_types(self):
        for decl in ("const uint8_t digits[] = {258, 7};", "static uint8_t const digits[4] = {258, 7};"):
            text = self.table_source(decl, "if (a) { bigpemu_jag_write16(0x1f0000, digits[0] + 256); }")
            self.assertEqual(converter.Module(text).convert(DIGEST)[0]["code"], "001F0000:0102")
        text = self.table_source("enum { SIZE = 4, LAST = 3 }; const int16_t digits[SIZE] = {-7};",
                                 "if (a) { bigpemu_jag_write16(0x1f0000, digits[0]); bigpemu_jag_write8(0x1f0002, digits[LAST]); }")
        self.assertEqual(converter.Module(text).convert(DIGEST)[0]["code"], "001F0000:FFF9+001F0002:00")

    def test_constant_table_invalid_indices_are_not_python_indices(self):
        for index in (-1, 4):
            text = self.table_source(body=f"if (a) {{ bigpemu_jag_write8(0x1f0000, 1); bigpemu_jag_write32(0x1f0004, digits[{index}]); }}")
            result = converter.Module(text).convert(DIGEST)[0]
            self.assertIn("array index out of bounds", result["unsupported"])
            self.assertNotIn("code", result)

    def test_constant_table_unsupported_declarations_fail_closed(self):
        for declaration in ("uint32_t digits[4] = {0};", "uint32_t digits[4][2] = {{0}};",
                            "const uint32_t *digits[4] = {0};", "const int digits[0] = {};",
                            "const int digits[2] = {1,2,3};", "const int digits[] = {};",
                            "const int digits[4];", "const int digits[4] = {[1] = 3};",
                            'const int digits[4] = {"abc"};'):
            with self.subTest(declaration=declaration), self.assertRaises(converter.Unsupported):
                converter.Module(self.table_source(declaration)).convert(DIGEST)

    def test_constant_table_limits_and_side_effects(self):
        for declaration in ("const int digits[1025] = {0};", "const int digits[800] = {0}; const int more[800] = {0};",
                            "const int digits[4] = {flag++};", "const int digits[flag++] = {0};",
                            "const int digits[4] = {bigpemu_jag_read8(1)};"):
            with self.subTest(declaration=declaration), self.assertRaises(converter.Unsupported):
                converter.Module(self.table_source(declaration)).convert(DIGEST)

    def test_constant_table_no_pointer_decay_or_mutation(self):
        for statement in ("digits[0] = 9;", "++digits[0];", "digits = 2;", "++digits;",
                          "bigpemu_jag_write32(0x1f0000, digits);", "int *ptr = digits;",
                          "const int local[2] = {1,2};", "int digits = 2; bigpemu_jag_write8(0x1f0000, digits[0]);"):
            text = self.table_source(body="if (a) { " + statement + " }")
            result = converter.Module(text).convert(DIGEST)[0]
            self.assertIn("unsupported", result, statement)
            self.assertNotIn("code", result, statement)

    def test_constant_table_index_dependencies_preserved(self):
        text = self.table_source(body="if (a) { bigpemu_jag_write32(0x1f0000, digits[b+1]); }")
        result = converter.Module(text).convert(DIGEST)[0]
        self.assertIn("exactly one independent setting", result["unsupported"])

    def test_sequenced_loop_update_still_checks_budget_and_persistent_state(self):
        self.skipped("for (int i = 0; i < 1; ++i, --i) { }", "budget")
        self.skipped("for (int i = 0; i < 1; ++i, ++flag) { bigpemu_jag_write8(0x1f0000, 7); }", "persistent")

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
                                     'bigpemu_register_setting_int(category, "Other freeze", 0)')
        results = converter.Module(text).convert(DIGEST)
        self.assertIn("code", results[0])
        self.assertIn("parameter", results[1]["unsupported"])
        for label in ("Grouped freeze", "Something (Part 1)"):
            with self.assertRaisesRegex(converter.Unsupported, "ambiguous"):
                converter.Module(source(SIMPLE).replace("Other freeze", label))

    def init_constant_source(self, declarations="const int maxValue = (2 * 2) - 1;", bound="maxValue", body=None):
        return source(body or SIMPLE.replace("0x1234", "b")).replace(
            'second = bigpemu_register_setting_bool(category, "Other freeze", 0);',
            declarations + '\nsecond = bigpemu_register_setting_int_full(category, "Other freeze", 0, 0, ' + bound + ', 1);')

    def test_init_const_bound_expands_without_losing_boolean(self):
        module = converter.Module(self.init_constant_source())
        result = module.convert(DIGEST)
        self.assertEqual([r["code"] for r in result], ["001F0000:AB", "001F0002:0001", "001F0002:0002", "001F0002:0003"])
        self.assertTrue(all("code" not in r for r in module.convert(DIGEST ^ 1)))

    def test_init_const_order_macros_enums_and_fixed_width_types(self):
        text = self.init_constant_source("const uint8_t wrapped = 258; const int maxValue = wrapped + LAST;", body=SIMPLE.replace("0x1234", "b"))
        text += "\nenum { LAST = 1 };\n"
        self.assertEqual(converter.Module(text).convert(DIGEST)[-1]["code"], "001F0002:0003")
        text = self.init_constant_source("const int base = 1; const int maxValue = base + EXTRA;", bound="maxValue") + "\n#define EXTRA 2\n"
        self.assertEqual(len(converter.Module(text).convert(DIGEST)), 4)

    def test_init_const_macros_cannot_hide_side_effects_or_calls(self):
        for expr in ("++counter", "counter++", "--counter", "counter--", "counter = 2",
                     "bigpemu_get_loaded_fnv1a64()"):
            text = source(SIMPLE.replace("0xab", "counter"), extra=
                          "int counter = 1;\n#define BUMP (" + expr + ")\n#define ALIAS BUMP\n")
            text = text.replace("void bigp_init(void) {", "void bigp_init(void) { const int unused = ALIAS;")
            with self.subTest(expr=expr), self.assertRaisesRegex(converter.Unsupported, "pure integer initializer"):
                converter.Module(text).convert(DIGEST)

    def test_setting_metadata_macros_cannot_hide_mutation(self):
        text = self.init_constant_source(bound="ALIAS") + "\nint counter = 1;\n#define BUMP (++counter)\n#define ALIAS BUMP\n"
        with self.assertRaisesRegex(converter.Unsupported, "metadata must be constant"):
            converter.Module(text).convert(DIGEST)

    def test_array_initializer_macros_cannot_hide_mutation(self):
        text = source(SIMPLE, extra="int counter = 1;\n#define BUMP (++counter)\n#define ALIAS BUMP\nconst int table[] = {ALIAS};")
        with self.assertRaisesRegex(converter.Unsupported, "constant integer expressions"):
            converter.Module(text).convert(DIGEST)

    def test_message_argument_macros_cannot_hide_mutation(self):
        for name in ("printf", "printf_notify"):
            text = source(name + '("%d", ALIAS);' + SIMPLE, extra="int counter = 1;\n#define BUMP (++counter)\n#define ALIAS BUMP\n")
            with self.subTest(name=name), self.assertRaisesRegex(converter.Unsupported, "side effect"):
                converter.Module(text).convert(DIGEST)

    def test_macro_purity_walk_bounds_shared_expansions(self):
        definitions = ["#define VALUE0 7"]
        definitions += [f"#define VALUE{i} (VALUE{i-1} + VALUE{i-1})" for i in range(1, 41)]
        module = converter.Module(source(SIMPLE, extra="\n".join(definitions)))
        nodes = list(itertools.islice(converter.walk(module.macros["VALUE40"], module.macros), 201))
        self.assertLess(len(nodes), 130)
        self.assertTrue(any(isinstance(n, converter.C.Constant) and n.value == "7" for n in nodes))

    def test_init_const_future_declaration_not_visible_to_registration(self):
        text = self.init_constant_source("")
        text = text.replace('frame_event = bigpemu_register_event_emu_thread_frame(module, frame);',
                            'const int maxValue = 3; frame_event = bigpemu_register_event_emu_thread_frame(module, frame);')
        result = converter.Module(text).convert(DIGEST)
        self.assertIn("code", result[0])
        self.assertIn("unknown identifier maxValue", result[1]["unsupported"])

    def test_init_const_invalid_declarations_and_initializers_refused(self):
        for decl in ("int maxValue = 3;", "const int maxValue;", "const int maxValue[2] = {1,2};",
                     "const int *maxValue = 0;", "static const int maxValue = 3;",
                     "const int maxValue = flag++;", "const int maxValue = (flag = 3);",
                     "const int maxValue = (int)bigpemu_jag_read8(0);",
                     "const int maxValue = later; const int later = 3;",
                     "const int maxValue = 1; const int maxValue = 3;",
                     "const int flag = 3; const int maxValue = flag;",
                     "const int frame = 3; const int maxValue = frame;",
                     "const int bigpemu_register_setting_bool = 3; const int maxValue = 3;",
                     "{ const int maxValue = 3; }"):
            with self.subTest(decl=decl), self.assertRaises(converter.Unsupported):
                converter.Module(self.init_constant_source(decl)).convert(DIGEST)

    def test_init_const_cannot_be_modified_or_use_registration_handles(self):
        for decl in ("const int maxValue = 3; ++maxValue;", "const int maxValue = 3; maxValue = 4;",
                     "const int maxValue = first;", "const int maxValue = category;",
                     "const int maxValue = ALIAS;"):
            text = self.init_constant_source(decl) + "\n#define ALIAS first\n"
            with self.subTest(decl=decl), self.assertRaises(converter.Unsupported):
                converter.Module(text).convert(DIGEST)
        text = self.init_constant_source("const int maxValue = 3; const int category = 1;")
        with self.assertRaises(converter.Unsupported):
            converter.Module(text).convert(DIGEST)

    def test_init_const_is_not_visible_to_frame_callback(self):
        result = converter.Module(self.init_constant_source(body=SIMPLE.replace("0xab", "maxValue"))).convert(DIGEST)
        self.assertIn("unknown identifier maxValue", result[0]["unsupported"])
        self.assertIn("code", result[1])

    def test_unused_init_const_is_still_validated(self):
        text = source(SIMPLE).replace('void bigp_init(void) {', 'void bigp_init(void) { const int unused = missing;')
        with self.assertRaisesRegex(converter.Unsupported, "unknown identifier"):
            converter.Module(text).convert(DIGEST)

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

    def int_module(self, frame_body, load=None):
        text = source(frame_body, load=load).replace('bigpemu_register_setting_bool(category, "Other freeze", 0)',
            'bigpemu_register_setting_int_full(category, "Start At Level", -1, -1, 9, 1)')
        return converter.Module(text)

    def test_int_full_expands_values_and_preserves_boolean(self):
        module = self.int_module(SIMPLE.replace("if (b)", "if (b >= 0)").replace("0x1234", "b"))
        results = module.convert(DIGEST)
        self.assertEqual(len(results), 11)
        self.assertEqual(results[0]["code"], "001F0000:AB")
        self.assertEqual([r["name"] for r in results[1:]], [f"Start At Level: {n}" for n in range(10)])
        self.assertEqual([r["code"] for r in results[1:]], [f"001F0002:{n:04X}" for n in range(10)])
        self.assertTrue(all("code" not in r for r in module.convert(DIGEST ^ 1)))

    def test_int_full_step_negative_macro_enum_and_default(self):
        text = source(SIMPLE.replace("if (b)", "if (b > -4)").replace("0x1234", "b + 10"),
                      extra="enum { LOW = -4, HIGH = 4 };\n#define STEP (1 << 1)")
        text = text.replace('bigpemu_register_setting_bool(category, "Other freeze", 0)',
                            'bigpemu_register_setting_int_full(category, "Other freeze", 2, LOW, HIGH, STEP)')
        results = converter.Module(text).convert(DIGEST)
        self.assertEqual([r["name"] for r in results[1:]], [f"Other freeze: {n}" for n in (-2, 0, 2, 4)])
        self.assertEqual([r["code"] for r in results[1:]], [f"001F0002:{n:04X}" for n in (8, 10, 12, 14)])

    def test_int_full_dependent_write_refused(self):
        results = self.int_module("if (a) { bigpemu_jag_write8(0x1f0000, 1); if (b != -1) { bigpemu_jag_write8(0x1f0abc, 2); } }").convert(DIGEST)
        self.assertIn("code", results[0])
        self.assertTrue(all("code" not in r for r in results[1:]))
        self.assertTrue(all("1F0ABC" not in r.get("code", "") for r in results))

    def test_int_full_runtime_choice_omitted_in_full(self):
        results = self.int_module(SIMPLE.replace("if (b)", "if (b >= 0)").replace("0x1234", "b") +
            "if (b == 3) { bigpemu_jag_read8(1); }").convert(DIGEST)
        self.assertIn("unsupported call", results[4]["unsupported"])
        self.assertNotIn("code", results[4])
        self.assertIn("code", results[3])
        self.assertIn("code", results[5])

    def test_int_full_invalid_and_excessive_ranges(self):
        for args, reason in (("0, 0, 2, 0", "invalid"), ("0, 0, 2, -1", "invalid"),
                             ("0, 2, 0, 1", "invalid"), ("9, 0, 2, 1", "invalid"),
                             ("1, 0, 4, 2", "invalid"), ("0, 0, 64, 1", "64-value"),
                             ("0, 0, unknown, 1", "unknown identifier"),
                             ("0, 0, 0, 1", "no selectable values")):
            text = source(SIMPLE).replace('bigpemu_register_setting_bool(category, "Other freeze", 0)',
                                         'bigpemu_register_setting_int_full(category, "Other freeze", ' + args + ')')
            results = converter.Module(text).convert(DIGEST)
            self.assertIn("code", results[0])
            self.assertIn(reason, results[1]["unsupported"])

    def test_int_full_minimum_must_be_inactive(self):
        with self.assertRaisesRegex(converter.Unsupported, "ROM-hash guard"):
            self.int_module(SIMPLE).convert(DIGEST)  # if (-1) writes even with all cheats off.

    def test_int_full_default_does_not_auto_enable(self):
        text = source(SIMPLE.replace("0x1234", "b")).replace(
            'bigpemu_register_setting_bool(category, "Other freeze", 0)',
            'bigpemu_register_setting_int_full(category, "Other freeze", 2, 0, 3, 1)')
        results = converter.Module(text).convert(DIGEST)
        self.assertEqual([r["code"] for r in results[1:]], ["001F0002:0001", "001F0002:0002", "001F0002:0003"])

    def test_int_full_different_write_footprints_omitted(self):
        results = self.int_module(SIMPLE.replace("if (b)", "if (b >= 0)") +
            "if (b > 3) { bigpemu_jag_write8(0x1f0010, 7); }").convert(DIGEST)
        self.assertIn("code", results[0])
        self.assertTrue(all("different bytes" in r["unsupported"] for r in results[1:]))

    def test_int_full_cross_setting_interactions(self):
        with self.assertRaisesRegex(converter.Unsupported, "interact"):
            self.int_module(SIMPLE.replace("if (b)", "if (b >= 0)") +
                "if (a && b == 7) { bigpemu_jag_write8(0x1f0010, 7); }").convert(DIGEST)

    def test_int_full_two_parameter_interaction(self):
        text = source("if (a > 0) { bigpemu_jag_write8(0x1f0000, a); } "
                      "if (b > 0) { bigpemu_jag_write8(0x1f0001, b); } "
                      "if (a == 2 && b == 3) { bigpemu_jag_write8(0x1f0002, 9); }")
        for label in ("Grouped freeze", "Other freeze"):
            text = re.sub(r'bigpemu_register_setting_bool\(category, "' + label + r'", [01]\)',
                          f'bigpemu_register_setting_int_full(category, "{label}", 0, 0, 3, 1)', text)
        with self.assertRaisesRegex(converter.Unsupported, "interact"):
            converter.Module(text).convert(DIGEST)

    def test_int_full_expanded_label_collision(self):
        text = source(SIMPLE).replace('"Grouped freeze"', '"Other freeze: 1"').replace(
            'bigpemu_register_setting_bool(category, "Other freeze", 0)',
            'bigpemu_register_setting_int_full(category, "Other freeze", 0, 0, 2, 1)')
        with self.assertRaisesRegex(converter.Unsupported, "ambiguous"):
            converter.Module(text).convert(DIGEST)

    def test_int_full_interaction_budget(self):
        text = source("if (a > 0) { bigpemu_jag_write8(0x1f0000, a); } if (b > 0) { bigpemu_jag_write8(0x1f0001, b); }")
        for label in ("Grouped freeze", "Other freeze"):
            text = re.sub(r'bigpemu_register_setting_bool\(category, "' + label + r'", [01]\)',
                          f'bigpemu_register_setting_int_full(category, "{label}", 0, 0, 63, 1)', text)
        with mock.patch.object(converter, "MAX_SETTING_COMBINATIONS", 100):
            with self.assertRaisesRegex(converter.Unsupported, "interaction limit"):
                converter.Module(text).convert(DIGEST)

    def test_int_full_all_enabled_subsets_replay_last_choice(self):
        text = source(SIMPLE.replace("if (b)", "if (b >= 0)").replace("0x1234", "b")).replace(
            'bigpemu_register_setting_bool(category, "Other freeze", 0)',
            'bigpemu_register_setting_int_full(category, "Start At Level", -1, -1, 2, 1)')
        module = converter.Module(text)
        results = module.convert(DIGEST)
        # Exercise every frontend subset including multiple choices for one handle.
        for mask in range(1 << len(results)):
            actual = {}
            selected = {}
            for i, entry in enumerate(results):
                if not mask & (1 << i):
                    continue
                selected[1 if i == 0 else 2] = 1 if i == 0 else i-1
                for segment in entry["code"].split("+"):
                    address, value = segment.split(":")
                    for offset, byte in enumerate(bytes.fromhex(value)):
                        actual[int(address, 16)+offset] = byte
            runtime = module.runtime(DIGEST, selected)
            runtime.run(module.events["bigpemu_register_event_emu_thread_frame"])
            expected = {}
            for address, value, width in runtime.writes:
                for offset, byte in enumerate(value.to_bytes(width, "big")):
                    expected[address+offset] = byte
            self.assertEqual(actual, expected, mask)
        self.assertEqual(converter.cht_entries(results).count("cheat0_enable = false"), 1)
        self.assertFalse(any("enable = true" in line for line in converter.cht_entries(results)))

    def test_int_full_metadata_side_effect_refused(self):
        text = source(SIMPLE).replace('bigpemu_register_setting_bool(category, "Other freeze", 0)',
            'bigpemu_register_setting_int_full(category, "Other freeze", 0, 0, flag++, 1)')
        with self.assertRaisesRegex(converter.Unsupported, "initialisation mutation"):
            converter.Module(text).convert(DIGEST)

    def test_printf_notify_load_message(self):
        load = r'flag = 0; if (digest == MATCH_HASH) { printf_notify("Supported.\n"); flag = 1; }'
        module = converter.Module(source(SIMPLE, load=load))
        self.assertEqual(module.messages, [])
        self.assertIn("code", module.convert(DIGEST)[0])
        self.assertEqual(module.messages, ["Supported."])
        module.convert(DIGEST ^ 1)
        self.assertEqual(module.messages, [])

    def test_printf_notify_frame_or_format(self):
        load = r'flag = 0; if (digest == MATCH_HASH) { flag = 1; printf_notify("x %d", flag); printf_notify(MESSAGE_TEXT); printf_notify("ok\n"); }'
        module = converter.Module(source(SIMPLE + ' printf_notify("frame note\\n"); printf("log\\n");', load=load, extra="int MESSAGE_TEXT;"))
        self.assertIn("code", module.convert(DIGEST)[0])
        self.assertEqual(module.messages, ["ok"])
        self.assertEqual(module.dropped_messages, ["script message not shown: x %d", "script message not shown: dynamic",
                                                   "script message not shown: frame note"])
        self.assertEqual(module.ignored_calls, {"printf"})
        module.convert(DIGEST ^ 1)
        self.assertEqual((module.messages, module.dropped_messages, module.ignored_calls), ([], [], set()))

    def test_printf_notify_limits(self):
        load = 'flag = 1; ' + "".join('printf_notify("m%d\\n"); printf_notify("m%d");' % (i, i) for i in range(6)) + (
            'printf_notify("%s");' % ("y"*300))
        module = converter.Module(source(SIMPLE, load=load))
        module.convert(DIGEST)
        self.assertEqual(module.messages, ["m0", "m1", "m2", "m3"])
        self.assertEqual(module.dropped_messages, ["script message not shown: m4", "script message not shown: m5",
                                                   "script message not shown: " + "y"*60])
        module = converter.Module(source(SIMPLE, load='flag = 1; printf_notify("%s");' % ("z"*300)))
        module.convert(DIGEST)
        self.assertEqual(module.messages, ["z"*200])

    def test_unknown_call_names_call(self):
        with self.assertRaises(converter.Unsupported) as caught:
            self.convert(SIMPLE + "bigpemu_jag_foo();")
        self.assertEqual(caught.exception.call, "bigpemu_jag_foo")
        self.assertIsNone(converter.Unsupported("plain").call)
        result = self.convert("if (a) { bigpemu_jag_read8(0x1f0000); } if (b) { bigpemu_jag_write8(0x1f1000, 1); }")
        self.assertIn("unsupported call bigpemu_jag_read8", result[0]["unsupported"])
        with self.assertRaises(converter.Unsupported) as caught:
            converter.Module(source(SIMPLE).replace("bigpemu_get_module_handle()", "bigpemu_jag_bar()"))
        self.assertEqual(caught.exception.call, "bigpemu_jag_bar")

    def test_printf_side_effect_refused(self):
        for statement in ('printf_notify("x %d", n++);', 'printf("%d", f());', 'printf("%d", n = 3);', 'printf_notify("x", bigpemu_jag_write8(0x1f0000, 1));'):
            with self.assertRaisesRegex(converter.Unsupported, "side effect"):
                self.convert("int n = 0; " + statement + SIMPLE, extra="int f(void) { return 1; }")
        self.convert('int n = 4; printf("%d", n + 1);' + SIMPLE)

    def test_call_attribute_scope(self):
        for statement, call in (("bigpemu_jag_write8(1);", None), ("abs(a);", None), ("bigpemu_jag_read8(0x1f0000);", "bigpemu_jag_read8"),
                                ("bigpemu_jag_foo();", "bigpemu_jag_foo")):
            with self.assertRaises(converter.Unsupported) as caught:
                self.convert(statement + SIMPLE)
            self.assertEqual(caught.exception.call, call, statement)
        with self.assertRaisesRegex(converter.Unsupported, "unsupported use of bigpemu_jag_write8"):
            self.convert("bigpemu_jag_write8(1);" + SIMPLE)

    def test_api_map_covers_converter(self):
        calls = converter.load_api_map()
        self.assertEqual(list(calls), sorted(calls))
        self.assertTrue(converter.SPECIAL_CALLS)
        for name, status in converter.SPECIAL_CALLS.items():
            self.assertEqual(calls[name]["status"], status, name)
        for name in ("bigpemu_jag_read8", "bigpemu_jag_sysmemwrite", "bigpemu_jag_m68k_bp_add"):
            self.assertEqual(calls[name]["status"], "unsupported")
        for name, row in calls.items():
            self.assertEqual(set(row), {"status", "meaning", "ours", "todo"}, name)
            self.assertTrue(row["meaning"] and row["ours"], name)

    def test_api_map_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"map.json"
            for text in ('{"format": 2, "calls": {}}', '{"format": 1, "calls": {"x": {"status": "maybe", "meaning": "", "ours": "", "todo": ""}}}'):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    converter.load_api_map(path)

    def test_cht_text_unchanged(self):
        entries = [{"name": "A one", "code": "001F0000:AB"}, {"name": "B two", "code": "001F0002:1234+001F0004:89ABCDEF"}]
        expected = ('# Locally converted community cheats; nothing is enabled automatically.\n# Source: src/x.c\n'
                    '# Source SHA256: ' + "ab"*32 + '\n# Cartridge revision guard is enforced by the Virtual Jaguar app.\n'
                    'vj_rom_crc32 = "DEADBEEF"\ncheats = 2\n\ncheat0_desc = "A one"\ncheat0_code = "001F0000:AB"\n'
                    'cheat0_enable = false\n\ncheat1_desc = "B two"\ncheat1_code = "001F0002:1234+001F0004:89ABCDEF"\n'
                    'cheat1_enable = false\n')
        self.assertEqual(converter.cht_text("T", 0xDEADBEEF, entries, "src/x.c", "ab"*32), expected)
        comments = ["Locally converted community cheats; nothing is enabled automatically.", "Source: src/x.c",
                    "Source SHA256: " + "ab"*32, "Cartridge revision guard is enforced by the Virtual Jaguar app."]
        header = converter.cht_header(0xDEADBEEF, comments)
        self.assertEqual(header[0], "# " + comments[0])
        self.assertEqual(header[-1], 'vj_rom_crc32 = "DEADBEEF"')
        self.assertFalse(any(line.startswith("cheats") for line in header))
        self.assertEqual(converter.cht_entries(entries)[:2], ["cheats = 2", ""])
        self.assertEqual("\n".join(header + converter.cht_entries(entries)), expected)

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
