# Hi-res upscaling Stage 0 — corpus-wide blit / OP census (R1 + R2)

**Date:** 2026-08-07
**Epic:** #338, track 1 ("internal resolution upscaling")
**Design:** `docs/hires-upscaling-design.md` — this document executes **Stage 0**
(§8) and resolves risks **R1** (is there a second beneficiary?) and **R2**
(do IS-class titles rasterize with GPU stores?).
**Status:** measurement report. **No product code.** The instrumentation was a
throwaway compile-time patch and is not part of this commit.

---

## 0. Executive summary

**R1 is resolved: the design's "Doom is the only qualifying title" premise was
an artifact of the small first census, not of the hardware or the library.**
With gameplay-reaching input across the corpus, **eight commercial titles**
consume fractional source walks onto 16bpp CRY destinations at material rates —
Doom, Missile Command 3D, Hover Strike (cart), Iron Soldier 2 (CD), Alien vs
Predator, I-War, Skyhammer, and Towers II — plus one homebrew (CRZ Demo).
The no-build condition in R1's decision rule is **not met**.

**R2 is resolved: no measured title rasterizes its framebuffer with GPU
stores.** Iron Soldier's famous "388 blits in 900 frames" was measured on a
*menu*; in actual gameplay IS1 issues ~378 blits/frame (Z-buffered `PATDSEL`
span fills through the blitter). GPU stores into OP-scanned framebuffer
regions are 7–3,700 bytes/frame across the R2 set — HUD-scale, not
rasterization-scale — while the blitter writes 39–220 KB/frame into the same
regions. **No GPU-store shadow hook is needed.**

The OP scaler census (Stage 3 evidence) additionally shows heavy non-integer
`HSCALE`/`VSCALE` use in Primal Rage (its entire fighters), Val d'Isere (its
entire renderer), Towers II, Atari Karts, Super Cross 3D, and Wolfenstein 3D.

**Recommendation: GO** — see §6.

---

## 1. Method

- **Instrumentation:** a throwaway counter block at the single blit-launch site
  (`BlitterWriteWord`, `offset & 0xFF == 0x3A` in `src/tom/blitter_mmio.c` —
  engine-independent: it fires before dispatch to either blitter), in the OP
  bitmap processors (`OPProcessFixedBitmap` / `OPProcessScaledBitmap`), and in
  the `JaguarWrite{Byte,Word,Long}` funnels (`who == GPU` / `who == BLITTER`,
  main-RAM addresses only). Aggregates exposed via a dlsym-able struct read by
  a small harness runner after each run. The patch was reverted before this
  commit; only this document is committed.
- **Per-blit record:** `B_CMD` flags (`GOURD`, `SRCSHADE`, Z bits, `DSTA2`,
  `SRCEN`, `PATDSEL`, `UPDA1F`), destination pixel depth from the destination
  walker's `A1_FLAGS`/`A2_FLAGS` (per `DSTA2`), OUTER count, phrase vs pixel
  mode, and fractional-source-walk consumption.
- **Fractional source walk** (the anti-inert-register definition from design
  §2): counted only when A1 is the **source** (`DSTA2` set — A2 has no
  fractional machinery), and either the inner walk consumes a fraction
  (`XADDCTL = add-increment` **and** `A1_FINC` X-fraction ≠ 0) or the outer
  walk does (`UPDA1F` set **and** `A1_FSTEP` X-fraction ≠ 0). A stale fraction
  behind a phrase-mode `XADDCTL` does **not** count — the Cybermorph trap.
- **Qualifying** = fractional source walk onto a 16bpp destination (the shape
  Stage 2 as designed can reach). **Frac→CLUT** = same walk onto a 1/2/4/8bpp
  destination (reachable only with a CLUT-destination extension).
- **R2 classification:** main RAM is divided into 256 × 8 KB buckets. A bucket
  is "framebuffer" if the OP fetched bitmap pixel data from it during the run
  (object-list fetches are not marked). GPU-store and blitter-write bytes are
  then split framebuffer vs non-framebuffer. Cumulative over the run, so a
  menu-phase buffer stays marked — this over-approximates "framebuffer", which
  is the conservative direction for R2 (it can only overstate, never hide,
  GPU-store rasterization).
- **Runs:** instrumented build of `libretro/develop` @ `28120cd` (wide test
  ABI), headless via the shared harness. 2,400 frames per cart title (more
  where menu navigation needed it, up to 12,000 for CD titles), scripted
  `--press` input, per-title final-frame screenshots to verify the reached
  scene. Identical runs are deterministic, so one run per title.
- **Gameplay verification:** every row marked "gameplay" below had its
  final-frame screenshot eyeballed (Doom in E1M1, IS1 in mission 1 cockpit,
  IS2-CD in a city mission, AvP in a corridor as the Alien, Primal Rage
  mid-fight, etc.). Rows marked "menu/attract" are lower bounds, not
  negatives.

## 2. Blit census — cartridge corpus

Columns: total blits; shaded = `GOURD`+`SRCSHADE`; Z = any Z compare/write;
dst16 / dstCLUT = blits by destination depth; frac src = fractional source
walks (inner+outer); **QUALIFY16** = frac src onto 16bpp (Stage 2 reach);
frac→CLUT = frac src onto CLUT dest (CLUT-extension reach).

