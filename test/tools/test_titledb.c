/*
 * test_titledb.c
 *
 * Table-integrity + lookup unit test. Links titledb.c + crc32.c directly;
 * no core dlopen, no ROMs — CI-safe. c99 allowed.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "../../src/core/titledb.h"

/* Stub: the test calls TitleDBSetCRC directly, not TitleDBSetContent.
 * In the full core build, this is overridden by file.c's real implementation.
 */
uint32_t DetectPrependedHeaderSize(uint8_t *buffer, uint32_t size)
{
   (void)buffer;
   (void)size;
   return 0;
}

static int fails = 0;

#define CHECK(cond, msg) do { \
   if (cond) printf("  PASS: %s\n", msg); \
   else { printf("  FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void)
{
   int count = 0;
   int i = 0;
   int j = 0;
   int k = 0;
   int hooks_seen = 0;
   const TitleDBEntry *t = TitleDBTable(&count);

   CHECK(count > 0, "table is non-empty");

   for (i = 0; i < count; i++)
   {
      CHECK(t[i].crc32 != 0, "entry crc32 is non-zero");
      CHECK(t[i].name && t[i].name[0], "entry has a name");
      CHECK(t[i].pairs[0].key != NULL, "entry has at least one pair");

      for (j = i + 1; j < count; j++)
         CHECK(t[i].crc32 != t[j].crc32, "no duplicate crc32 keys");

      for (j = 0; j < TITLEDB_MAX_PAIRS && t[i].pairs[j].key; j++)
      {
         CHECK(strncmp(t[i].pairs[j].key, "virtualjaguar_", 14) == 0,
               "pair key is a core option key");
         CHECK(t[i].pairs[j].value && t[i].pairs[j].value[0],
               "pair value is non-empty");
         /* The enhancement-hook gate is read RAW in retro_load_game,
          * precisely so a DB row cannot switch its own gate on.  Enforce
          * that mechanically here rather than by discipline: a row naming
          * the gate key is a table bug even though the raw read already
          * makes it inert. */
         CHECK(strcmp(t[i].pairs[j].key,
                      "virtualjaguar_enhancement_hooks") != 0,
               "no row names the enhancement-hook gate in its own pairs[]");
      }

      /* Enhancement hooks (issue #370).  The table ships zero hook rows
       * today, so this loop is currently vacuous by design -- it exists so
       * the FIRST row added is validated at `make test` time, before it can
       * reach a user.  See the authoring checklist in src/core/titledb.c. */
      hooks_seen = 0;
      for (j = 0; j < TITLEDB_MAX_HOOKS; j++)
      {
         const TitleDBHook *h = &t[i].hooks[j];

         if (h->kind == TITLEDB_HOOK_NONE)
         {
            /* Terminated: everything after must be NONE too, or the
             * applier's terminator scan would silently drop hooks. */
            for (k = j; k < TITLEDB_MAX_HOOKS; k++)
               CHECK(t[i].hooks[k].kind == TITLEDB_HOOK_NONE,
                     "hooks[] has no live entry after its terminator");
            break;
         }

         hooks_seen++;
         CHECK(h->kind == TITLEDB_HOOK_ROM_PATCH, "hook kind is a known kind");
         CHECK(h->len >= 1 && h->len <= TITLEDB_HOOK_MAX_BYTES,
               "hook len is 1..TITLEDB_HOOK_MAX_BYTES");
         CHECK(h->expect != NULL, "hook has a mandatory expect[]");
         CHECK(h->patch != NULL, "hook has a patch[]");
         CHECK(h->name != NULL && h->name[0], "hook has a name");
         if (h->expect && h->patch && h->len)
            CHECK(memcmp(h->expect, h->patch, h->len) != 0,
                  "hook actually changes bytes (a no-op hook is a table bug)");
         /* Mirrors the applier's entry-vector fence: cart $400..$407 is
          * consumed by the loader before hooks run, so a row there would
          * ship as a permanent silent no-op. */
         CHECK(!(h->offset < 0x408u && (h->offset + h->len) > 0x400u),
               "hook does not overlap the cart entry vector $400..$407");

         for (k = 0; k < j; k++)
         {
            const TitleDBHook *p = &t[i].hooks[k];
            CHECK(!(h->offset < (p->offset + p->len)
                    && p->offset < (h->offset + h->len)),
                  "hooks within a row do not overlap");
            CHECK(!(p->name && h->name && strcmp(p->name, h->name) == 0),
                  "hook names are unique within a row");
         }
      }
      (void)hooks_seen;
   }

   /* Lookup behaviour without content: no override. */
   TitleDBSetCRC(0);
   CHECK(TitleDBOverride("virtualjaguar_true_color") == NULL,
         "no content -> no override");
   CHECK(TitleDBTitleName() == NULL, "no content -> no title");

   /* Unknown content (CRC of 0x12345678): no override. */
   TitleDBSetCRC(0x12345678);
   CHECK(TitleDBOverride("virtualjaguar_true_color") == NULL,
         "unknown content -> no override");

   /* TitleDBContentCRC mirrors the last SetCRC / SetContent (issue #409:
    * the miss log line in libretro.c prints it). */
   TitleDBSetCRC(0xDEADBEEF);
   CHECK(TitleDBContentCRC() == 0xDEADBEEFu, "content CRC getter mirrors SetCRC");
   {
      static const uint8_t crcbuf[4] = { 0x01, 0x02, 0x03, 0x04 };
      TitleDBSetContent(crcbuf, 4);
      CHECK(TitleDBContentCRC() == 0xB63CFBCDu, "content CRC getter after SetContent");
   }
   TitleDBSetCRC(0);
   CHECK(TitleDBContentCRC() == 0, "content CRC getter clears with SetCRC(0)");

   /* Alias rows: Doom EX romhack builds inherit Doom's enhancement pairs
    * (issue #409).  One representative CRC; the table-integrity loop above
    * already covers the rest (non-zero CRC, no duplicates, valid pairs). */
   TitleDBSetCRC(0x754096DB);
   CHECK(TitleDBOverride("virtualjaguar_internal_resolution") != NULL,
         "Doom EX alias row present and carries internal_resolution");
   TitleDBSetCRC(0);

   /* Hook lookup (issue #370). */
   {
      int hcount = -1;
      static const uint8_t fake_expect[2] = { 0x12, 0x34 };
      static const uint8_t fake_patch[2]  = { 0x56, 0x78 };
      static TitleDBHook fake[2];

      TitleDBSetCRC(0);
      CHECK(TitleDBHooks(&hcount) == NULL, "no content -> no hooks");

      TitleDBSetCRC(0x12345678);
      CHECK(TitleDBHooks(&hcount) == NULL, "unknown content -> no hooks");

      /* A matched row returns its (currently empty) array, terminated. */
      TitleDBSetCRC(0x754096DB);
      {
         const TitleDBHook *h = TitleDBHooks(&hcount);
         CHECK(h != NULL, "matched row returns a hook array");
         CHECK(hcount == TITLEDB_MAX_HOOKS,
               "matched row reports TITLEDB_MAX_HOOKS slots");
         CHECK(h && h[0].kind == TITLEDB_HOOK_NONE,
               "shipped rows carry zero hooks (mechanism ships, table does not)");
      }

      /* The test-only setter wins regardless of CRC match, which is what
       * keeps the end-to-end gate test out of the shipped table -- a canary
       * row on yarc.j64 would break test_pertitle_db --case 5. */
      fake[0].kind   = TITLEDB_HOOK_ROM_PATCH;
      fake[0].len    = 2;
      fake[0].offset = 0x1000;
      fake[0].expect = fake_expect;
      fake[0].patch  = fake_patch;
      fake[0].name   = "fake";
      fake[1].kind   = TITLEDB_HOOK_NONE;

      TitleDBSetCRC(0);
      TitleDBSetHooksForTest(fake, 2);
      {
         const TitleDBHook *h = TitleDBHooks(&hcount);
         CHECK(h == fake && hcount == 2,
               "test setter overrides lookup even with no content loaded");
      }
      TitleDBSetHooksForTest(NULL, 0);
      CHECK(TitleDBHooks(&hcount) == NULL, "test setter clears back to normal");
   }
   TitleDBSetCRC(0);

   printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
   return fails ? 1 : 0;
}
