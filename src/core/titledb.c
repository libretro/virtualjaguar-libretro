/*
 * TITLEDB.C
 *
 * Per-title enhancement defaults database — keyed by CRC32.
 * Entries only apply when user has left options at their registered defaults.
 */

#include "titledb.h"

#include <string.h>
#include "crc32.h"
#include "file.h"

/*
 * Table-derivation policy (docs/hires-stage0-census.md, section
 * "2. Blit census — cartridge corpus"):
 *
 *  - An entry requires the census row's "Scene reached" to be actual
 *    gameplay (or attract that renders the game engine, e.g. an
 *    "attract/gameplay mix" scene). "menu", "menus only", "did not
 *    render", and "attract" alone do NOT qualify a row.
 *  - virtualjaguar_true_color=enabled when (shaded blits / Frames) >= 10.
 *  - virtualjaguar_internal_resolution=2x when (QUALIFY16 / Frames) >= 5.
 *  - A title can earn one or both pairs; titles earning neither get no
 *    entry. Rows that qualify on scene but miss both thresholds (Atari
 *    Karts, Checkered Flag, Club Drive, Defender 2000, Super Cross 3D,
 *    Wolfenstein 3D, Val d'Isere, Iron Soldier v1.04 — QUALIFY16 4.8/f,
 *    shaded 1.3/f, both below threshold even though reached via a
 *    savestate into mission 1) are intentionally excluded.
 *  - One census measurement per title; the resulting entry applies to
 *    every plain retail CRC of that title in src/core/filedb.c (Alpha/
 *    beta/proto/demo/bad-dump rows excluded; each distinct retail CRC
 *    gets its own table row).
 *  - Romhack alias rows (issue #409): a patched build inherits the
 *    enhancement defaults of the retail title it patches — same engine,
 *    same rendering — keyed by the patched image's CRC.  Each row is
 *    boot-verified before inclusion.  Coverage is exactly the enumerated
 *    builds; new patch releases hash differently and fall through to the
 *    miss log in retro_load_game.
 *
 * Seed entries (AvP, Cybermorph x2) predate this policy pass but were
 * verified to fall out of it unchanged: AvP shaded 544.3k/4800f=113/f,
 * QUALIFY16 405.8k/4800f=84.5/f (both thresholds cleared); Cybermorph
 * shaded 1.70M/2400f=708/f (cleared), QUALIFY16 13/2400f=0.005/f (below
 * threshold) -> true_color only.
 *
 * ------------------------------------------------------------------
 * hooks[] authoring checklist (issue #370, docs/enhancement-hooks.md)
 *
 * A hook row (a byte patch into cartridge ROM at load) is admissible
 * only when ALL of the following hold.  test_titledb enforces the
 * mechanical half at `make test` time; the rest is review.
 *
 *  1. `offset` is PAYLOAD-relative -- verified against a headerless
 *     dump, or with 512 subtracted from a headered one.  The CRC is
 *     identical for both dumps, so a raw hex-editor offset from a
 *     headered file is silently wrong by 512.
 *  2. `expect[]` was read out of the SHIPPED image at that offset, and
 *     `crc32` is the CRC of that exact image.  NEVER inherited from an
 *     alias row: the Doom EX rows above deliberately reuse retail
 *     Doom's pairs, and that is safe for settings and unsafe for bytes,
 *     because offset $X in a romhack is exactly the region the hack may
 *     have rewritten.  On mismatch the applier refuses and logs; it
 *     never writes.
 *  3. The behaviour the patch produces was determined by OUR OWN
 *     analysis (disassembly, vjtrace, m68k_pc_histogram, gpu_disasm_dump,
 *     trace_dump) and the comment cites which.  No third-party patch
 *     data is transliterated into this table.
 *  4. The defect is a GAME defect, not one of ours.  A hook whose root
 *     cause is emulator inaccuracy is rejected at review and re-filed
 *     against the accuracy track (#319 / #408).
 *  5. Before/after measurement is committed as the row's evidence, the
 *     way every existing row cites its census numbers.
 *  6. cart_boot_probe boots the title clean with the hook on.
 *  7. frame_hash_ab with virtualjaguar_enhancement_hooks OFF is
 *     byte-identical to the pre-change build.
 *
 * The table currently ships ZERO hook rows.  The mechanism is complete,
 * gated off by default and CI-covered; rows land as data once a
 * behaviour has been researched to the standard above.
 * ------------------------------------------------------------------
 */
