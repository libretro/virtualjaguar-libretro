/*
 * Unit tests for the Jaguar cheat engine (src/cheat.c).
 *
 * Self-contained: links only src/cheat.c. `make test` uses `-I src` before
 * libretro-common, so `<boolean.h>` resolves to src/boolean.h (compatible with
 * libretro-common) via src/cheat.h.
 *
 * Build & run (from repo root): `make test`
 * or manually:
 *     cc -O2 -Wall -std=c99 -I src -I libretro-common/include \
 *        -o test/test_cheat test/test_cheat.c src/cheat.c && ./test/test_cheat
 *
 * The tests cover:
 *   1. cheat_parse_one: all accepted format lengths, every separator style,
 *      and a broad set of rejection cases (bad chars, wrong length, NULL).
 *   2. List management: add, toggle off, replacement-on-same-index, and
 *      capacity clamping when the list is full (additional inserts are
 *      ignored).
 *   3. Multi-code strings (the '+' and newline separators used by
 *      RetroArch's cheat .cht files).
 *   4. Application against a 16 MB simulated address space, exercising
 *      byte / word / long writes with big-endian ordering matching the
 *      Jaguar bus (so the values on the wire match the emulator).
 *   5. "ROM simulation" end-to-end scenario: a fake CPU main loop writes
 *      a health value to RAM each frame, the cheat re-applies after the
 *      frame, and we confirm the patched value survives across frames.
 *   6. Example codes taken from publicly-documented Jaguar cheat sources
 *      (Pro Action Replay format) to prove the parser accepts them as-is.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#include "cheat.h"

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(expr) do {                                                  \
   tests_run++;                                                           \
   if (!(expr)) {                                                         \
      tests_failed++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
   }                                                                      \
} while (0)

#define CHECK_EQ_U(a, b) do {                                             \
   uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b);                       \
   tests_run++;                                                           \
   if (_a != _b) {                                                        \
      tests_failed++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s (=%llx) != %s (=%llx)\n",           \
              __FILE__, __LINE__, #a,                                     \
              (unsigned long long)_a, #b, (unsigned long long)_b);        \
   }                                                                      \
} while (0)

/* --------------------------------------------------------------------- */
/* 1. Parser                                                              */
/* --------------------------------------------------------------------- */