| Title | Frames | Blits | shaded | Z | dst16 | dstCLUT | frac src | QUALIFY16 | frac→CLUT | Scene reached |
|---|---|---|---|---|---|---|---|---|---|---|
| Aircars | 2400 | 228.4k (95/f) | 114.7k | 115.0k | 123.6k | 25.2k | 16 | **8** | 0 | gameplay (terrain) |
| Alien vs Predator | 4800 | 640.0k (133/f) | 544.3k | 0 | 615.2k | 19.3k | 405.8k | **405.8k** | 0 | gameplay (Alien, corridors) |
| Atari Karts | 2400 | 159.7k (67/f) | 0 | 131.0k | 1 | 159.7k | 131.0k | **0** | 131.0k | gameplay (race, 7th) |
| Attack of the Mutant Penguins | 2400 | 1.9k (1/f) | 0 | 30 | 1.2k | 607 | 16 | **0** | 16 | attract/menu |
| Brutal Sports Football | 2400 | 1.3k (1/f) | 1.1k | 71 | 1.3k | 1 | 0 | **0** | 0 | attract |
| Bubsy | 2400 | 919 (0/f) | 0 | 0 | 917 | 2 | 0 | **0** | 0 | attract/menu |
| Cannon Fodder | 2400 | 2.2k (1/f) | 0 | 0 | 2 | 2.2k | 1.4k | **0** | 1.4k | menu |
| Checkered Flag | 2400 | 1.67M (697/f) | 0 | 3.5k | 1.58M | 14 | 16 | **8** | 0 | gameplay (race) |
| Club Drive | 2400 | 1.74M (724/f) | 0 | 4.0k | 1.70M | 38.7k | 626 | **314** | 0 | gameplay (driving) |
| Cybermorph (1993) | 2400 | 1.74M (723/f) | 1.70M | 1.70M | 1.72M | 19.1k | 26 | **13** | 0 | gameplay (pods remaining HUD) |
| Defender 2000 | 2400 | 16.0k (7/f) | 696 | 2.9k | 6.9k | 8.6k | 1.0k | **516** | 0 | attract/gameplay mix |
| Doom | 2400 | 1.07M (445/f) | 1.07M | 0 | 1.07M | 2.4k | 1.04M | **1.04M** | 0 | gameplay (E1M1) |
| Double Dragon V | 2400 | 1.6k (1/f) | 0 | 0 | 0 | 1.6k | 0 | **0** | 0 | menu |
| Dragon | 2400 | 209 (0/f) | 0 | 0 | 209 | 0 | 0 | **0** | 0 | menu |
| Evolution Dino Dudes | 2400 | 261 (0/f) | 0 | 0 | 0 | 261 | 0 | **0** | 0 | menu |
| Fever Pitch Soccer | 2400 | 0 (0/f) | 0 | 0 | 0 | 0 | 0 | **0** | 0 | did not render |
| Fight For Your Life | 2400 | 577.6k (241/f) | 0 | 0 | 577.5k | 0 | 0 | **0** | 0 | menu (BEGIN GAME; heaviest per-frame blit volume in corpus) |
| Flashback | 2400 | 1.0k (0/f) | 0 | 0 | 495 | 553 | 0 | **0** | 0 | menu |
| Flip Out | 2400 | 4 (0/f) | 0 | 0 | 1 | 0 | 0 | **0** | 0 | menu |
| Hover Strike (cart) | 6000 | 4.02M (670/f) | 6.8k | 2.12M | 4.02M | 835 | 2.02M | **2.02M** | 0 | gameplay (mission approach, ALERT) |
| I-War | 2400 | 2.58M (1076/f) | 1.85M | 2.56M | 2.58M | 0 | 441.4k | **441.4k** | 0 | gameplay (DAMAGE CRITICAL) |
| Iron Soldier (v1.04, state+nav) | 2400 | 907.3k (378/f) | 3.1k | 868.1k | 883.7k | 21.2k | 15.6k | **11.6k** | 0 | gameplay (mission 1, verified) |
| Iron Soldier 2 (cart) | 7200 | 329.2k (46/f) | 0 | 0 | 5.1k | 324.1k | 4.3k | **4.3k** | 0 | menus only (unit select; could not exit headlessly) |
| Kasumi Ninja | 2400 | 1.07M (446/f) | 920.9k | 0 | 1.07M | 3.9k | 0 | **0** | 0 | 3D gauntlet walk (pre-fight) |
| Missile Command 3D | 2400 | 5.73M (2386/f) | 3.96M | 1.27M | 5.73M | 0 | 4.48M | **4.48M** | 0 | gameplay (Original 3D mode) |
| NBA Jam TE | 2400 | 1.4k (1/f) | 384 | 560 | 949 | 467 | 240 | **240** | 0 | attract/menu |
| Pinball Fantasies | 2400 | 0 (0/f) | 0 | 0 | 0 | 0 | 0 | **0** | 0 | did not render |
| Pitfall | 2400 | 20.5k (9/f) | 0 | 0 | 0 | 20.5k | 0 | **0** | 0 | attract |
| Power Drive Rally | 2400 | 33.8k (14/f) | 198 | 0 | 9.3k | 22.9k | 0 | **0** | 0 | attract/menu |
| Raiden | 2400 | 0 (0/f) | 0 | 0 | 0 | 0 | 0 | **0** | 0 | attract |
| Rayman | 2400 | 8.8k (4/f) | 0 | 0 | 2.2k | 0 | 0 | **0** | 0 | menu |
| Ruiner Pinball | 2400 | 9 (0/f) | 0 | 0 | 0 | 7 | 0 | **0** | 0 | menu |
| Skyhammer | 2400 | 197.8k (82/f) | 176.4k | 0 | 195.8k | 110 | 169.3k | **169.3k** | 0 | gameplay (cockpit) |
| Super Burnout | 2400 | 119.3k (50/f) | 0 | 0 | 0 | 141 | 0 | **0** | 0 | attract/gameplay |
| Super Cross 3D | 2400 | 273.5k (114/f) | 0 | 0 | 2.4k | 271.0k | 173.6k | **0** | 173.6k | gameplay (race) |
| Syndicate | 6000 | 1.98M (330/f) | 0 | 1.74M | 7 | 1.98M | 0 | **0** | 0 | mission brief (mouse-driven UI; gameplay unreachable headlessly) |
| Tempest 2000 | 2400 | 1.75M (727/f) | 1.73M | 1.57M | 1.75M | 0 | 8.3k | **7.6k** | 0 | gameplay (web) |
| Theme Park | 2400 | 43.4k (18/f) | 0 | 19 | 7 | 43.4k | 0 | **0** | 0 | menu |
| Towers II | 7200 | 748.7k (104/f) | 0 | 0 | 735.6k | 0 | 234.4k | **234.4k** | 0 | gameplay (dungeon) |
| Trevor McFur | 2400 | 73.6k (31/f) | 0 | 0 | 20.6k | 51.1k | 0 | **0** | 0 | attract |
| Troy Aikman NFL | 2400 | 510 (0/f) | 0 | 0 | 0 | 508 | 0 | **0** | 0 | menu |
| Ultra Vortek | 2400 | 2.5k (1/f) | 2 | 0 | 1.5k | 1.0k | 0 | **0** | 0 | attract |
| Val d'Isere | 2400 | 1.5k (1/f) | 0 | 0 | 1 | 0 | 0 | **0** | 0 | gameplay (snowboarding) |
| White Men Can't Jump | 2400 | 956 (0/f) | 768 | 1 | 770 | 181 | 0 | **0** | 0 | attract/menu |
| Wolfenstein 3D | 2400 | 222.4k (93/f) | 0 | 107 | 1.7k | 220.6k | 215.3k | **0** | 215.3k | gameplay (in level) |
| Zool 2 | 4800 | 1.7k (0/f) | 0 | 1.7k | 61 | 1.7k | 0 | **0** | 0 | stage intro |
| Zoop | 2400 | 34.8k (14/f) | 0 | 0 | 0 | 34.8k | 0 | **0** | 0 | attract |