static const TitleDBEntry titledb_table[] = {
   /* Alien vs Predator (retail) — 2x internal resolution + true color.
    * Evidence: docs/avp-renderer-analysis.md (Stage 2 A/B, frame 6000),
    * docs/hires-stage0-census.md (census GO), shipped in v3.2.0.
    * CRCs: Alien vs Predator (World) from src/core/filedb.c line 106. */
   {
      0xDC187F82, "Alien vs Predator",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         /* NOT tagged with virtualjaguar_blit_memo yet -- see
          * docs/blit-memo.md.  AvP is the memo's best-evidenced title
          * (710,433 verify checks, 0 divergences, bit-identical A/B
          * over 8,000 frames) but that covers the scenes one fixture
          * reaches, and the corpus sweep is unfinished.  Tagging here
          * turns the memo ON by default for every AvP user, so it
          * waits for the full sweep plus a device check. */
         { NULL, NULL }
      }
   },

   /* Cybermorph (retail, Rev 1) — true color kills gouraud banding.
    * Evidence: site/assets/truecolor_ab_cybermorph.png (committed A/B),
    * docs/true-color-shadowfb-design.md.
    * CRC: Cybermorph (World) (Rev 1) from src/core/filedb.c line 94. */
   {
      0xBDE67498, "Cybermorph",
      {
         { "virtualjaguar_true_color", "enabled" },
         { NULL, NULL }
      }
   },

   /* Cybermorph (retail, Rev 2) — true color kills gouraud banding.
    * Evidence: site/assets/truecolor_ab_cybermorph.png (committed A/B),
    * docs/true-color-shadowfb-design.md.
    * CRC: Cybermorph (World) (Rev 2) from src/core/filedb.c line 115. */
   {
      0xECF854E7, "Cybermorph",
      {
         { "virtualjaguar_true_color", "enabled" },
         { NULL, NULL }
      }
   },

   /* Air Cars (retail) — true color: census row "Aircars" 2400f,
    * shaded 47.8/f (>=10), QUALIFY16 0.003/f (<5), scene: gameplay
    * (terrain). Note: the §4 OP-scaler table measured the same corpus
    * copy as "Aircars (beta)"; policy applies the result to every
    * retail revision, so it is applied here to the plain "(World)" row.
    * CRC: Air Cars (World) from src/core/filedb.c line 50.
    * (Excluded: 0xCBFD822A "Air Cars (World) (alt)" is FF_BAD_DUMP.) */
   {
      0x40E1A1D0, "Air Cars",
      {
         { "virtualjaguar_true_color", "enabled" },
         { NULL, NULL }
      }
   },

   /* Doom (retail) — true color + 2x: census row "Doom" 2400f,
    * shaded 445.8/f (>=10), QUALIFY16 433.3/f (>=5), scene: gameplay
    * (E1M1).
    * CRC: Doom (World) from src/core/filedb.c line 62. */
   {
      0x5E2CDBC0, "Doom",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },

   /* Doom EX romhack family — alias rows inheriting Doom (0x5E2CDBC0)'s
    * pairs.  CRC = IPS patch applied to the verified retail dump;
    * boot-verified via cart_boot_probe (issue #409).
    *
    * NO virtualjaguar_p2_device ROW HERE, and it is not an oversight
    * (#429, #428).  #429 proposed auto-selecting the ST/Amiga mouse for
    * JagDoomEX by CRC; the design spec made that row conditional on
    * first confirming the title actually reads port 2.  It does not.
    * Disassembled against every CRC in this block (plus the 250318
    * archive build, which hashes to 0x35743B9C above):
    *
    *   - the pad poll writes only the port-1 row selects $81FE, $81FD,
    *     $81FB and $81F7.  The port-2 nibble is $F in all four, i.e.
    *     every port-2 row is deselected;
    *   - the poll then does or.l #$F0FFFFFC before and.l into an 8-bit
    *     accumulator, so $F14000 bits 12-15 -- the four lines a mouse
    *     adapter drives -- cannot reach the result;
    *   - all 11 absolute-long $F14000 references in the image are
    *     accounted for: 8 in that poll, one move.l #$100 at init, and
    *     two lea $F14000,a0 in the EEPROM driver (which reads bit 0).
    *     There is no other path to the register.
    *
    * A row here would therefore disconnect the port-2 RetroPad for a
    * title that reads nothing from port 2, in exchange for no function.
    * Do not add one without re-running that check against the build in
    * hand -- a future JagDoomEX release may add mouse support, and it
    * will hash differently from every CRC listed here anyway. */
   {
      0x754096DB, "Doom EX (JagDoomEX)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x4643E9DB, "Doom EX (JagDoomEX 2)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x35743B9C, "Doom EX (JagDoomEX 3)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xAD6B68BA, "Doom EX (JagDoomEX 4)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xC4F4CACF, "Doom EX (JagDoomEX 5)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x1F4EE4A5, "Doom EX (JagDoomEX 6)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x013A5359, "Doom EX (spectral)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xB92D1CA3, "Doom EX (transparent)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xEA12E234, "Doom EX (JagDoom2EX)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },

   /* Hover Strike (retail) — 2x: census row "Hover Strike (cart)" 6000f,
    * shaded 1.1/f (<10), QUALIFY16 336.7/f (>=5), scene: gameplay
    * (mission approach, ALERT).
    * CRC: Hover Strike (World) from src/core/filedb.c line 53. */
   {
      0x4899628F, "Hover Strike",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { NULL, NULL }
      }
   },

   /* I-War (retail) — true color + 2x: census row "I-War" 2400f,
    * shaded 770.8/f (>=10), QUALIFY16 183.9/f (>=5), scene: gameplay
    * (DAMAGE CRITICAL).
    * CRC: I-War (World) from src/core/filedb.c line 82. */
   {
      0x97EB4651, "I-War",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },

   /* Kasumi Ninja (retail) — true color: census row "Kasumi Ninja" 2400f,
    * shaded 383.7/f (>=10), QUALIFY16 0/f (<5), scene: "3D gauntlet walk
    * (pre-fight)" — real engine rendering of a playable segment, not a
    * menu/attract loop, so it qualifies under the policy's scene rule.
    * CRC: Kasumi Ninja (World) from src/core/filedb.c line 32. */
   {
      0x0957A072, "Kasumi Ninja",
      {
         { "virtualjaguar_true_color", "enabled" },
         { NULL, NULL }
      }
   },

   /* Missile Command 3D (retail) — true color + 2x: census row
    * "Missile Command 3D" 2400f, shaded 1650/f (>=10), QUALIFY16
    * 1866.7/f (>=5), scene: gameplay (Original 3D mode).
    * CRC: Missile Command 3D (World) from src/core/filedb.c line 105. */
   {
      0xDA9C4162, "Missile Command 3D",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },

   /* Skyhammer (retail) — true color + 2x: census row "Skyhammer" 2400f,
    * shaded 73.5/f (>=10), QUALIFY16 70.5/f (>=5), scene: gameplay
    * (cockpit).
    * CRC: Skyhammer (World) from src/core/filedb.c line 51 (FF_ALPINE is
    * a dump-header-format flag, not a build-stage flag — this is the
    * sole, FF_VERIFIED, untagged retail row for the title). */
   {
      0x4471BFA0, "Skyhammer",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { NULL, NULL }
      }
   },

   /* Tempest 2000 (retail) — true color: census row "Tempest 2000" 2400f,
    * shaded 720.8/f (>=10), QUALIFY16 3.17/f (<5), scene: gameplay
    * (web).
    * CRC: Tempest 2000 (World) from src/core/filedb.c line 68. */
   {
      0x6B2B95AD, "Tempest 2000",
      {
         { "virtualjaguar_true_color", "enabled" },
         { NULL, NULL }
      }
   },

   /* Towers II (retail) — 2x: census row "Towers II" 7200f, shaded 0/f
    * (<10), QUALIFY16 32.6/f (>=5), scene: gameplay (dungeon).
    * CRC: Towers II (0x83A3FB5D, FF_ROM|FF_VERIFIED) from
    * src/core/filedb.c line 73. (Excluded: 0x3241AB6A "Towers II" is
    * FF_ALPINE with no FF_VERIFIED — not the plain retail row.) */
   {
      0x83A3FB5D, "Towers II",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { NULL, NULL }
      }
   }
};