static void test_parse_valid_formats(void)
{
   uint32_t addr, val;
   uint8_t  size;

   /* 6+2: short address + byte */
   CHECK(cheat_parse_one("F03200 7F", &addr, &val, &size));
   CHECK_EQ_U(addr, 0xF03200u);
   CHECK_EQ_U(val,  0x7Fu);
   CHECK_EQ_U(size, 1u);

   /* 6+4: short address + word */
   CHECK(cheat_parse_one("003D00:FFFF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x003D00u);
   CHECK_EQ_U(val,  0xFFFFu);
   CHECK_EQ_U(size, 2u);

   /* 8+4: full address + word (Pro Action Replay canonical) */
   CHECK(cheat_parse_one("00003D00 FFFF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x003D00u);        /* masked to 24 bits */
   CHECK_EQ_U(val,  0xFFFFu);
   CHECK_EQ_U(size, 2u);

   /* 6+8: short address + long */
   CHECK(cheat_parse_one("100000 CAFEBABE", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x100000u);
   CHECK_EQ_U(val,  0xCAFEBABEu);
   CHECK_EQ_U(size, 4u);

   /* 8+8: full address + long */
   CHECK(cheat_parse_one("00100000-DEADBEEF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x100000u);
   CHECK_EQ_U(val,  0xDEADBEEFu);
   CHECK_EQ_U(size, 4u);

   /* 8+2: full address + byte — boundary via space or last ':', '-', '.' */
   CHECK(cheat_parse_one("00003D00 FF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x003D00u);
   CHECK_EQ_U(val,  0xFFu);
   CHECK_EQ_U(size, 1u);

   CHECK(cheat_parse_one("00003D00:FF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x003D00u);
   CHECK_EQ_U(val,  0xFFu);
   CHECK_EQ_U(size, 1u);

   CHECK(cheat_parse_one("0000:3D00:FF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x003D00u);
   CHECK_EQ_U(val,  0xFFu);
   CHECK_EQ_U(size, 1u);
}

static void test_parse_separators(void)
{
   uint32_t addr, val;
   uint8_t  size;

   /* All of these must yield the same result. */
   const char *equivalents[] = {
      "00003D00 FFFF",
      "00003D00:FFFF",
      "00003D00-FFFF",
      "00003D00.FFFF",
      "00003D00FFFF",
      "0000:3D00-FFFF",
      "00 00 3D 00 FF FF",
      "  00003D00   FFFF  ",
      NULL
   };
   size_t i;
   for (i = 0; equivalents[i]; i++)
   {
      CHECK(cheat_parse_one(equivalents[i], &addr, &val, &size));
      CHECK_EQ_U(addr, 0x003D00u);
      CHECK_EQ_U(val,  0xFFFFu);
      CHECK_EQ_U(size, 2u);
   }
}

static void test_parse_case_insensitive(void)
{
   uint32_t addr, val;
   uint8_t  size;
   CHECK(cheat_parse_one("abcdef 12", &addr, &val, &size));
   CHECK_EQ_U(addr, 0xABCDEFu);
   CHECK_EQ_U(val,  0x12u);
   CHECK(cheat_parse_one("ABCDEF 12", &addr, &val, &size));
   CHECK_EQ_U(addr, 0xABCDEFu);
   CHECK(cheat_parse_one("AbCdEf 12", &addr, &val, &size));
   CHECK_EQ_U(addr, 0xABCDEFu);
}

static void test_parse_address_masked_to_24_bits(void)
{
   uint32_t addr, val;
   uint8_t  size;
   /* Pro Action Replay codes sometimes carry a high byte encoding
    * region/metadata. We strip it — only the 24-bit bus address matters. */
   CHECK(cheat_parse_one("FF003D00 FFFF", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x003D00u);
}

static void test_parse_invalid(void)
{
   uint32_t addr = 0xdeadbeef, val = 0xdeadbeef;
   uint8_t  size = 99;

   /* NULL inputs */
   CHECK(!cheat_parse_one(NULL,      &addr, &val, &size));
   CHECK(!cheat_parse_one("00 11",    NULL, &val, &size));
   CHECK(!cheat_parse_one("00 11",   &addr,  NULL, &size));
   CHECK(!cheat_parse_one("00 11",   &addr, &val,   NULL));

   /* Wrong total length (all other lengths must fail) */
   CHECK(!cheat_parse_one("",                          &addr, &val, &size));
   CHECK(!cheat_parse_one("12",                        &addr, &val, &size));
   CHECK(!cheat_parse_one("1234",                      &addr, &val, &size));
   CHECK(!cheat_parse_one("123456",                    &addr, &val, &size)); /* addr only */
   CHECK(!cheat_parse_one("1234567",                   &addr, &val, &size)); /* 7 digits */
   CHECK(!cheat_parse_one("123456789",                 &addr, &val, &size)); /* 9 digits */
   CHECK(!cheat_parse_one("12345678 1",                &addr, &val, &size)); /* 9 digits */
   CHECK(!cheat_parse_one("12345678 123",              &addr, &val, &size)); /* 11 digits */
   CHECK(!cheat_parse_one("12345678 12345",            &addr, &val, &size)); /* 13 */
   CHECK(!cheat_parse_one("12345678 1234567",          &addr, &val, &size)); /* 15 */
   CHECK(!cheat_parse_one("12345678 123456789",        &addr, &val, &size)); /* 17 */

   /* Bad characters */
   CHECK(!cheat_parse_one("GGGGGG 12",                 &addr, &val, &size));
   CHECK(!cheat_parse_one("00003D00 FFFG",             &addr, &val, &size));
   CHECK(!cheat_parse_one("00003D00/FFFF",             &addr, &val, &size));
   CHECK(!cheat_parse_one("00003D00,FFFF",             &addr, &val, &size));

   /* More than 16 hex digits (longest valid single code is 8+8). */
   CHECK(!cheat_parse_one("00003D00FFFFFFFF1",         &addr, &val, &size));
}

/* --------------------------------------------------------------------- */
/* 2. List management                                                     */
/* --------------------------------------------------------------------- */

static void test_list_add_and_remove(void)
{
   cheat_list_t list;
   cheat_list_reset(&list);
   CHECK_EQ_U(list.count, 0u);

   cheat_list_set(&list, 0, true, "00003D00 FFFF");
   CHECK_EQ_U(list.count, 1u);
   CHECK_EQ_U(list.entries[0].address, 0x003D00u);
   CHECK_EQ_U(list.entries[0].value,   0xFFFFu);
   CHECK_EQ_U(list.entries[0].size,    2u);
   CHECK_EQ_U(list.entries[0].tag,     0u);
   CHECK(list.entries[0].enabled);

   cheat_list_set(&list, 1, true, "100000 BEEF");
   CHECK_EQ_U(list.count, 2u);

   /* Disable index 0 — must remove the first entry only. */
   cheat_list_set(&list, 0, false, "");
   CHECK_EQ_U(list.count, 1u);
   CHECK_EQ_U(list.entries[0].address, 0x100000u);
   CHECK_EQ_U(list.entries[0].tag,     1u);

   /* enabled=false must clear the slot even when code is NULL. */
   cheat_list_set(&list, 1, false, NULL);
   CHECK_EQ_U(list.count, 0u);

   cheat_list_reset(&list);
}

static void test_list_index_not_truncated(void)
{
   cheat_list_t list;
   cheat_list_reset(&list);
   cheat_list_set(&list, 0, true, "001000 7F");
   cheat_list_set(&list, 256, true, "002000 AB");
   CHECK_EQ_U(list.count, 2u);
   cheat_list_set(&list, 256, false, NULL);
   CHECK_EQ_U(list.count, 1u);
   CHECK_EQ_U(list.entries[0].tag, 0u);

   cheat_list_reset(&list);
   CHECK_EQ_U(list.count, 0u);
}

static void test_list_replace_same_index(void)
{
   cheat_list_t list;
   cheat_list_reset(&list);

   cheat_list_set(&list, 5, true, "00003D00 FFFF");
   cheat_list_set(&list, 5, true, "00100000 CAFE"); /* replace */
   CHECK_EQ_U(list.count, 1u);
   CHECK_EQ_U(list.entries[0].address, 0x100000u);
   CHECK_EQ_U(list.entries[0].value,   0xCAFEu);
}

static void test_list_multi_code_string(void)
{
   cheat_list_t list;
   cheat_list_reset(&list);

   /* '+' and newline as separators — this mirrors multi-line .cht codes. */
   cheat_list_set(&list, 3, true,
                  "00003D00 FFFF+"
                  "00100000 BEEF\n"
                  "00200000 CAFEBABE");
   CHECK_EQ_U(list.count, 3u);
   CHECK_EQ_U(list.entries[0].address, 0x003D00u);
   CHECK_EQ_U(list.entries[0].size,    2u);
   CHECK_EQ_U(list.entries[1].address, 0x100000u);
   CHECK_EQ_U(list.entries[1].value,   0xBEEFu);
   CHECK_EQ_U(list.entries[2].address, 0x200000u);
   CHECK_EQ_U(list.entries[2].size,    4u);
   CHECK_EQ_U(list.entries[2].value,   0xCAFEBABEu);

   /* Toggling the group off removes all three. */
   cheat_list_set(&list, 3, false, "");
   CHECK_EQ_U(list.count, 0u);
}

static void test_list_skips_malformed_entries(void)
{
   cheat_list_t list;
   cheat_list_reset(&list);

   /* Middle entry is garbage — the valid ones before and after must remain. */
   cheat_list_set(&list, 0, true,
                  "00003D00 FFFF+"
                  "garbage+"
                  "00100000 BEEF");
   CHECK_EQ_U(list.count, 2u);
   CHECK_EQ_U(list.entries[0].address, 0x003D00u);
   CHECK_EQ_U(list.entries[1].address, 0x100000u);
}

static void test_list_capacity(void)
{
   cheat_list_t list;
   unsigned i;
   cheat_list_reset(&list);
   /* Fill with distinct cheat indices until the list hits CHEAT_MAX_ENTRIES. */
   for (i = 0; i < CHEAT_MAX_ENTRIES + 50; i++)
   {
      char code[32];
      snprintf(code, sizeof(code), "%06X FF", i);
      cheat_list_set(&list, i, true, code);
   }
   CHECK(list.count <= CHEAT_MAX_ENTRIES);
}

/* --------------------------------------------------------------------- */
/* 3. Application to simulated memory                                     */
/* --------------------------------------------------------------------- */

/* 16 MB simulated Jaguar-sized address space. Big-endian writes match
 * the real emulator, which targets a big-endian bus. */
#define SIM_MEM_SIZE (16 * 1024 * 1024)
static uint8_t sim_mem[SIM_MEM_SIZE];

static void sim_write(uint32_t addr, uint32_t value, uint8_t size, void *user)
{
   (void)user;
   addr &= 0x00FFFFFF;
   /* 24-bit bus: last in-range long write starts at 0xFFFFFC; word at 0xFFFFFE. */
   if ((size_t)addr + (size_t)size > (size_t)SIM_MEM_SIZE)
      return;
   switch (size)
   {
      case 1:
         sim_mem[addr] = (uint8_t)(value & 0xFF);
         break;
      case 2:
         sim_mem[addr]     = (uint8_t)((value >> 8) & 0xFF);
         sim_mem[addr + 1] = (uint8_t)(value       & 0xFF);
         break;
      case 4:
         sim_mem[addr]     = (uint8_t)((value >> 24) & 0xFF);
         sim_mem[addr + 1] = (uint8_t)((value >> 16) & 0xFF);
         sim_mem[addr + 2] = (uint8_t)((value >>  8) & 0xFF);
         sim_mem[addr + 3] = (uint8_t)(value         & 0xFF);
         break;
   }
}

static uint16_t sim_read16(uint32_t addr)
{
   addr &= 0x00FFFFFF;
   if ((size_t)addr + 2u > (size_t)SIM_MEM_SIZE)
      return 0;
   return (uint16_t)((sim_mem[addr] << 8) | sim_mem[addr + 1]);
}

static uint32_t sim_read32(uint32_t addr)
{
   addr &= 0x00FFFFFF;
   if ((size_t)addr + 4u > (size_t)SIM_MEM_SIZE)
      return 0;
   return ((uint32_t)sim_mem[addr]     << 24) |
          ((uint32_t)sim_mem[addr + 1] << 16) |
          ((uint32_t)sim_mem[addr + 2] <<  8) |
           (uint32_t)sim_mem[addr + 3];
}

static void test_apply_byte_word_long(void)
{
   cheat_list_t list;
   memset(sim_mem, 0, sizeof(sim_mem));
   cheat_list_reset(&list);

   cheat_list_set(&list, 0, true, "001000 7F");        /* byte */
   cheat_list_set(&list, 1, true, "002000 ABCD");      /* word */
   cheat_list_set(&list, 2, true, "003000 DEADBEEF");  /* long */

   cheat_list_apply(&list, sim_write, NULL);

   CHECK_EQ_U(sim_mem[0x001000],  0x7Fu);
   CHECK_EQ_U(sim_read16(0x002000), 0xABCDu);
   CHECK_EQ_U(sim_read32(0x003000), 0xDEADBEEFu);
}

static void test_apply_disabled_not_written(void)
{
   cheat_list_t list;
   memset(sim_mem, 0, sizeof(sim_mem));
   cheat_list_reset(&list);

   cheat_list_set(&list, 0, true, "001000 FF");
   cheat_list_apply(&list, sim_write, NULL);
   CHECK_EQ_U(sim_mem[0x001000], 0xFFu);

   /* Disable it and wipe the location; re-apply must NOT touch it. */
   cheat_list_set(&list, 0, false, "");
   sim_mem[0x001000] = 0x00;
   cheat_list_apply(&list, sim_write, NULL);
   CHECK_EQ_U(sim_mem[0x001000], 0x00u);
}

/* --------------------------------------------------------------------- */
/* 4. End-to-end simulation ("fake ROM" that fights the cheat)            */
/* --------------------------------------------------------------------- */

/* Simulates the real-world situation where a running game writes a
 * value (e.g. current lives) to RAM every frame. Without a cheat, the
 * location holds whatever the "CPU" last wrote. With an infinite-lives
 * cheat applied after each frame, the patched value must win. */
static void fake_cpu_frame(uint32_t addr, uint8_t game_value)
{
   sim_mem[addr] = game_value;
}

static void test_end_to_end_frame_loop(void)
{
   cheat_list_t list;
   const uint32_t lives_addr = 0x00ABCD;
   int frame;

   memset(sim_mem, 0, sizeof(sim_mem));
   cheat_list_reset(&list);

   /* Without a cheat, the fake CPU decrements lives every frame. */
   for (frame = 5; frame >= 0; frame--)
   {
      fake_cpu_frame(lives_addr, (uint8_t)frame);
      cheat_list_apply(&list, sim_write, NULL); /* no-op: list empty */
   }
   CHECK_EQ_U(sim_mem[lives_addr], 0u);

   /* Now install an infinite-lives cheat: every frame, after the game
    * writes the "real" value, we clobber it back to 9. */
   cheat_list_set(&list, 0, true, "00ABCD 09");
   for (frame = 5; frame >= 0; frame--)
   {
      fake_cpu_frame(lives_addr, (uint8_t)frame);
      cheat_list_apply(&list, sim_write, NULL);
      CHECK_EQ_U(sim_mem[lives_addr], 9u);
   }
}

/* --------------------------------------------------------------------- */
/* 5. Real-world-shaped examples                                          */
/* --------------------------------------------------------------------- */

/* These are the sorts of codes a user copy-pastes from a cheat database
 * into the RetroArch cheat editor. We don't ship any game-specific cheat
 * database (for licensing reasons), but the parser must accept the forms
 * they're published in. */
static void test_real_world_shapes(void)
{
   uint32_t addr, val;
   uint8_t  size;

   /* Typical "infinite lives" style — PAR canonical form. */
   CHECK(cheat_parse_one("00004ACC 0009", &addr, &val, &size));
   CHECK_EQ_U(size, 2u);

   /* Colon-separated 24-bit, byte value (common on homebrew docs). */
   CHECK(cheat_parse_one("004ACC:09", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x004ACCu);
   CHECK_EQ_U(val,  0x09u);
   CHECK_EQ_U(size, 1u);

   /* Long-value patch — e.g. replacing a jump instruction (pair of words). */
   CHECK(cheat_parse_one("00102030 4E714E71", &addr, &val, &size));
   CHECK_EQ_U(addr, 0x102030u);
   CHECK_EQ_U(val,  0x4E714E71u);   /* two NOPs */
   CHECK_EQ_U(size, 4u);
}

/* --------------------------------------------------------------------- */

int main(void)
{
   test_parse_valid_formats();
   test_parse_separators();
   test_parse_case_insensitive();
   test_parse_address_masked_to_24_bits();
   test_parse_invalid();

   test_list_add_and_remove();
   test_list_index_not_truncated();
   test_list_replace_same_index();
   test_list_multi_code_string();
   test_list_skips_malformed_entries();
   test_list_capacity();

   test_apply_byte_word_long();
   test_apply_disabled_not_written();

   test_end_to_end_frame_loop();
   test_real_world_shapes();

   printf("cheat tests: %d run, %d failed\n", tests_run, tests_failed);
   return tests_failed == 0 ? 0 : 1;
}