PD/homebrew demos with zero-to-negligible blit activity are omitted from the
table: 40+ demo/utility ROMs measured 0–5 blits in 2,400 frames (JagMania,
Jaguar Server examples, BadCode series, Mandelbrot, Memory Dump, Gorf 2000,
Phase Zero, JagFest, sound demos…). Two exceptions worth naming: **CRZ Demo**
(1.96M blits, 1.90M gouraud, 57.8k qualifying onto 16bpp — a real, if small,
homebrew beneficiary) and **DEMO1B/C + Ladybug + Asteroid + Native Demo +
Jaguar Tetris**, which render via OP scaled objects (§4).

## 3. Blit census — CD titles

All CD runs used the default HLE boot path with the shared harness
(`--system-dir` at the private ROM tree).

| Title | Frames | Blits | shaded | Z | dst16 | dstCLUT | frac src | QUALIFY16 | frac→CLUT | Scene reached |
|---|---|---|---|---|---|---|---|---|---|---|
| Battle Morph (CD) | 12000 | 21.3k (2/f) | 13.0k | 2.4k | 21.3k | 2 | 1.5k | **738** | 0 | menus + weapon select (see note) |
| Iron Soldier 2 (CD) | 12000 | 7.00M (583/f) | 13.4k | 3.16M | 6.51M | 343.6k | 3.25M | **3.22M** | 0 | gameplay (city mission) |
| Hover Strike: Unconquered Lands (CD) | 12000 | 252.0k (21/f) | 1 | 117 | 251.5k | 489 | 233 | **117** | 0 | mission-select cockpit (see note) |
| Primal Rage (CD) | 9000 | 669 (0/f) | 0 | 0 | 0 | 669 | 0 | **0** | 0 | gameplay (fight, Sauron vs Diablo) |

Notes:

- **Iron Soldier 2 (CD) is the second-heaviest qualifying title by absolute
  count (3.22M) and fourth by rate (268/frame)**: 583 blits/frame in-mission,
  46% of them fractional source walks onto a 16bpp destination (textured,
  Z-buffered spans). The cart release never got past its unit-select menu
  headlessly; the CD row is the authoritative one.
- **Battle Morph** could not be walked into a flight mission headlessly (name
  pad → weapon bays → LAUNCH needs pixel-precise cursor work); the measured
  segment (menus, briefing, loadout — 738 qualifying blits from briefing
  scenes) is a lower bound and the row is marked accordingly. Its engine
  lineage (Cybermorph sequel) predicts gouraud-span flight rendering — i.e.
  the Cybermorph non-beneficiary result — but only a flight-mission run can
  confirm that; its R2 numbers are still valid (§5).
- **Hover Strike UL (CD)** parked on its mission-select cockpit; the **cart**
  Hover Strike run did reach mission flight and is the representative row
  (2.02M qualifying — the engine is a heavy fractional texture walker).
- **Primal Rage** renders its fighters entirely through the OP scaler — 0.1
  blits/frame mid-fight, 505k non-integer scaled-object calls (§4).

## 4. OP scaler census (Stage 3 evidence)

Per object-scanline call of `OPProcessScaledBitmap` with `render` and a
non-zero `HSCALE`. "non-int" = `HSCALE` **or** `VSCALE` fraction bits ≠ 0
(3.5 fixed point, `$20` = 1.0×). The "h<1.0" / "h>1.0" buckets are
**HSCALE-only** — Towers II shows 0 in both because its HSCALE is exactly
1.0 while its non-integer scaling is entirely in VSCALE. They split the
histogram into
minification (source detail exists beyond what 1x keeps — **real recovery**)
and magnification (only step-placement smoothing).