static const int titledb_count =
   sizeof(titledb_table) / sizeof(titledb_table[0]);

/* Current loaded title match; NULL if no content is loaded or CRC doesn't match. */
static const TitleDBEntry *current = NULL;

/* Kept even on a miss so the caller can name the CRC it looked up. */
static uint32_t content_crc = 0;

/* Test-only hook override; see TitleDBSetHooksForTest in titledb.h. */
static const TitleDBHook *hooks_override = NULL;
static int hooks_override_count = 0;

/*
 * Internal: set the CRC directly.
 * Linear-scan the table to find a match.
 */
void TitleDBSetCRC(uint32_t crc)
{
   int i;

   content_crc = crc;
   current = NULL;
   for (i = 0; i < titledb_count; i++)
   {
      if (titledb_table[i].crc32 == crc)
      {
         current = &titledb_table[i];
         return;
      }
   }
}

/*
 * Return the CRC32 (header-normalized) of the currently loaded content,
 * as last set by TitleDBSetContent/TitleDBSetCRC; 0 when no content is loaded.
 */
uint32_t TitleDBContentCRC(void)
{
   return content_crc;
}

/*
 * Load content for CRC lookup.
 * If data==NULL || size==0, clear the cached match.
 * Else compute header-normalized CRC and call TitleDBSetCRC.
 * Note: This function is provided for integration into the full core.
 * Tests can bypass the header-detection by calling TitleDBSetCRC directly.
 */
