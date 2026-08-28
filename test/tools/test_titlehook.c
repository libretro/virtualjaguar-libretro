/*
 * test_titlehook.c
 *
 * Host unit test for the enhancement-hook applier (issue #370).  Links
 * src/core/titlehook.c + src/core/titledb.c + src/core/crc32.c directly --
 * no core dlopen, no ROMs, CI-safe -- and drives every fence against a
 * malloc'd stand-in for the cartridge window.
 *
 * The applier is deliberately split so this is possible:
 * TitleHookApplyToBuffer() is pure (image in, count out, no core global),
 * and TitleHookApplyROM() is the thin wrapper that reads
 * jaguarCartInserted / jaguarROMSize / jaguarMainROM / jgdActive.  This
 * file supplies those globals so the wrapper's own three fences (gate off,
 * not-a-cartridge, CRC disagreement) are covered too.
 *
 * The single most important assertion in here is not "the patch landed"
 * but "on ANY refusal, the image is byte-for-byte unchanged".  Every
 * refusal case re-verifies the whole buffer against a pristine copy, so a
 * partial write cannot pass as a refusal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include <boolean.h>
#include <libretro.h>

#include "../../src/core/titledb.h"
#include "../../src/core/titlehook.h"
#include "../../src/core/hookfile.h"
#include "../../src/core/jaggd.h"

/* ------------------------------------------------------------------
 * Stubs for the core globals the wrapper reads, and for the logger.
 * ------------------------------------------------------------------ */

retro_log_printf_t vj_log_cb = NULL;

bool     jaguarCartInserted = false;
uint32_t jaguarMainROMCRC32 = 0;
uint32_t jaguarROMSize      = 0;
uint8_t *jaguarMainROM      = NULL;

uint8_t  jgdActive          = 0;

/* titledb.c calls this from TitleDBSetContent; the test drives
 * TitleDBSetCRC directly, exactly like test_titledb.c does. */
uint32_t DetectPrependedHeaderSize(uint8_t *buffer, uint32_t size)
{
   (void)buffer;
   (void)size;
   return 0;
}

/* ------------------------------------------------------------------
 * Harness
 * ------------------------------------------------------------------ */

static int fails = 0;

#define CHECK(cond, msg) do { \
   if (cond) printf("  PASS: %s\n", msg); \
   else { printf("  FAIL: %s\n", msg); fails++; } \
} while (0)

#define ROM_BYTES 4096

static uint8_t rom[ROM_BYTES];
static uint8_t pristine[ROM_BYTES];

static void rom_reset(void)
{
   int i;
   for (i = 0; i < ROM_BYTES; i++)
      rom[i] = (uint8_t)(i * 7 + 3);
   memcpy(pristine, rom, ROM_BYTES);
}

static int rom_untouched(void)
{
   return memcmp(rom, pristine, ROM_BYTES) == 0;
}

/* Bytes the pattern above puts at a given offset, for expect[]. */
static uint8_t pat(int off)
{
   return (uint8_t)(off * 7 + 3);
}

/* ------------------------------------------------------------------
 * Hook fixtures.  expect[]/patch[] must outlive the call, so they are
 * file-scope statics rather than locals.
 * ------------------------------------------------------------------ */

static uint8_t exp4[4];
static const uint8_t pat4[4]     = { 0xDE, 0xAD, 0xBE, 0xEF };
static const uint8_t wrong4[4]   = { 0x00, 0x01, 0x02, 0x03 };
static uint8_t exp1a[1];
static uint8_t exp1b[1];
static uint8_t exp1c[1];
static const uint8_t one_a[1]    = { 0xA1 };
static const uint8_t one_b[1]    = { 0xB2 };
static const uint8_t one_c[1]    = { 0xC3 };
static uint8_t expv[4];
static uint8_t exp_lo[4];
static uint8_t exp_hi[4];
static const uint8_t patv[4]     = { 0x11, 0x22, 0x33, 0x44 };