| Title (scene) | scaled calls | non-int | h<1.0 | h>1.0 | top HSCALE values |
|---|---|---|---|---|---|
| Towers II (dungeon) | 6.10M | 6.10M | 0 | 0 | $20:6.10M |
| Val d'Isere (snowboarding) | 809.6k | 520.9k | 386.9k | 134.1k | $20:288.6k, $10:167.0k, $21:67.9k, $0D:44.0k |
| Primal Rage (fight) | 504.9k | 504.9k | 504.9k | 0 | $1C:504.4k, $18:160, $14:136, $10:108 |
| Jaguar Tetris (PD) | 477.1k | 0 | 0 | 477.1k | $40:477.1k |
| Super Cross 3D (race) | 442.8k | 442.8k | 0 | 442.8k | $24:307.8k, $22:135.0k |
| Defender 2000 | 361.6k | 239.1k | 54.3k | 306.8k | $40:122.8k, $3C:85.2k, $21:76.2k, $1E:1.9k |
| Atari Karts (race) | 318.3k | 264.9k | 166.8k | 100.7k | $1F:51.5k, $20:50.8k, $08:24.7k, $07:19.1k |
| Native Demo (PD) | 306.2k | 254.7k | 86.3k | 168.9k | $20:51.0k, $32:31.5k, $19:27.8k, $26:19.0k |
| Missile Command 3D | 0 | 0 | 0 | 0 |  |
| NBA Jam TE | 262.7k | 1.7k | 1.7k | 0 | $20:260.9k, $1E:217, $1C:203, $1A:188 |
| Wolfenstein 3D | 127.6k | 127.6k | 43.0k | 84.5k | $24:65.0k, $1A:18.5k, $1E:11.6k, $25:4.3k |
| Aircars (beta) | 65.8k | 65.8k | 65.8k | 0 | $10:65.8k |
| IS2 (CD, mission) | 33.8k | 23.7k | 23.7k | 0 | $20:10.1k, $1F:9.7k, $1E:9.4k, $1D:304 |
| Asteroid (PD) | 102.7k | 70.8k | 70.8k | 31.9k | $1E:61.7k, $40:31.9k, $1D:606, $1C:585 |
| International Sensible Soccer | 249.8k | 73.0k | 18.6k | 55.8k | $20:175.4k, $1C:1.8k, $1E:1.5k, $1D:1.3k |
| Iron Soldier (mission) | 7.3k | 4.8k | 4.8k | 0 | $20:2.5k, $1F:2.4k, $1E:2.3k |
| Zool 2 | 51.5k | 51.5k | 0 | 0 | $20:51.5k |

Readings:

- **Primal Rage:** every fighter sprite is drawn at `$1C` (0.875×)
  minification — the archetypal Stage 3 beneficiary, and it is a 2D title the
  blitter path can never help.
- **Val d'Isere:** the whole game is OP-scaled objects (809k calls, 521k
  non-integer, dominant minification) with essentially zero blitting — the
  "inconclusive" row in the design's first census is resolved: it is a Stage 3
  title, not a Stage 2 one.
- **Towers II:** all 6.1M calls at `HSCALE $20` / `VSCALE $22` — a constant
  1.0625× vertical magnification (aspect correction). Smoothing-only.
- **Atari Karts:** deep minification (`$02`–`$08` = 1/16×–1/4× for distant
  karts) — real recovery, but its blit destinations are CLUT (§2), so the OP
  CLUT resolve limitation (§6.5 of the design) applies to its sprites too.
- **Wolfenstein 3D** scales its weapon/pickup sprites non-integer (`$24`,
  `$1A`…) in addition to its CLUT wall columns.
- Fixed (unscaled) bitmap objects dominate every other title; scaled-object
  use is concentrated exactly where the table shows it.

## 5. R2 — GPU-store probe

Bytes/frame written by each engine into OP-fetched ("framebuffer") buckets vs
elsewhere in main RAM. "GPU st events" = total GPU store instructions landing
in main RAM (all STORE variants, via the write funnels).

| Title (scene) | Frames | GPU→FB B/f | GPU→other B/f | Blitter→FB B/f | GPU st events |
|---|---|---|---|---|---|
| Iron Soldier 1 (mission 1, from state) | 2400 | 11 | 5730 | 74849 | 3.52M |
| Iron Soldier 2 (CD, city mission) | 12000 | 3688 | 9427 | 51214 | 40.24M |
| Battle Morph (CD, menus+loadout) | 12000 | 308 | 149 | 59061 | 1.37M |
| Cybermorph (gameplay) | 2400 | 7 | 8366 | 220765 | 10.33M |
| Doom (gameplay, contrast) | 2400 | 66 | 80818 | 39095 | 49.38M |
| Checkered Flag (gameplay, contrast) | 2400 | 530 | 7185 | 86844 | 5.53M |

Conclusions:

1. **No measured title rasterizes via GPU stores.** The largest GPU→FB rate in
   the R2 set (IS2-CD, ~3.7 KB/frame) is an order of magnitude below its own
   blitter's ~51 KB/frame into the same regions, and is consistent with HUD /
   radar element updates, not polygon filling. Iron Soldier 1 in gameplay:
   **11 bytes/frame** of GPU stores into framebuffer buckets against 74.8
   KB/frame of blitter writes.
2. **The design's IS anomaly is explained.** The prior save state was the main
   menu (verified by screenshot). In-mission, IS1 issues ~378 blits/frame:
   Z-buffered `PATDSEL` flat spans onto a 16bpp destination with **integer**
   walks — i.e. IS1 is a Cybermorph-class NN title (11.6k fractional blits in
   2,400 frames is marginal), not an invisible-renderer. The 24bpp objects
   noted in `src/tom/op.c` appear only on its menu/briefing screens; gameplay
   scans 16bpp objects.