void TitleDBSetContent(const uint8_t *data, size_t size)
{
   uint32_t hdr;
   uint32_t crc;

   if (data == NULL || size == 0)
   {
      TitleDBSetCRC(0);
      return;
   }

   /* DetectPrependedHeaderSize is defined in file.c; this will only
    * be called in the full core build where file.c is linked. */
   hdr = DetectPrependedHeaderSize((uint8_t *)data, (uint32_t)size);
   crc = (uint32_t)crc32_calcCheckSum((unsigned char *)data + hdr,
                                       (unsigned int)(size - hdr));
   TitleDBSetCRC(crc);
}

/*
 * Lookup: return the preset value for a key in the loaded content, or NULL.
 */
const char *TitleDBOverride(const char *key)
{
   int i;

   if (current == NULL || key == NULL)
      return NULL;

   for (i = 0; i < TITLEDB_MAX_PAIRS; i++)
   {
      if (current->pairs[i].key == NULL)
         return NULL;
      if (strcmp(current->pairs[i].key, key) == 0)
         return current->pairs[i].value;
   }

   return NULL;
}

/*
 * Return the title name of the loaded content match, or NULL.
 */
const char *TitleDBTitleName(void)
{
   if (current == NULL)
      return NULL;
   return current->name;
}

/*
 * Enhancement hooks for the loaded content, or NULL on a miss.
 * The test override, when installed, wins regardless of CRC match.
 */
const TitleDBHook *TitleDBHooks(int *count)
{
   if (hooks_override != NULL)
   {
      if (count != NULL)
         *count = hooks_override_count;
      return hooks_override;
   }

   if (current == NULL)
      return NULL;

   if (count != NULL)
      *count = TITLEDB_MAX_HOOKS;
   return current->hooks;
}

/*
 * Test-only: install a hook array TitleDBHooks() returns regardless of
 * CRC match.  NULL restores normal table lookup.
 */
void TitleDBSetHooksForTest(const TitleDBHook *hooks, int count)
{
   if (hooks == NULL || count <= 0)
   {
      hooks_override = NULL;
      hooks_override_count = 0;
      return;
   }
   hooks_override = hooks;
   hooks_override_count = count;
}

/*
 * Test-only introspection: the raw table.
 */
const TitleDBEntry *TitleDBTable(int *count)
{
   if (count != NULL)
      *count = titledb_count;
   return titledb_table;
}