static TitleDBHook mkhook(uint32_t offset, uint8_t len,
                          const uint8_t *expect, const uint8_t *patch,
                          const char *name)
{
   TitleDBHook h;
   memset(&h, 0, sizeof(h));
   h.kind   = TITLEDB_HOOK_ROM_PATCH;
   h.len    = len;
   h.offset = offset;
   h.expect = expect;
   h.patch  = patch;
   h.name   = name;
   return h;
}

static void fill_expect(uint8_t *dst, int off, int len)
{
   int i;
   for (i = 0; i < len; i++)
      dst[i] = pat(off + i);
}

int main(void)
{
   TitleDBHook hooks[4];
   int n;
   int i;

   printf("test_titlehook: enhancement-hook applier (issue #370)\n");

   /* ---- case 1: clean apply, and nothing outside the window moves ---- */
   rom_reset();
   fill_expect(exp4, 0x100, 4);
   hooks[0] = mkhook(0x100, 4, exp4, pat4, "clean-apply");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case1", 0);
   CHECK(n == 1, "case1: clean apply returns 1");
   CHECK(memcmp(rom + 0x100, pat4, 4) == 0, "case1: patch bytes landed");
   CHECK(memcmp(rom, pristine, 0x100) == 0,
         "case1: bytes before the window untouched");
   CHECK(memcmp(rom + 0x104, pristine + 0x104, ROM_BYTES - 0x104) == 0,
         "case1: bytes after the window untouched");

   /* ---- case 2: expect[] mismatch, single hook -> zero bytes written ---- */
   rom_reset();
   hooks[0] = mkhook(0x100, 4, wrong4, pat4, "bad-precondition");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case2", 0);
   CHECK(n == 0, "case2: expect[] mismatch refuses");
   CHECK(rom_untouched(), "case2: ZERO bytes written on refusal");

   /* ---- case 3: mismatch in hook 2 of 3 -> all-or-nothing ---- */
   rom_reset();
   fill_expect(exp1a, 0x200, 1);
   fill_expect(exp1c, 0x220, 1);
   hooks[0] = mkhook(0x200, 1, exp1a,  one_a, "h1");
   hooks[1] = mkhook(0x210, 1, wrong4, one_b, "h2-bad");
   hooks[2] = mkhook(0x220, 1, exp1c,  one_c, "h3");
   hooks[3].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case3", 0);
   CHECK(n == 0, "case3: one bad hook in three refuses the entry");
   CHECK(rom_untouched(),
         "case3: ZERO bytes written -- hook 1 did not land either");

   /* Sanity: the same three hooks all-valid do apply, so case 3 failed for
    * the right reason and not because the fixture was broken. */
   rom_reset();
   fill_expect(exp1a, 0x200, 1);
   fill_expect(exp1b, 0x210, 1);
   fill_expect(exp1c, 0x220, 1);
   hooks[0] = mkhook(0x200, 1, exp1a, one_a, "h1");
   hooks[1] = mkhook(0x210, 1, exp1b, one_b, "h2");
   hooks[2] = mkhook(0x220, 1, exp1c, one_c, "h3");
   hooks[3].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case3b", 0);
   CHECK(n == 3, "case3b: three valid hooks all apply");
   CHECK(rom[0x200] == 0xA1 && rom[0x210] == 0xB2 && rom[0x220] == 0xC3,
         "case3b: all three patches landed");

   /* ---- case 4: out of range ---- */
   rom_reset();
   fill_expect(exp4, 0x100, 4);
   hooks[0] = mkhook(ROM_BYTES - 2, 4, exp4, pat4, "past-end");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case4", 0);
   CHECK(n == 0 && rom_untouched(), "case4: offset+len past the image refused");

   rom_reset();
   hooks[0] = mkhook(0xFFFFFFF0u, 4, exp4, pat4, "wrap");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case4b", 0);
   CHECK(n == 0 && rom_untouched(),
         "case4b: offset near UINT32_MAX cannot wrap into range");

   /* ---- case 5: malformed records ---- */
   rom_reset();
   fill_expect(exp4, 0x100, 4);

   hooks[0] = mkhook(0x100, 0, exp4, pat4, "len-zero");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case5a", 0);
   CHECK(n == 0 && rom_untouched(), "case5a: len == 0 refused");

   hooks[0] = mkhook(0x100, TITLEDB_HOOK_MAX_BYTES + 1, exp4, pat4, "len-big");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case5b", 0);
   CHECK(n == 0 && rom_untouched(), "case5b: len > 64 refused");

   hooks[0] = mkhook(0x100, 4, NULL, pat4, "no-expect");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case5c", 0);
   CHECK(n == 0 && rom_untouched(), "case5c: expect == NULL refused");

   hooks[0] = mkhook(0x100, 4, exp4, NULL, "no-patch");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case5d", 0);
   CHECK(n == 0 && rom_untouched(), "case5d: patch == NULL refused");

   hooks[0] = mkhook(0x100, 4, exp4, pat4, NULL);
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case5e", 0);
   CHECK(n == 0 && rom_untouched(), "case5e: name == NULL refused");

   hooks[0] = mkhook(0x100, 4, exp4, pat4, "bad-kind");
   hooks[0].kind = 99;
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case5f", 0);
   CHECK(n == 0 && rom_untouched(), "case5f: unknown kind refused");

   /* ---- case 7: GameDrive content is fenced off ---- */
   rom_reset();
   fill_expect(exp4, 0x100, 4);
   hooks[0] = mkhook(0x100, 4, exp4, pat4, "gd-flat");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case7a", 1);
   CHECK(n == 0 && rom_untouched(),
         "case7a: jgdActive refused (banked image would defeat the patch)");

   n = TitleHookApplyToBuffer(rom, (uint32_t)JGD_AUTO_THRESHOLD + 1,
                              hooks, 4, "Case7b", 0);
   CHECK(n == 0 && rom_untouched(),
         "case7b: image larger than the flat cart window refused");

   /* ---- case 8: the entry-vector fence ---- */
   rom_reset();
   fill_expect(expv, 0x404, 4);
   hooks[0] = mkhook(0x404, 4, expv, patv, "entry-vector");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case8a", 0);
   CHECK(n == 0 && rom_untouched(),
         "case8a: a patch at cart+$404 is refused (silent no-op guard)");

   rom_reset();
   fill_expect(expv, 0x400, 4);
   hooks[0] = mkhook(0x400, 4, expv, patv, "entry-ssp");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case8b", 0);
   CHECK(n == 0 && rom_untouched(), "case8b: a patch at cart+$400 is refused");

   /* Straddling from below and from above, so the range test is a real
    * overlap test and not two endpoint comparisons. */
   rom_reset();
   fill_expect(expv, 0x3FE, 4);
   hooks[0] = mkhook(0x3FE, 4, expv, patv, "straddle-low");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case8c", 0);
   CHECK(n == 0 && rom_untouched(),
         "case8c: a patch straddling $3FE..$401 is refused");

   rom_reset();
   fill_expect(expv, 0x406, 4);
   hooks[0] = mkhook(0x406, 4, expv, patv, "straddle-high");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case8d", 0);
   CHECK(n == 0 && rom_untouched(),
         "case8d: a patch straddling $406..$409 is refused");

   /* ---- case 9: the fence is exact, not over-broad ---- */
   rom_reset();
   fill_expect(exp_lo, 0x3FC, 4);
   fill_expect(exp_hi, 0x408, 4);
   hooks[0] = mkhook(0x3FC, 4, exp_lo, patv, "just-below");
   hooks[1] = mkhook(0x408, 4, exp_hi, patv, "just-above");
   hooks[2].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "Case9", 0);
   CHECK(n == 2, "case9: $3FC..$3FF and $408..$40B both accepted");
   CHECK(memcmp(rom + 0x3FC, patv, 4) == 0
         && memcmp(rom + 0x408, patv, 4) == 0,
         "case9: both adjacent patches landed");
   CHECK(memcmp(rom + 0x400, pristine + 0x400, 8) == 0,
         "case9: the vector itself was not touched");

   /* ---- empty / degenerate arrays ---- */
   rom_reset();
   hooks[0].kind = TITLEDB_HOOK_NONE;
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, hooks, 4, "CaseEmpty", 0);
   CHECK(n == 0 && rom_untouched(), "empty hook array is a no-op");
   n = TitleHookApplyToBuffer(rom, ROM_BYTES, NULL, 4, "CaseNull", 0);
   CHECK(n == 0 && rom_untouched(), "NULL hook array is a no-op");
   n = TitleHookApplyToBuffer(NULL, ROM_BYTES, hooks, 4, "CaseNullRom", 0);
   CHECK(n == 0, "NULL image is a no-op");

   /* ------------------------------------------------------------------
    * Wrapper fences: gate, cartridge, CRC.  TitleHookApplyROM() reads the
    * stub globals above plus the titledb match.
    * ------------------------------------------------------------------ */

   rom_reset();
   fill_expect(exp4, 0x100, 4);
   hooks[0] = mkhook(0x100, 4, exp4, pat4, "wrapper");
   hooks[1].kind = TITLEDB_HOOK_NONE;
   TitleDBSetHooksForTest(hooks, 4);

   jaguarMainROM      = rom;
   jaguarROMSize      = ROM_BYTES;
   jaguarCartInserted = true;
   jaguarMainROMCRC32 = 0x11223344u;
   TitleDBSetCRC(0x11223344u);
   jgdActive          = 0;

   /* ---- case 6: gate off (the default) ---- */
   TitleHookSetEnabled(0);
   CHECK(TitleHookGetEnabled() == 0, "case6: gate defaults to off");
   n = TitleHookApplyROM();
   CHECK(n == 0 && rom_untouched(), "case6: gate off refuses a valid entry");

   /* not a cartridge */
   TitleHookSetEnabled(1);
   jaguarCartInserted = false;
   n = TitleHookApplyROM();
   CHECK(n == 0 && rom_untouched(),
         "wrapper: non-cartridge content refused");
   jaguarCartInserted = true;

   /* CRC disagreement between the table key and the loaded image */
   jaguarMainROMCRC32 = 0x55667788u;
   n = TitleHookApplyROM();
   CHECK(n == 0 && rom_untouched(), "wrapper: CRC disagreement refused");
   jaguarMainROMCRC32 = 0x11223344u;

   /* GameDrive, through the wrapper */
   jgdActive = 1;
   n = TitleHookApplyROM();
   CHECK(n == 0 && rom_untouched(), "wrapper: GameDrive content refused");
   jgdActive = 0;

   /* Everything satisfied: the patch lands. */
   n = TitleHookApplyROM();
   CHECK(n == 1, "wrapper: gate on + all fences clear -> applied");
   CHECK(memcmp(rom + 0x100, pat4, 4) == 0, "wrapper: patch bytes landed");

   /* The override clears cleanly, so a later load cannot inherit it. */
   TitleDBSetHooksForTest(NULL, 0);
   CHECK(TitleDBHooks(NULL) == NULL,
         "test hook override clears back to normal table lookup");
   TitleHookSetEnabled(0);
   TitleDBSetCRC(0);

   /* ---- user hook file (#637) ------------------------------------- */
   {
      HookFileSet set;
      const char *path = "/tmp/vj_hookfile_test.txt";
      FILE *f;

      /* Absent file is the normal case and must be silent + empty. */
      CHECK(HookFileLoad("/tmp/definitely-not-here-637.txt", 0xAABBCCDDu,
                         &set) == 0,
            "hookfile: absent file yields zero hooks");

      /* Well-formed: two hooks under the matching CRC, one under another
       * title that must NOT be picked up. */
      f = fopen(path, "w");
      CHECK(f != NULL, "hookfile: fixture writable");
      fprintf(f, "# comment line\n");
      fprintf(f, "crc=AABBCCDD\n");
      fprintf(f, "hook=first  0x100 %02X%02X%02X%02X AABBCCDD\n",
              pristine[0x100], pristine[0x101], pristine[0x102],
              pristine[0x103]);
      fprintf(f, "hook=second 0x200 %02X 5A   # trailing comment\n",
              pristine[0x200]);
      fprintf(f, "crc=99999999\n");
      fprintf(f, "hook=other 0x300 %02X 11\n", pristine[0x300]);
      fclose(f);

      CHECK(HookFileLoad(path, 0xAABBCCDDu, &set) == 2,
            "hookfile: parses exactly the matching CRC's hooks");
      CHECK(set.hooks[0].len == 4 && set.hooks[1].len == 1,
            "hookfile: per-hook lengths derived from the hex strings");
      CHECK(strcmp(set.hooks[0].name, "first") == 0,
            "hookfile: hook name carried through");
      CHECK(set.hooks[2].kind == TITLEDB_HOOK_NONE,
            "hookfile: array is kind==0 terminated");

      /* The parsed set drives the real applier against a real buffer. */
      rom_reset();
      n = TitleHookApplyToBuffer(rom, ROM_BYTES, set.hooks, set.count,
                                 "UserFile", 0);
      CHECK(n == 2, "hookfile: parsed hooks apply through the real applier");
      CHECK(rom[0x100] == 0xAA && rom[0x200] == 0x5A,
            "hookfile: patch bytes landed");

      /* A CRC with no section yields nothing. */
      CHECK(HookFileLoad(path, 0x12345678u, &set) == 0,
            "hookfile: unmatched CRC yields zero hooks");

      /* Malformed line discards the WHOLE file, not just that line --
       * otherwise the applier's all-or-nothing rule would be enforced
       * over a set we only partly understood. */
      f = fopen(path, "w");
      fprintf(f, "crc=AABBCCDD\n");
      fprintf(f, "hook=good 0x100 %02X AA\n", pristine[0x100]);
      fprintf(f, "hook=bad 0x200 ZZ 5A\n");
      fclose(f);
      CHECK(HookFileLoad(path, 0xAABBCCDDu, &set) == 0,
            "hookfile: one malformed line discards every hook in the file");

      /* expect/patch length disagreement is malformed. */
      f = fopen(path, "w");
      fprintf(f, "crc=AABBCCDD\nhook=mismatch 0x100 AABB CC\n");
      fclose(f);
      CHECK(HookFileLoad(path, 0xAABBCCDDu, &set) == 0,
            "hookfile: expect/patch length mismatch refused");

      /* Over-long hex cannot overrun the arena. */
      f = fopen(path, "w");
      fprintf(f, "crc=AABBCCDD\nhook=toolong 0x100 ");
      for (i = 0; i < TITLEDB_HOOK_MAX_BYTES + 4; i++) fprintf(f, "AA");
      fprintf(f, " ");
      for (i = 0; i < TITLEDB_HOOK_MAX_BYTES + 4; i++) fprintf(f, "BB");
      fprintf(f, "\n");
      fclose(f);
      CHECK(HookFileLoad(path, 0xAABBCCDDu, &set) == 0,
            "hookfile: over-long hex refused, arena not overrun");

      /* More hooks than TITLEDB_MAX_HOOKS refuses the file. */
      f = fopen(path, "w");
      fprintf(f, "crc=AABBCCDD\n");
      for (i = 0; i < TITLEDB_MAX_HOOKS + 1; i++)
         fprintf(f, "hook=h%d 0x%X %02X AA\n", i, 0x100 + i,
                 pristine[0x100 + i]);
      fclose(f);
      CHECK(HookFileLoad(path, 0xAABBCCDDu, &set) == 0,
            "hookfile: more than TITLEDB_MAX_HOOKS refuses the file");

      remove(path);
   }

   printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
   return fails ? 1 : 0;
}