3. **The shadow's two touch points (blitter stores + OP resolve) see every
   pixel producer that matters.** No GPU-store hook is required; the
   "structurally untouched write paths" guarantee of PR #341 carries to Nx
   unweakened. Caveat: GPU→other traffic (e.g. Doom's 80.8 KB/frame) includes
   game state and any GPU-written intermediate buffers; if such a buffer later
   reaches the screen through a blit copy, the shadow sees the copy and
   degrades that content to NN by tag mismatch — safe by construction, just
   not supersampled. That is the design's intended failure mode, not a gap.

## 6. Aggregate answers and recommendation

**A1 — Titles qualifying for Stage 2 as designed (fractional source walk onto
16bpp CRY, gameplay-verified; the lowest row here is 24 blits/frame):**

> ⚠ **This table counts blit *shapes*, not supersampled pixels on screen.**
> Of the nine rows below, four are verified on-screen beneficiaries, three
> measured **zero**, one could not be verified, and one (CRZ Demo,
> homebrew) was not measured. Do not quote this table as a beneficiary
> list — see **§9, "Stage 2 on-screen verification"**, which measures what
> actually reaches the display.

| Title | qualifying blits/frame |
|---|---|
| Missile Command 3D | 1,865 |
| Doom | 434 |
| Hover Strike (cart, and by engine identity the CD release) | 336 |
| Iron Soldier 2 (CD) | 268 |
| I-War | 184 |
| Alien vs Predator | 85 |
| Skyhammer | 71 |
| Towers II | 33 |
| CRZ Demo (homebrew) | 24 |

Marginal but non-zero: Tempest 2000 (3.2/f — its webs are gouraud; the
fractional walks are its 3D interstitials), Iron Soldier 1 (4.8/f).
**Answer: 8 commercial qualifying titles, not 1.**

**A2 — Additional titles reachable only with a CLUT-destination extension:**

| Title | frac→CLUT blits/frame |
|---|---|
| Wolfenstein 3D | 90 |
| Super Cross 3D | 72 |
| Atari Karts | 55 |

**Answer: a CLUT extension adds 3 titles.** Worthwhile follow-on, no longer
the make-or-break scope decision the design feared.

**A3 — R2 / IS-class:** blitter-rendered everywhere measured; no GPU-store
hook needed (§5).

### Recommendation — against R1's decision rule

R1's rule: *"if Stage 0 finds no second beneficiary, do not build Stage 1;
either extend scope to CLUT destinations first, or close the track."* Stage 0
found **eight** commercial second beneficiaries for Stage 2 as already
specified, several of them heavier consumers of sub-pixel source information
than Doom itself (Missile Command 3D at 4.3×, Hover Strike and IS2-CD
comparable), and none of them requires the CLUT extension or a GPU-store hook.
The rule's no-build condition is therefore not met: **GO — build Stage 1 (N=2
plumbing) and Stage 2 exactly as designed.** The CLUT-destination extension is
demoted from "possible prerequisite for a second beneficiary" to a follow-on
stage that would add Wolfenstein 3D, Super Cross 3D and Atari Karts; Stage 3
(OP scaled-object supersampling) is independently justified by Primal Rage,
Val d'Isere and Atari Karts, and is the only stage that reaches 2D titles.
Design §10's cost argument stands unchanged: Cybermorph, Checkered Flag, Club
Drive, Kasumi Ninja, Tempest 2000 and Iron Soldier 1 pay the full N² cost for
effectively nothing, so track 4's per-title enhancement DB remains a
**shipping prerequisite** for default-on behaviour.

## 7. Not measured, and why

- **Homebrew Doom-engine mods were not in the corpus** — no such ROMs exist
  under `test/roms/private/ROMS/`. Given stock Doom's numbers they are
  presumed beneficiaries, but that is presumption, not measurement.
- **24 PD/homebrew files failed to load headlessly** (raw `bin`/`rom`/BJL
  variants of demos whose `.jag` counterparts did run: BadCode raw dumps,
  Drumpad, Hubble, JDC demos, JSS demos, Painter bin, PAULA previews, PlaySFX,
  Native bin, Chroma-Luma bin, one JagMania and one JagMarble build, AvP
  Alpha). Their `.jag` siblings are in the data where they exist.
- **BattleSphere Gold** ships only as a `.zip` in the tree — not measured.
- **Menu-locked commercial titles** (marked in §2): Rayman, Flashback, Double
  Dragon V, Dragon, Theme Park, Troy Aikman, Ruiner Pinball, Fever Pitch,
  Pinball Fantasies, Fight For Your Life, Syndicate (mouse-driven UI), Zool 2
  (stage card), Kasumi Ninja (pre-fight gauntlet), Iron Soldier 2 cart, Battle
  Morph flight missions, Hover Strike UL (CD) missions. Their rows are lower
  bounds. None of these is plausibly a hidden Stage 2 beneficiary of Doom's
  class given what their engines showed in the measured scenes, but the rows
  should not be quoted as negatives.
- **Val d'Isere / DEMO1B / DEMO1C**: measured and active — via the OP scaler,
  not the blitter (§4), consistent with the A/B-sweep tooling notes.

## 8. Reproduction

The instrumentation pattern (for re-running or extending):

1. Counter struct + record function at `BlitterWriteWord`, firing when
   `(offset & 0xFF) == 0x3A` (the B_CMD low-word write that launches a blit)
   (before engine dispatch) reading `blitter_ram` directly.
2. `OPProcessFixedBitmap` / `OPProcessScaledBitmap` hooks after decode
   (`render` only): depth histogram, `HSCALE`/`VSCALE` histograms, 8 KB
   fetch-bucket marking from the object's data pointer.
3. `JaguarWriteByte/Word/Long` main-RAM branches: bytes by `who` (GPU /
   BLITTER) into 8 KB buckets.
