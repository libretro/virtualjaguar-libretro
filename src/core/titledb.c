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
 * Issue #551: the shaded-blit-count proxy for virtualjaguar_true_color
 * does not predict a visible difference. Pixel-diffed A/B pairs (same
 * ROM, same scripted input, same frame, only the option toggled) found
 * three rows that clear the >=10 shaded-blits/frame threshold by wide
 * margins and still produce 0.0000% changed pixels: Alien vs Predator
 * (61-frame sweep, menu through moving gameplay), the Doom engine family
 * (retail + every JagDoomEX romhack alias, which by policy share the
 * retail row's pairs because they share the same renderer), and Missile
 * Command 3D. true_color was dropped from those rows below; Cybermorph
 * keeps it — it is the one row with a committed A/B screenshot actually
 * showing the banding fix (39.04% pixels changed). The remaining
 * true_color rows are unaudited against pixel evidence and are not
 * claimed correct or incorrect by this pass — see #551 for the
 * re-qualification this doesn't attempt.
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
 *
 * negative[] authoring checklist (issue #464)
 *
 * A negative row ("this setting is known-bad for this title") is
 * admissible only when ALL of the following hold.  test_titledb enforces
 * the mechanical half; the rest is review, exactly like hooks[] above.
 *
 *  1. The regression was REPRODUCED and CONFIRMED, not hypothesized.  A
 *     bug report that names a suspect option is not evidence by itself --
 *     see issue #463 (Cybermorph, RISC overclock): a full headless
 *     investigation left it at "the deciding experiment has not been
 *     run" and the issue stays `blocked` pending reporter artifacts. That
 *     is the bar this checklist enforces: a plausible mechanism is not a
 *     confirmed one, and a negative row is a stronger claim than a
 *     positive one, because it changes what the user is prevented from
 *     doing.
 *  2. The evidence isolates the SPECIFIC value from every other
 *     explanation (emulator regression, other timing models, input
 *     class) the same way #463's own investigation did before it
 *     stalled -- byte-identical framebuffers vs a known-good baseline,
 *     the suspect option demonstrably live (changes something), every
 *     other candidate ruled out.
 *  3. The comment cites the evidence the way every positive row does: a
 *     doc, a committed measurement, or an issue with the reproduction
 *     already closed out -- never "reported by a user" alone.
 *  4. `TITLEDB_NEG_ANY_NONDEFAULT` ("*") is for a genuinely monotonic
 *     class of unsafe values (e.g. "any overclock breaks this title's
 *     frame-coupled physics"), not a shortcut to avoid enumerating which
 *     specific values were actually tested.  If only some non-default
 *     values were confirmed bad, list them individually.
 *  5. No key may appear in both pairs[] and negative[] of the same row --
 *     a row that both applies X and calls X unsafe is a table bug.
 *
 * The table currently ships ZERO negative rows.  The mechanism is the
 * deliverable for issue #464; rows land once a title clears the bar
 * above.  See docs/enhancement-hooks.md for the settings-vs-patches
 * discriminator and this section's evidence bar restated for authors.
 * ------------------------------------------------------------------
 *
 * virtualjaguar_risc_idle_skip qualification policy (issue #707,
 * docs/perf-audit/idle-skip-corpus-2026-08.md):
 *
 *  - A row earns { "virtualjaguar_risc_idle_skip", "enabled" } only when
 *    BOTH held in the 2026-08 corpus A/B sweep (test/tools/dsp_idle_ab +
 *    test/tools/gpu_idle_ab, 1800 measured frames + 300 warmup, savestate
 *    hash every 100 frames, off-vs-on CSV hash streams cmp byte-identical
 *    for BOTH harnesses):
 *      (a) A/B byte-identical (framebuffer + audio + savestate hashes);
 *      (b) >10% interpreted-opcode reduction on either RISC processor.
 *  - Rows are keyed ONLY by CRCs whose exact image was swept.  This is
 *    deliberately stricter than the census policy above (which transfers
 *    one measurement to every plain retail CRC of the title): the idle
 *    skip's A/B claim is about the concrete instruction stream, so an
 *    unswept revision earns nothing until it is swept.  Sole exception:
 *    the Doom EX alias rows inherit retail Doom's pair per the #409
 *    alias policy (settings inherit safely across a romhack), backed by
 *    a direct sweep of one EX build (CRC 0xEE7B84EB, -76.5% DSP,
 *    byte-identical) -- the same engine claim the aliases already rest on.
 *  - FF_BAD_DUMP / alpha / beta / proto images are excluded from rows
 *    even when they swept clean, same exclusion the census policy uses.
 *  - Titles that swept byte-identical but measured no win (fires=0: the
 *    engine's wait loops don't fit the #569 admission rules -- Cybermorph,
 *    Battle Sphere, NBA Jam TE, Syndicate, Theme Park, Ultra Vortek,
 *    Skyhammer corpus dump, ...) get NO pair: the option would burn probe
 *    work for nothing.  Full per-title numbers in the sweep doc.
 *  - SUPERSEDED by #608 (v3.6.0): the global default is now "enabled",
 *    after the 148-image corpus sweep. These rows are therefore no-ops
 *    today -- a per-title default only substitutes when the option sits
 *    at its registered default, and that value is now the same one the
 *    rows carry. They are kept, not deleted, because they record WHICH
 *    titles were actually measured (the fires=0 titles still have no
 *    row), and they become load-bearing again the moment the global
 *    default moves back. Do not read them as the reason idle-skip is on.
 * ------------------------------------------------------------------
 */
static const TitleDBEntry titledb_table[] = {
   /* Alien vs Predator (retail) — 2x internal resolution.
    * Evidence: docs/avp-renderer-analysis.md (Stage 2 A/B, frame 6000),
    * docs/hires-stage0-census.md (census GO), shipped in v3.2.0.
    * true_color dropped per #551: pixel-diffed across a 61-frame sweep
    * (menu through moving gameplay), 0.0000% pixels changed with the
    * option toggled -- the shaded-blit-count heuristic that qualified it
    * (113/f) does not predict a visible difference here.
    * idle_skip per #707 sweep: byte-identical, DSP -73.8%, GPU -45.2%.
    * CRCs: Alien vs Predator (World) from src/core/filedb.c line 106. */
   {
      0xDC187F82, "Alien vs Predator",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         /* Deliberately NOT tagged with virtualjaguar_blit_memo -- see
          * docs/blit-memo.md.  AvP is the memo's best-evidenced title
          * (710,433 verify checks, 0 divergences, bit-identical A/B
          * over 8,000 frames) and the full 64-title corpus sweep is
          * clean too, so this is not a soundness gap. It is declined
          * on measured benefit: the memo only ever skips idle-window
          * redundant blits, which are themselves ~15% of idle wall
          * time at 2x, so the memo buys roughly -15% of that slice --
          * about 2% of idle time, unmeasurable in RetroArch (issue
          * #411). Tagging would turn it on by default for every AvP
          * user for a gain that small, adding a 14.6MB entry pool +
          * 9.6MB shadow arena (too much for iOS, open item 3) and an
          * untested interaction with run-ahead/rollback (open item 6).
          * Revisit only if the benefit is remeasured larger, or the
          * memory/determinism costs come down. */
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
    * idle_skip per #707 sweep: byte-identical, DSP -88.3%.
    * CRC: Air Cars (World) from src/core/filedb.c line 50.
    * (Excluded: 0xCBFD822A "Air Cars (World) (alt)" is FF_BAD_DUMP.) */
   {
      0x40E1A1D0, "Air Cars",
      {
         { "virtualjaguar_true_color",     "enabled" },
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Doom (retail) — 2x: census row "Doom" 2400f, shaded 445.8/f (>=10),
    * QUALIFY16 433.3/f (>=5), scene: gameplay (E1M1).
    * true_color dropped per #551: pixel-diffed as "Doom (World) EX",
    * 0.0000% pixels changed with the option toggled -- the shaded-blit
    * heuristic clears by a wide margin and still predicts nothing
    * visible here.
    * idle_skip per #707 sweep: byte-identical, DSP -79.5%.
    * CRC: Doom (World) from src/core/filedb.c line 62. */
   {
      0x5E2CDBC0, "Doom",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
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
    * will hash differently from every CRC listed here anyway.
    *
    * idle_skip on every alias row per #707: inherited from retail Doom
    * (byte-identical, DSP -79.5%) under the settings-inherit-safely
    * alias policy above, corroborated by a direct sweep of a Doom EX
    * build (CRC 0xEE7B84EB, not itself an alias row: byte-identical,
    * DSP -76.5%). */
   {
      0x754096DB, "Doom EX (JagDoomEX)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x4643E9DB, "Doom EX (JagDoomEX 2)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x35743B9C, "Doom EX (JagDoomEX 3)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xAD6B68BA, "Doom EX (JagDoomEX 4)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xC4F4CACF, "Doom EX (JagDoomEX 5)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x1F4EE4A5, "Doom EX (JagDoomEX 6)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0x013A5359, "Doom EX (spectral)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xB92D1CA3, "Doom EX (transparent)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },
   {
      0xEA12E234, "Doom EX (JagDoom2EX)",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },

   /* Hover Strike (retail) — 2x: census row "Hover Strike (cart)" 6000f,
    * shaded 1.1/f (<10), QUALIFY16 336.7/f (>=5), scene: gameplay
    * (mission approach, ALERT).
    * idle_skip per #707 sweep: byte-identical, DSP -71.8%.
    * CRC: Hover Strike (World) from src/core/filedb.c line 53. */
   {
      0x4899628F, "Hover Strike",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },

   /* I-War (retail) — true color + 2x: census row "I-War" 2400f,
    * shaded 770.8/f (>=10), QUALIFY16 183.9/f (>=5), scene: gameplay
    * (DAMAGE CRITICAL).
    * idle_skip per #707 sweep: byte-identical, DSP -86.7%.
    * CRC: I-War (World) from src/core/filedb.c line 82. */
   {
      0x97EB4651, "I-War",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_true_color",          "enabled" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
         { NULL, NULL }
      }
   },

   /* Kasumi Ninja (retail) — true color: census row "Kasumi Ninja" 2400f,
    * shaded 383.7/f (>=10), QUALIFY16 0/f (<5), scene: "3D gauntlet walk
    * (pre-fight)" — real engine rendering of a playable segment, not a
    * menu/attract loop, so it qualifies under the policy's scene rule.
    * idle_skip per #707 sweep: byte-identical, DSP -77.7%.
    * CRC: Kasumi Ninja (World) from src/core/filedb.c line 32. */
   {
      0x0957A072, "Kasumi Ninja",
      {
         { "virtualjaguar_true_color",     "enabled" },
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Missile Command 3D (retail) — 2x: census row "Missile Command 3D"
    * 2400f, shaded 1650/f (>=10), QUALIFY16 1866.7/f (>=5), scene:
    * gameplay (Original 3D mode).
    * true_color dropped per #551: pixel-diffed, 0.0000% pixels changed
    * with the option toggled -- the shaded-blit heuristic clears by a
    * wide margin and still predicts nothing visible here.
    * idle_skip per #707 sweep: byte-identical, DSP -63.1%, GPU -80.2%
    * (the docs/perf-audit/mc3d-stall-attribution.md semaphore poll).
    * CRC: Missile Command 3D (World) from src/core/filedb.c line 105. */
   {
      0xDA9C4162, "Missile Command 3D",
      {
         { "virtualjaguar_internal_resolution", "2x" },
         { "virtualjaguar_risc_idle_skip",      "enabled" },
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
    * idle_skip per #707 sweep: byte-identical, DSP -87.2%.
    * CRC: Tempest 2000 (World) from src/core/filedb.c line 68. */
   {
      0x6B2B95AD, "Tempest 2000",
      {
         { "virtualjaguar_true_color",     "enabled" },
         { "virtualjaguar_risc_idle_skip", "enabled" },
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
   },

   /* ------------------------------------------------------------------
    * RISC idle-loop fast-forward rows (issue #707).
    *
    * Every row below carries exactly { virtualjaguar_risc_idle_skip,
    * "enabled" } and nothing else, earned under the qualification policy
    * in the header comment: the exact image (CRC listed) swept
    * byte-identical off-vs-on by BOTH test/tools/dsp_idle_ab and
    * test/tools/gpu_idle_ab (1800 measured frames + 300 warmup, savestate
    * hash every 100 frames) AND measured >10% interpreted-opcode
    * reduction on at least one RISC processor.  Percentages below are
    * interpreted-opcodes-per-frame off->on from the harness TRAILERs;
    * the full sweep table (including titles that passed but measured no
    * win, and the excluded bad dumps / variants) is
    * docs/perf-audit/idle-skip-corpus-2026-08.md.  All CRCs are the
    * plain retail rows in src/core/filedb.c.
    * ------------------------------------------------------------------ */

   /* Atari Karts — DSP -72.2%, GPU -23.2%. */
   {
      0xE28756DE, "Atari Karts",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Attack of the Mutant Penguins — GPU -90.6% (DSP loops don't
    * qualify: 0 fires). */
   {
      0xCD5BF827, "Attack of the Mutant Penguins",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Brutal Sports Football — DSP -77.8%.  (0x0FDCEB66 FF_BAD_DUMP row
    * excluded per policy.) */
   {
      0xBCB1A4BF, "Brutal Sports Football",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Bubsy in Fractured Furry Tales — DSP -86.6%. */
   {
      0x2E17D5DA, "Bubsy in Fractured Furry Tales",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Cannon Fodder — DSP -81.3%. */
   {
      0xBDA405C6, "Cannon Fodder",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Checkered Flag — DSP -74.8%. */
   {
      0xFA7775AE, "Checkered Flag",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Club Drive — DSP -95.5%, the largest DSP win in the sweep. */
   {
      0xEEE8D61D, "Club Drive",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Defender 2000 — DSP -87.1%. */
   {
      0x27594C6A, "Defender 2000",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Double Dragon V - The Shadow Falls — DSP -77.0%. */
   {
      0x348E6449, "Double Dragon V",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Dragon - The Bruce Lee Story — DSP -78.3%. */
   {
      0x8FEA5AB0, "Dragon - The Bruce Lee Story",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Evolution - Dino Dudes — DSP -86.7%. */
   {
      0x0EC5369D, "Evolution - Dino Dudes",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Fever Pitch Soccer — GPU -89.1% (DSP: 0 fires). */
   {
      0x3615AF6A, "Fever Pitch Soccer",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Fight for Life — DSP -69.5%.  (0xC6C7BA62 FF_BAD_DUMP row excluded
    * per policy, though it swept identically.) */
   {
      0xB14C4753, "Fight for Life",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Flashback - The Quest for Identity — DSP -71.0%. */
   {
      0xDE55DCC7, "Flashback",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Flip Out! — DSP -82.7%. */
   {
      0x892BC67C, "Flip Out!",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Iron Soldier (v1.04) — DSP -66.2%, the #569 design title.  The
    * corpus "[a1]" variant dump (CRC 0xF7F3462B, not a filedb row) also
    * swept clean at -67.2% but earns no row per policy. */
   {
      0x08F15576, "Iron Soldier",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Iron Soldier 2 — GPU -92.9% (DSP: 0 fires). */
   {
      0xD6C19E34, "Iron Soldier 2",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Pinball Fantasies — DSP -84.7%. */
   {
      0x5CFF14AB, "Pinball Fantasies",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Pitfall - The Mayan Adventure — DSP -86.6%, GPU -91.9%. */
   {
      0x817A2273, "Pitfall - The Mayan Adventure",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Power Drive Rally — DSP -72.7%, GPU -92.5%. */
   {
      0x1660F070, "Power Drive Rally",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Raiden — DSP -86.9%.  The swept image is filedb's "Raiden (World)
    * (alt)" row (FF_ROM|FF_REQ_BIOS; retail, not a bad dump).  The
    * FF_VERIFIED dump 0x31812799 was not in the corpus, so per policy it
    * earns nothing until swept. */
   {
      0x0509C85E, "Raiden",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Rayman — DSP -71.0%. */
   {
      0xA9F8A00E, "Rayman",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Ruiner Pinball — DSP -78.6%. */
   {
      0x5B6BB205, "Ruiner Pinball",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Sensible Soccer - International Edition — DSP -64.2%. */
   {
      0x5A101212, "Sensible Soccer - International Edition",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Super Burnout — GPU -33.1% (DSP: 0 fires). */
   {
      0x6F8B2547, "Super Burnout",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* SuperCross 3D — DSP -46.1%.  (0x4A08A2BD FF_BAD_DUMP row excluded
    * per policy, though it swept identically.) */
   {
      0xEC22F572, "SuperCross 3D",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Trevor McFur in the Crescent Galaxy — DSP -70.8%.  Only the swept
    * CRC; the "(alt)" dump 0x95143668 was not in the corpus. */
   {
      0x1E451446, "Trevor McFur in the Crescent Galaxy",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Troy Aikman NFL Football — DSP -78.1%. */
   {
      0x38A130ED, "Troy Aikman NFL Football",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Wolfenstein 3D — DSP -83.8%. */
   {
      0xE91BD644, "Wolfenstein 3D",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Zool 2 — DSP -87.0%. */
   {
      0x8975F48B, "Zool 2",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
         { NULL, NULL }
      }
   },

   /* Zoop! — DSP -72.9%, GPU -93.9%. */
   {
      0xC5562581, "Zoop!",
      {
         { "virtualjaguar_risc_idle_skip", "enabled" },
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

/* Test-only negative-pair override; see TitleDBSetNegativeForTest below. */
static const TitleDBNegativePair *negative_override = NULL;
static int negative_override_count = 0;

/* Test-only positive-pair override; see TitleDBSetPairsForTest below. */
static const TitleDBPair *pairs_override = NULL;
static int pairs_override_count = 0;

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
 * The test override, when installed, REPLACES the table lookup entirely
 * (it wins regardless of CRC match and a miss in it does not fall back to
 * the row), same as hooks_override/negative_override -- a fixture that
 * half-consulted the shipped table would be exactly as fragile as the
 * shipped-table dependency it exists to remove (issue #590).
 */
const char *TitleDBOverride(const char *key)
{
   int i;

   if (key == NULL)
      return NULL;

   if (pairs_override != NULL)
   {
      for (i = 0; i < pairs_override_count; i++)
      {
         if (pairs_override[i].key == NULL)
            break;
         if (strcmp(pairs_override[i].key, key) == 0)
            return pairs_override[i].value;
      }
      return NULL;
   }

   if (current == NULL)
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
 * Negative-entry lookup (issue #464): does the loaded content's row mark
 * key=value (or key="any non-default value") as known-bad?  Pure string
 * comparison only -- no option-system calls -- so this file keeps linking
 * standalone in test/test_titledb (titledb.c + crc32.c, no core).
 * The test override, when installed, wins regardless of CRC match, same
 * as TitleDBHooks()'s hooks_override.
 */
int TitleDBUnsafeValue(const char *key, const char *value,
                        const char *option_default)
{
   const TitleDBNegativePair *neg;
   int count;
   int i;

   if (key == NULL || value == NULL)
      return 0;

   if (negative_override != NULL)
   {
      neg = negative_override;
      count = negative_override_count;
   }
   else if (current != NULL)
   {
      neg = current->negative;
      count = TITLEDB_MAX_NEGATIVE_PAIRS;
   }
   else
      return 0;

   for (i = 0; i < count && neg[i].key != NULL; i++)
   {
      if (strcmp(neg[i].key, key) != 0)
         continue;
      if (strcmp(neg[i].value, TITLEDB_NEG_ANY_NONDEFAULT) == 0)
      {
         if (option_default == NULL || strcmp(value, option_default) != 0)
            return 1;
      }
      else if (strcmp(neg[i].value, value) == 0)
         return 1;
   }

   return 0;
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
 * Test-only: install a negative-pair array TitleDBUnsafeValue() consults
 * regardless of CRC match.  NULL restores normal table lookup.
 */
void TitleDBSetNegativeForTest(const TitleDBNegativePair *pairs, int count)
{
   if (pairs == NULL || count <= 0)
   {
      negative_override = NULL;
      negative_override_count = 0;
      return;
   }
   negative_override = pairs;
   negative_override_count = count;
}

/*
 * Test-only: install a positive-pair array TitleDBOverride() answers from
 * regardless of CRC match.  NULL restores normal table lookup.
 */
void TitleDBSetPairsForTest(const TitleDBPair *pairs, int count)
{
   if (pairs == NULL || count <= 0)
   {
      pairs_override = NULL;
      pairs_override_count = 0;
      return;
   }
   pairs_override = pairs;
   pairs_override_count = count;
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