4. Frame counter in `retro_run`; struct exported through the wide test ABI and
   read via `harness_dlsym` after `harness_run()`; per-title final-frame
   screenshot via the harness video callback for scene verification.

Budget observed: ~5 ms/frame instrumented; the whole corpus (~115 titles ×
2,400 frames) measures in under an hour on one host.

---

## 9. Stage 2 on-screen verification (added 2026-08-09)

**Date:** 2026-08-09 · **Build:** `feature/338-hires-stage2` @ `836baf6`
(Stage 2 + the ADDDSEL review fix), clean tree, `VJ_EXPECT_BUILD` guard
enabled on every run. **Tool:** `test/tools/hires_shot`.

### 9.1 Why this section exists

§2/§3/§6 counted **qualifying blit shapes**: a fractional A1 source walk
onto a 16bpp CRY destination. That is the *shape* Stage 2 can reach — it
is not the same thing as *supersampled pixels arriving on the display*.
Three further conditions have to hold, and each one is a real filter:

- **(a) pixel-mode destination writes.** The accurate engine's phrase-mode
  `dwrite` path keeps Stage 1 box replication (a documented Stage 2
  deviation). *Measured: not a filter for any title here* — see §9.4.
- **(b) a source-dependent data path.** `GOURD` and `PATDSEL` produce
  output that does not depend on the source sample, so re-sampling the
  source cannot change anything; both engines exclude them.
- **(c) the supersampled content must survive to the displayed
  framebuffer.** If a title renders into an off-screen buffer and then
  plain-copies that buffer to the display, the copy is an integer 1:1
  walk: it replicates, and the sub-pixel content is discarded.

And one condition the census could not see at all:

- **(d) the source walk must MINIFY.** Stage 2 derives its extra samples
  by re-reading the source at `pos + inc/2`. When the walk consumes less
  than one source texel per destination pixel (magnification), the
  half-step lands inside the *same texel* and returns the identical
  value. A fractional walk is necessary but not sufficient — only
  minification carries information the 1x sample threw away.

And one that is not a property of the title at all:

- **(e) the OP resolve has to accept the stored blocks.** Production (the
  blitter storing supersampled blocks) and delivery (the OP resolving them
  into the Nx line buffer) are **separate failure points, and only delivery
  fails silently.** Every block can be stored bit-identically and every one
  of them rejected at the value+epoch check in `shadow_hires_block()`
  (`src/tom/shadowfb.c`) — 0.0000% on screen, no log line, nothing else
  wrong. The **epoch** half is the silent one: a title whose engine takes
  more than `HIRES_EPOCH_WINDOW` presented frames per rendered view has
  *every* block rejected for age, not merely some.

**So when a title measures `0.0000` here, check the OP resolve hit rate
BEFORE investigating blit shapes, the Stage 2 gate, or the census
predicate.** That check is a permanent counter now, not throwaway
instrumentation: run with `virtualjaguar_crash_detect=verbose` and the
watchdog heartbeat prints `hires_resolve … (epoch=… value=… nopage=…)
rate=… window_rate=…` every 600 frames while hi-res is active. Read
`window_rate` (last 600 frames), not the cumulative `rate`, which menus and
boot dilute; the first line of a run reads `window_rate=n/a (first window)`
because it is seeding that baseline, not reporting one. Healthy AvP gameplay
reads 97–98%, and an `epoch`-dominated
bucket with `window_rate` near zero is the signature. This failure mode has
bitten **two** titles — Doom (fixed by `404cb11`, window 2 → 16) and Alien
vs Predator (same constant; A/B in `docs/avp-renderer-analysis.md` §6 shows
OP resolve hits 42,827,520 → 0 with blitter-side production bit-identical).
Bucket-by-bucket triage is in `docs/hires-upscaling-design.md` §8, Stage 2.
Do not widen `HIRES_EPOCH_WINDOW` reflexively: 16 is measured to saturate
the benefit.

### 9.2 The metric

`hires_shot` reports, for each dumped 2x frame, the **percentage of 2×2
output blocks whose four subpixels are not all equal**. This is a
*coverage* number — the fraction of the screen carrying real sub-pixel
detail. It is **not** "N% better image". Stage 1 replication and every
non-beneficiary give exactly `0.0000`; a nonzero value is exactly where
Stage 2 fired and survived. (The number is only meaningful at 2x; run at
1x it just measures ordinary neighbouring-pixel variance.)

### 9.3 Measured results

Every "scene" below was screenshot-verified (PPM → PNG → eyeballed) at the
frames measured — **not** inferred from a frame count. The census's own
IS1 mistake (a "gameplay" row measured on a menu) is the reason.

> **Reading the verdicts.** "Zero on the shipped engine" means 0.00 % on
> **Accurate**, which is the default. The Fast engine's sub-1 % figures on
> those rows come from the predicate-parity gap in §9.8 item 2 — it
> supersamples `SRCSHADE`+`GOURD` and `ADDDSEL` blits that Accurate
> rejects — and are not a user-visible benefit.

| Title | Scene (screenshot-verified) | Frames | Accurate (shipped default) | Fast | Verdict |
|---|---|---|---|---|---|
| **Alien vs Predator (1994)** | Alien, textured corridor, SCORE HUD | 5200 / 6000 | **30.63 / 30.62 %** | 30.63 / 30.62 % | **verified** |
| **Skyhammer** | in-mission cockpit, city flight, weapons armed | 2000 / 2600 / 2900 | **10.01 / 17.74 / 3.40 %** | 10.01 / 17.74 / 3.40 % | **verified** |
| **Skyhammer** | attract flight (same renderer + HUD) | 1200 / 1800 / 2400 | 22.68 / 18.05 / 3.67 % | 22.68 / 18.05 / 3.67 % | **verified** |
| **Doom** | E1M1 gameplay | 900 | **8.68 %** | 8.68 % | **verified** |
| **Missile Command 3D** | Original 3D gameplay | 1600 / 2000 / 2400 | **5.03 / 5.88 / 5.16 %** | 5.03 / 5.88 / 5.16 % | **verified** |
| **Hover Strike (cart)** | mission cockpit, terrain + ALERT HUD | 2400 / 3200 / 4000 | **0.00 / 0.00 / 0.00 %** | 0.55 / 0.00 / 0.32 % | **zero on the shipped engine** (§9.5) |
| **I-War** | gameplay, "DAMAGE CRITICAL" | 2800 / 3200 / 3600 | **0.00 / 0.00 / 0.00 %** | 0.39 / 0.26 / 0.41 % | **zero on the shipped engine** (§9.6) |
| **Towers II** | first-person dungeon | Acc: 12000 / 14800 · Fast: 12000 / 13500 / 14800 | **0.00 / 0.00 %** | 0.00 / 0.00 / 0.00 % | **zero** |
| **Iron Soldier 2 (CD)** | gameplay not reachable headlessly | — | — | — | **unverified** (§9.7) |
| **CRZ Demo (homebrew)** | — | — | — | — | **not measured** |
| Checkered Flag / Cybermorph | gameplay (non-beneficiary controls) | — | 0.0000 % | 0.0000 % | zero, as designed |

Notes on the table:

- **Engine parity.** The harness leaves `vjs.useFastBlitter` at its init
  value unless `--option virtualjaguar_usefastblitter=disabled` is passed,
  so an unqualified harness run measures the **Fast** blitter while the
  shipped core default is **Accurate**. Every row above was therefore run
  on both. Doom, Skyhammer and AvP are bit-identical between engines. The
  Fast column's small nonzero on Hover Strike and I-War comes from the
  Fast engine's slightly wider Stage 2 predicate (`SRCSHADE || (!GOURD &&
  !PATDSEL)`) versus the Accurate engine's (`!patdsel && !gourd &&
  !adddsel`) — an engine-parity gap worth ~0.3–0.5 % of screen, noted in
  §9.8.
- **Skyhammer** is measured twice because its attract sequence drives the
  full gameplay renderer and HUD; the in-mission row (reached with scripted
  input, screenshot-verified in a mission cockpit) is the authoritative
  one and both are strongly positive.
- **CRZ Demo** (the homebrew row in §6's A1 table) was not measured here.
  §2 records that 1.90 M of its 1.96 M blits are gouraud, and both engines
  exclude `GOURD` — so reason (b) predicts near-zero — but that is a
  prediction, not a measurement, and the row is left open rather than
  asserted.

### 9.4 Correction to a previously reported AvP result

An earlier hand measurement circulated as "**AvP: 0.0000 % on both blitter
engines**". **That is wrong.** On a clean `836baf6` tree with the
build-identity guard enabled, AvP measures **30.63 %** — the *largest*
on-screen benefit of any title measured, larger than Doom's. The scene
(Alien, textured corridor, SCORE HUD, claw in frame) was screenshot-
verified at both sampled frames on both engines. Instrumentation confirms
why: 21.2 M of 22.2 M candidate shadow stores are accepted by the Stage 2
predicate, 9.8 M of those blocks carry differing sub-samples, and 27.8 M
block lookups at render time return non-uniform blocks. The earlier zero
**could not be reproduced, and its cause was not established** — treat it
as retracted rather than explained.

**Pre-empting the obvious objection:** AvP's variance is near-constant
across the sampled window (23,964 / 23,960 / 23,960 non-uniform blocks at
frames 5200 / 5600 / 6000) and the run logs four `video_stall` lines, so
it is fair to ask whether this is a frozen buffer rather than live
rendering. It is not, and the shadow's own design proves it: the hi-res
tag carries a frame epoch and an entry is trusted only if written within
the last **16 presented frames** (`HIRES_EPOCH_WINDOW`, `shadowfb.c`). A
framebuffer that stopped being rewritten would have every block fall out
of the window and the measurement would decay to `0.0000`. A sustained
30.6 % across 800 frames is only possible if the blitter is re-rendering
those pixels continuously. The `video_stall` lines are the documented
benign class — `crash_detect` hashes ~256 pixels per frame, and a
visually near-static scene trips it.

### 9.5 Hover Strike — detail is produced, then discarded downstream

Hover Strike is **not** blocked by the Stage 2 predicate at all. Over 2,500
frames of verified mission gameplay:

| counter | value |
|---|---|
| candidate 16bpp shadow stores | 10,410,361 |
| accepted by the Stage 2 predicate | 10,376,870 (99.7 %) |
| accepted blocks carrying real sub-pixel detail | 9,056,918 (87 %) |
| rejected: no fractional walk | 33,491 |
| rejected: everything else (inhibit, no SRCEN, BCOMPEN, source depth, data path) | **0** |

Yet the display shows ~0 %. The destination-address histogram explains it
(64 KB buckets of main RAM; "render" = shadow-block lookups performed by
the Nx renderer for displayed pixels):

| bucket | Stage 2 stores | render lookups |
|---|---|---|
| `$000000`–`$040000` | 1,169,274 | 96.7 M |
| `$050000`–`$0B0000` | 0 | 61.8 M |
| **`$100000`** | **9,207,596 (89 %)** | **0** |

89 % of the supersampled pixels land at `$100000`, a region the display
**never reads**. Hover Strike renders its textured 3D view into an
off-screen buffer and then copies that buffer to the displayed
framebuffer; the copy is an integer 1:1 walk, so it replicates and the
sub-pixel content is dropped. This is condition (c) above, and it is the
one cheaply-fixable case found — see §9.8.

### 9.6 I-War — the detail barely exists to begin with

I-War is the opposite failure. Its Stage 2 stores and its display lookups
share the same address buckets (`$0D0000`–`$160000`), so nothing is being
discarded downstream. The content simply is not there:

| counter | value |
|---|---|
| accepted by the Stage 2 predicate | 2,652,798 |
| accepted blocks carrying real sub-pixel detail | 541,847 (20 %) |
| qualifying walks with **< 1 source texel per destination pixel** | 2,151,841 (81 %) |
| non-uniform blocks reaching the renderer | 604,757 |

81 % of its fractional walks are **magnifying** — the `pos + inc/2`
sub-sample lands inside the same source texel and returns the identical
value, so the block is flat by construction. And the little detail that
does exist *does* reach the screen (`604,757` non-uniform render hits ≈
`541,847` detailed blocks). **This is information-theoretic, not a bug and
not fixable within Stage 2**: there is no extra source information to
recover. Towers II is the same class (its OP census in §4 already showed a
constant `HSCALE $20` with only a 1.0625× vertical magnification —
smoothing, not minification).

**The rule to carry forward: Stage 2 recovers information only where the
source walk minifies.** A fractional walk is necessary but not sufficient.
Doom (44 % of its 4,936,831 qualifying walks at ≥ 1 texel/pixel) and AvP
benefit; I-War and Towers II cannot.

### 9.7 Iron Soldier 2 (CD) — not verified

Gameplay could not be reached headlessly in this session. The disc boots,
plays its intro, and shows its `START GAME / LOAD GAME / OPTIONS` menu
around frames 4,900–7,700 (screenshot-verified), but no button the shared
harness can script (`a b c pause option up down 0–6`, including 30-frame
holds and a per-button sweep) ever selects a menu entry — presses only
shorten the menu's timeout and advance the attract story/credits loop. Its
attract 3D city fly-through (same renderer) measured `0.0000 %` at frames
8800/10400/12000, but **that is an attract scene, not gameplay, and is not
counted as a result either way.** IS2-CD's on-screen benefit is
**unknown**, not zero. Re-running this needs a gameplay savestate.

### 9.8 Follow-on candidates (evidence only — not implemented)

1. **Shadow-aware copy propagation** (contained; would recover Hover
   Strike, and by engine identity Hover Strike: Unconquered Lands). Today a
   blit whose source region is itself shadow-tracked still writes Stage 1
   replicated blocks at the destination. Propagating the source's N×N
   sub-blocks through such a copy instead would carry the 9.2 M
   supersampled pixels at `$100000` into the displayed buffer. Evidence:
   the bucket table in §9.5. Cost is a source-side shadow lookup on
   qualifying copies; the risk to guard is tag/epoch coherence on the
   source side.
2. **Engine predicate parity** (small). The Fast engine supersamples
   `SRCSHADE`-with-`GOURD` and `ADDDSEL` blits that the Accurate engine
   rejects. Worth ~0.3–0.5 % of screen on Hover Strike / I-War — i.e.
   cosmetic — but the two engines should not disagree about what Stage 2
   covers.
3. **Not** phrase-mode support. The Stage 2 design flags the accurate
   engine's phrase-mode `dwrite` deviation as a known gap; it was
   instrumented here and is **not** the blocker for any measured title —
   Doom's accurate-engine run recorded `5,071,224` pixel-mode 16bpp
   destination writes and **zero** phrase-mode ones, and Hover Strike's
   rejection buckets are empty. Do not spend effort there on the strength
   of this census.

### 9.9 Reproduction

```bash
cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
   -o test/tools/hires_shot test/tools/hires_shot.c \
   test/harness/harness.c -ldl -lm

VJ_EXPECT_BUILD=$(bash scripts/build-id.sh) ./test/tools/hires_shot \
  ./virtualjaguar_libretro.dylib "<rom-or-cue>" \
  --option virtualjaguar_internal_resolution=2x \
  [--option virtualjaguar_usefastblitter=disabled] \
  --frames N --press F:BTN ... --shot F --out-prefix /tmp/shot --quiet
```

Scripted input used per title (all screenshot-verified):

| Title | `--press` sequence | shots |
|---|---|---|
| Doom | `300:a 500:a 700:a` | 900 |
| Missile Command 3D | `300:a 600:a 900:a 1200:a` | 1600 2000 2400 |
| Alien vs Predator | `3300:a 3600:a 3900:a 4200:a 4500:a 5000:a` | 5200 6000 |
| Skyhammer (mission) | `400:a 700:a 1000:a 1300:a` | 2000 2600 2900 |
| Skyhammer (attract) | *(none)* | 1200 1800 2400 |
| Hover Strike | `200:a 400:a 700:a 1000:a 1300:a 1600:a 1900:a 2200:a` | 2400 3200 4000 |
| I-War | `300:a 1000:b 1400:a 1800:a 2200:a 2600:a` | 2800 3200 3600 |
| Towers II | `7300:a 7700:a 8100:a 8500:a 8900:a 9300:a 10100:b 10500:b 10900:b 11300:b` | 12000 13500 14800 |

Convert and inspect with `sips -s format png <prefix>_fNNNNN.ppm --out x.png`.

The §9.5/§9.6 counter tables came from a throwaway instrumentation patch
(rejection buckets at both engines' Stage 2 sites in `src/tom/blitter.c`,
plus hit/miss/non-uniform and address buckets in
`ShadowHiresLineFromRAM` in `src/tom/shadowfb.c`, exported through
`exports-test.list` and read via `harness_dlsym`). It was reverted before
this commit; only this document is committed. Same pattern as §8.
