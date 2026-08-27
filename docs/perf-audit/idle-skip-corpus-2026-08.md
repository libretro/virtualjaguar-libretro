# RISC idle-skip corpus A/B sweep — 2026-08 (issue #707)

Corpus-wide off-vs-on determinism + win measurement for
`virtualjaguar_risc_idle_skip` (#569 DSP + #698/#699 GPU port), feeding the
per-title titledb rows shipped with this doc.  Shorthand, LLM-oriented;
companion to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md) P1 and
[`mc3d-stall-attribution.md`](mc3d-stall-attribution.md).

## Setup / provenance

- Worktree at `libretro/develop` **f9f6fec** (clean; binaries snapshotted before
  any table edit, `VJ_EXPECT_BUILD=f9f6fec` enforced on every run), macOS arm64
  (M-series), `make TEST_EXPORTS=1`.
- Harnesses: committed `test/tools/dsp_idle_ab` + `test/tools/gpu_idle_ab`, both
  arms per harness per title (4 runs/title):
  `<harness> ./virtualjaguar_libretro.dylib "<rom>" --frames 2100 --warmup 300
  --state-every 100 --csv <arm>.csv --system-dir test/roms/private
  [--option virtualjaguar_risc_idle_skip=enabled]`
  → 1800 measured frames, savestate hash every 100 frames.  **A/B verdict =
  `cmp` of the two arms' per-frame framebuffer/audio/savestate hash CSVs, for
  BOTH harnesses.**  No scripted input (headless attract/boot path — same
  content class as the census).
- Comparator sensitivity confirmed before trusting clean results: a deliberate
  perturbation arm (`virtualjaguar_internal_resolution=2x`, yarc) diffs.
- Corpus: every unique cart image (md5-deduped, `[BIOS]` files excluded) under
  `find -L test/roms/private \( -iname '*.j64' -o -iname '*.jag' -o -iname
  '*.rom' \)` → **148 images**, sequential, ≤2 core instances at a time,
  240 s/run timeout (`gtimeout`); plus 6 CD spot-checks (900 s timeout).
- Win metric: harness TRAILER `interpreted_per_frame` off→on (interpreted =
  executed − extrapolated-over); `fires` = successful loop extrapolations.
  Counters are deterministic (load-insensitive).

## Verdict

- **148/148 cart images + 6/6 CD spot-checks byte-identical** off vs on —
  zero hash divergences anywhere in the corpus.  No determinism-bug leads.
- 71/148 cart images cleared the >10% interpreted-opcode bar on ≥1 RISC
  processor; 39 of them map to plain filedb rows and got titledb pairs
  (31 new rows + 8 pre-existing rows extended, + 9 Doom EX alias rows by
  the #409 inherit policy).
- The #659 Chroma-Luma unbounded-blit wedge did **not** reproduce at these
  settings (all 4 variants completed 4/4 runs inside the timeout).

## Row policy (mirrors the header comment in `src/core/titledb.c`)

Pair `{ virtualjaguar_risc_idle_skip, "enabled" }` iff (a) byte-identical A/B
in BOTH harnesses AND (b) >10% interpreted-opcode reduction on either
processor.  Rows keyed **only by directly-swept CRCs** — deliberately stricter
than the hires census policy (idle-skip's claim is about the concrete
instruction stream, so unswept revisions earn nothing).  Exceptions/exclusions:

- Doom EX alias rows inherit retail Doom's pair (settings-inherit alias
  policy, #409), corroborated by a direct sweep of one EX build (`EE7B84EB`,
  −76.5% DSP, byte-identical).
- `FF_BAD_DUMP` (Fight for Life alt `C6C7BA62`, SuperCross 3D `4A08A2BD`) and
  beta images (Aircars beta, Ultra Vortek beta) excluded even where they swept
  clean, same exclusion the census uses.
- PD/homebrew images without a filedb identity: measured below, no rows this
  pass (a filedb identity is the "known title" bar the table has always used;
  13 BadCode format-variants alone would double the table for one demo).
- CD titles can NOT get rows: `retro_load_game` skips `TitleDBSetContent` for
  CD content (titledb v1 is cart-only by design) — spot-checks below are
  determinism evidence only.
- Titles that swept clean but measured **no win** (fires=0 — wait loops don't
  fit the #569 admission rules): Cybermorph (both revs), Battle Sphere,
  NBA Jam TE, Syndicate, Theme Park, Ultra Vortek (retail + beta),
  Val d'Isere, White Men Can't Jump, Phase Zero, Skyhammer corpus dump
  (`3C044941` — NB: titledb's Skyhammer row is `4471BFA0`, a dump not present
  in this corpus).  No pair: the option would pay probe cost for nothing.

User-override semantics (unchanged, stated for the record): titledb pairs
apply **only when the frontend reports the option at its registered default**
(`get_variable_pertitle()` in `libretro.c`) — an explicit user setting always
wins.  Known limitation of that convention: a user who explicitly re-selects
the default value is indistinguishable from one who never touched the option,
so the per-title default applies in that case too.  `virtualjaguar_pertitle_defaults=disabled`
kills all of it.

## Results

### filedb-identified images (retail + known dumps)

| title | CRC32 | A/B | DSP interp/f off->on | GPU interp/f off->on | fires/f (DSP/GPU) | row |
| --- | --- | --- | --- | --- | --- | --- |
| Air Cars (World) | `40E1A1D0` | IDENTICAL | 441,430 -> 51,759 (**-88.3%**) | 9,928 (no change) | 1356 / 0 | **YES** |
| Alien vs Predator (World) | `DC187F82` | IDENTICAL | 530,504 -> 139,040 (**-73.8%**) | 56,241 -> 30,799 (**-45.2%**) | 1187 / 99 | **YES** |
| Atari Karts (World) | `E28756DE` | IDENTICAL | 438,258 -> 121,914 (**-72.2%**) | 442,757 -> 340,023 (**-23.2%**) | 1600 / 490 | **YES** |
| Attack of the Mutant Penguins (World) | `CD5BF827` | IDENTICAL | 522,683 (no change) | 442,794 -> 41,816 (**-90.6%**) | 0 / 2163 | **YES** |
| Battle Sphere (World) | `5F2C2774` | IDENTICAL | 418,949 (no change) | 442,699 (no change) | 0 / 0 | no |
| Brutal Sports Football (World) | `BCB1A4BF` | IDENTICAL | 578,478 -> 128,233 (**-77.8%**) | 16,894 (no change) | 1267 / 0 | **YES** |
| Bubsy in Fractured Furry Tales (World) | `2E17D5DA` | IDENTICAL | 615,886 -> 82,272 (**-86.6%**) | 442,685 (no change) | 1298 / 0 | **YES** |
| Cannon Fodder (World) | `BDA405C6` | IDENTICAL | 423,027 -> 78,941 (**-81.3%**) | 1,398 (no change) | 1307 / 0 | **YES** |
| Checkered Flag (World) | `FA7775AE` | IDENTICAL | 527,625 -> 133,073 (**-74.8%**) | 20,886 (no change) | 1186 / 0 | **YES** |
| Club Drive (World) | `EEE8D61D` | IDENTICAL | 590,272 -> 26,675 (**-95.5%**) | 0 (no activity) | 1638 / 0 | **YES** |
| Cybermorph (World) (Rev 1) | `BDE67498` | IDENTICAL | 511,529 (no change) | 97,525 (no change) | 0 / 0 | no |
| Cybermorph (World) (Rev 2) | `ECF854E7` | IDENTICAL | 511,635 (no change) | 107,590 (no change) | 0 / 0 | no |
| Defender 2000 (World) | `27594C6A` | IDENTICAL | 622,030 -> 80,322 (**-87.1%**) | 442,684 (no change) | 1311 / 0 | **YES** |
| Doom (World) | `5E2CDBC0` | IDENTICAL | 434,352 -> 88,983 (**-79.5%**) | 442,715 (no change) | 1364 / 0 | **YES** |
| Double Dragon V - The Shadow Falls (World) | `348E6449` | IDENTICAL | 546,520 -> 125,643 (**-77.0%**) | 0 (no activity) | 1223 / 0 | **YES** |
| Dragon - The Bruce Lee Story (World) | `8FEA5AB0` | IDENTICAL | 426,099 -> 92,598 (**-78.3%**) | 442,690 (no change) | 1281 / 0 | **YES** |
| Evolution - Dino Dudes (World) | `0EC5369D` | IDENTICAL | 614,445 -> 81,524 (**-86.7%**) | 442,685 (no change) | 1297 / 0 | **YES** |
| Fever Pitch Soccer (World) (En,Fr,De,Es,It) | `3615AF6A` | IDENTICAL | 439,539 (no change) | 442,529 -> 48,370 (**-89.1%**) | 0 / 1842 | **YES** |
| Fight for Life (World) | `B14C4753` | IDENTICAL | 512,929 -> 156,292 (**-69.5%**) | 391,399 (no change) | 1122 / 0 | **YES** |
| Fight for Life (World) (alt) *(bad dump)* | `C6C7BA62` | IDENTICAL | 512,929 -> 156,292 (**-69.5%**) | 391,399 (no change) | 1122 / 0 | no *(bad dump)* |
| Flashback - The Quest for Identity (World) (En,Fr) | `DE55DCC7` | IDENTICAL | 504,329 -> 146,250 (**-71.0%**) | 5,567 (no change) | 1115 / 0 | **YES** |
| Flip Out! (World) | `892BC67C` | IDENTICAL | 571,506 -> 98,894 (**-82.7%**) | 442,688 (no change) | 1254 / 0 | **YES** |
| Hover Strike (World) | `4899628F` | IDENTICAL | 517,868 -> 145,877 (**-71.8%**) | 86,781 (no change) | 1147 / 0 | **YES** |
| I-War (World) | `97EB4651` | IDENTICAL | 617,478 -> 82,102 (**-86.7%**) | 2,615 (no change) | 1300 / 0 | **YES** |
| Iron Soldier (World) (v1.04) | `08F15576` | IDENTICAL | 490,182 -> 165,558 (**-66.2%**) | 41 (no change) | 1063 / 0 | **YES** |
| Iron Soldier 2 (World) | `D6C19E34` | IDENTICAL | 512,070 (no change) | 228,623 -> 16,324 (**-92.9%**) | 0 / 827 | **YES** |
| Kasumi Ninja (World) | `0957A072` | IDENTICAL | 553,876 -> 123,443 (**-77.7%**) | 442,703 (no change) | 1235 / 0 | **YES** |
| Missile Command 3D (World) | `DA9C4162` | IDENTICAL | 482,813 -> 178,037 (**-63.1%**) | 442,703 -> 87,677 (**-80.2%**) | 1014 / 1372 | **YES** |
| NBA Jam T.E. (World) | `0AC83D77` | IDENTICAL | 438,235 (no change) | 444,485 (no change) | 0 / 0 | no |
| Phase Zero | `EA9B3FA7` | IDENTICAL | 488,806 (no change) | 0 (no activity) | 0 / 0 | no |
| Pinball Fantasies (World) | `5CFF14AB` | IDENTICAL | 622,367 -> 95,184 (**-84.7%**) | 20,032 (no change) | 1962 / 0 | **YES** |
| Pitfall - The Mayan Adventure (World) | `817A2273` | IDENTICAL | 615,589 -> 82,523 (**-86.6%**) | 442,696 -> 35,740 (**-91.9%**) | 1299 / 1489 | **YES** |
| Power Drive Rally (World) | `1660F070` | IDENTICAL | 523,103 -> 142,877 (**-72.7%**) | 441,099 -> 32,945 (**-92.5%**) | 1159 / 1577 | **YES** |
| Raiden (World) (alt) | `0509C85E` | IDENTICAL | 487,695 -> 64,081 (**-86.9%**) | 11,947 (no change) | 1029 / 0 | **YES** |
| Rayman (World) | `A9F8A00E` | IDENTICAL | 516,311 -> 149,596 (**-71.0%**) | 3,305 (no change) | 1138 / 0 | **YES** |
| Ruiner Pinball (World) | `5B6BB205` | IDENTICAL | 570,032 -> 122,215 (**-78.6%**) | 442,702 (no change) | 1248 / 0 | **YES** |
| Sensible Soccer - International Edition (World) | `5A101212` | IDENTICAL | 483,795 -> 173,133 (**-64.2%**) | 10,970 (no change) | 999 / 0 | **YES** |
| Super Burnout (World) | `6F8B2547` | IDENTICAL | 451,900 (no change) | 446,463 -> 298,835 (**-33.1%**) | 0 / 792 | **YES** |
| SuperCross 3D (World) | `EC22F572` | IDENTICAL | 430,622 -> 232,307 (**-46.1%**) | 120,874 (no change) | 787 / 0 | **YES** |
| SuperCross 3D (World) *(bad dump)* | `4A08A2BD` | IDENTICAL | 430,622 -> 232,307 (**-46.1%**) | 120,872 (no change) | 787 / 0 | no *(bad dump)* |
| Syndicate (World) | `58272540` | IDENTICAL | 640,609 (no change) | 17,988 (no change) | 0 / 0 | no |
| Tempest 2000 (World) | `6B2B95AD` | IDENTICAL | 623,189 -> 79,838 (**-87.2%**) | 40,481 (no change) | 1314 / 0 | **YES** |
| Theme Park (World) | `47EBC158` | IDENTICAL | 645,803 (no change) | 37,945 (no change) | 0 / 0 | no |
| Trevor McFur in the Crescent Galaxy (World) | `1E451446` | IDENTICAL | 501,414 -> 146,540 (**-70.8%**) | 528 (no change) | 1061 / 0 | **YES** |
| Troy Aikman NFL Football (World) | `38A130ED` | IDENTICAL | 558,711 -> 122,187 (**-78.1%**) | 40,085 (no change) | 1244 / 0 | **YES** |
| Ultra Vortek (World) | `0F6A1C2C` | IDENTICAL | 483,612 (no change) | 2,491 (no change) | 0 / 0 | no |
| Ultra Vortek (World) (v0.94) (Beta) | `A27823D8` | IDENTICAL | 482,715 (no change) | 5,104 (no change) | 0 / 0 | no |
| Val d'Isere Skiing and Snowboarding (World) | `C9608717` | IDENTICAL | 846,169 (no change) | 442,685 (no change) | 0 / 0 | no |
| White Men Can't Jump (World) | `14915F20` | IDENTICAL | 0 (no activity) | 5,720 (no change) | 0 / 0 | no |
| Wolfenstein 3D (World) | `E91BD644` | IDENTICAL | 440,474 -> 71,336 (**-83.8%**) | 72,548 (no change) | 1795 / 0 | **YES** |
| Zool 2 (World) | `8975F48B` | IDENTICAL | 621,785 -> 80,564 (**-87.0%**) | 442,678 (no change) | 1311 / 0 | **YES** |
| Zoop! (World) | `C5562581` | IDENTICAL | 521,919 -> 141,527 (**-72.9%**) | 442,688 -> 26,831 (**-93.9%**) | 1113 / 1527 | **YES** |

### corpus images with no filedb identity (PD / homebrew / variant dumps)

| title | CRC32 | A/B | DSP interp/f off->on | GPU interp/f off->on | fires/f (DSP/GPU) | row |
| --- | --- | --- | --- | --- | --- | --- |
| Aircars  USA   Beta   1994-11-14 | `53E35744` | IDENTICAL | 442,090 -> 57,058 (**-87.1%**) | 9,928 (no change) | 1342 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Alien vs Predator  Alpha | `4242DFA7` | IDENTICAL | 532,364 (no change) | 187,513 (no change) | 0 / 0 | no |
| Arkanna Demo  PD | `BC3C8272` | IDENTICAL | 652,899 -> 33,416 (**-94.9%**) | 0 (no activity) | 1431 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Assassin Demo  The - Release 2  PD | `4750BBFF` | IDENTICAL | 0 (no activity) | 442,644 (no change) | 0 / 0 | no |
| Assassin Demo  The Part 1  1999   PD | `0DB47415` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Assassin Demo  The Part 1  for BJL   1999   PD | `D5C01E77` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Asteroid  2000   PD | `82629A5B` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode0 by Badcoder  bin   1999   PD | `F11D319B` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode0 by Badcoder  cof   1999   PD | `466A92C8` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode1 by Badcoder  bin   1999   PD | `008222AC` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode1 by Badcoder  cof   1999   PD | `800D7C67` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode2 by Badcoder  bin   2000   PD | `D69D1F02` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode2 by Badcoder  cof   2000   PD | `2A0C9443` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode3 by Badcoder  bin   2000   PD | `AC95CA6D` | IDENTICAL | 442,646 -> 52,261 (**-88.2%**) | 0 (no activity) | 1286 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode3 by Badcoder  jag   2000   PD | `FFBB1FA3` | IDENTICAL | 442,646 -> 52,261 (**-88.2%**) | 0 (no activity) | 1286 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4  Metal  by Badcoder  bin   2000   PD | `BA97C724` | IDENTICAL | 442,660 -> 52,410 (**-88.2%**) | 0 (no activity) | 1286 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4  Metal  by Badcoder  bin   2000   PD   a1 | `8F1586A9` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode4  Metal  by Badcoder  jag   2000   PD | `7D74B6F0` | IDENTICAL | 442,660 -> 52,410 (**-88.2%**) | 0 (no activity) | 1286 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4  Metal  by Badcoder  rom   2000   PD | `DE51F0FE` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode4  Modified  by Badcoder  bin   2000   PD | `111D4E2C` | IDENTICAL | 442,312 -> 53,272 (**-88.0%**) | 0 (no activity) | 1282 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4  Modified  by Badcoder  jag   2000   PD | `97757FC2` | IDENTICAL | 442,312 -> 53,272 (**-88.0%**) | 0 (no activity) | 1282 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4  Modified  by Badcoder  rom   2000   PD | `5E6D06B9` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| BadCode4 by Badcoder  bin   2000   PD | `22178D94` | IDENTICAL | 442,564 -> 52,520 (**-88.1%**) | 0 (no activity) | 1285 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4 by Badcoder  jag   2000   PD | `4491EC09` | IDENTICAL | 442,564 -> 52,520 (**-88.1%**) | 0 (no activity) | 1285 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4C by Badcoder  bin   2000   PD | `75E3A725` | IDENTICAL | 442,645 -> 52,396 (**-88.2%**) | 0 (no activity) | 1286 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4C by Badcoder  jag   2000   PD | `F4454EFE` | IDENTICAL | 442,645 -> 52,396 (**-88.2%**) | 0 (no activity) | 1286 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4N by Badcoder  bin   2000   PD | `B5A901DF` | IDENTICAL | 442,312 -> 53,295 (**-88.0%**) | 0 (no activity) | 1282 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| BadCode4N by Badcoder  jag   2000   PD badcde4n | `159EC9D3` | IDENTICAL | 442,312 -> 53,295 (**-88.0%**) | 0 (no activity) | 1282 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Chroma-Luma Color Pick  Mouse  by Matthias Domin  1996   PD | `37DF04AB` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Chroma-Luma Color Pick by Matthias Domin  bin   1996   PD | `115254E1` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Chroma-Luma Color Pick by Matthias Domin  jag   1996   PD | `2E967B0C` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Chroma-Luma Color Pick by Matthias Domin  jag   1996   PD  a1 | `24ECB02B` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| CRZ Demo  PD | `A1B28439` | IDENTICAL | 0 (no activity) | 419,692 (no change) | 0 / 0 | no |
| DEMO1  bin   PD | `E89D0F72` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| DEMO1  PD | `9E925A24` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| DEMO1B  PD | `E1734368` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| DEMO1B  PD   a1 | `C7B6D7FC` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| DEMO1C  PD | `6ACC377B` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Doom  World  EX 2 | `EE7B84EB` | IDENTICAL | 434,353 -> 102,031 (**-76.5%**) | 442,715 (no change) | 1322 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Drumpad2 by Robert Jurziga  2002   PD | `BAF04511` | IDENTICAL | 590,356 -> 32,334 (**-94.5%**) | 0 (no activity) | 1976 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Drumpad by Robert Jurziga  2002   PD | `48537047` | IDENTICAL | 648,556 -> 57,838 (**-91.1%**) | 0 (no activity) | 1460 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Fight For Your Life  1996   a2 | `2ABADB3D` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| FORCE Design - Legion Force Jidai Intro Demo 0   2001   PD | `1A68194C` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Gorf 2000  PD | `B4C74036` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Hubble Fade by Robert Jurziga  PD | `2708D9FE` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Hubble Nebula by Robert Jurziga  PD | `72DD1CB8` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Iron Soldier  1994   a1 | `F7F3462B` | IDENTICAL | 494,058 -> 161,939 (**-67.2%**) | 142 (no change) | 1076 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JagFest Demo  2001   PD | `7DAE6472` | IDENTICAL | 445,859 -> 218,781 (**-50.9%**) | 0 (no activity) | 854 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JagMania  Apr 18   1998   PD | `1E68B4D1` | IDENTICAL | 652,899 -> 33,417 (**-94.9%**) | 0 (no activity) | 1431 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JagMania  Aug 31   2000   PD | `0E768B5A` | IDENTICAL | 777,638 -> 38,152 (**-95.1%**) | 0 (no activity) | 1547 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JagMania  Jan 06   2001   PD | `1ED4C963` | IDENTICAL | 619,520 -> 82,100 (**-86.7%**) | 0 (no activity) | 1303 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JagMania  Jul 8   2000   PD | `88F4D5ED` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JagMania  Sep 10   2000   PD | `D0CDBD65` | IDENTICAL | 652,899 -> 33,416 (**-94.9%**) | 0 (no activity) | 1431 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JagMarble  1999   PD | `EBAFD0DE` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JagMarble  2000   PD | `1B57A623` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JagMarble  Jul 13   1997   PD | `C1D3612C` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server 1.08 UPDATE - JAGOS  PD | `44091752` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server 1.08 UPDATE - KEYB  PD | `6912ED13` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - 27OBJ  PD | `AF81E751` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - 50HZ  PD | `BE069593` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - 60HZ  PD | `EB135948` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - INTRO  PD | `15DF5CF9` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - INTROMOD  PD | `EC130C25` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - SCALE3  PD | `CD61879B` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Server  Program Examples  - SCALE  PD | `60E86BF6` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Jaguar Tetris  1995   PD | `C539EF03` | IDENTICAL | 442,033 (no change) | 442,734 -> 8,135 (**-98.2%**) | 0 / 1849 | no *(qualifies, no filedb identity -- see policy)* |
| JDC Demo V1  2000   PD | `EFFCA60E` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JDC Demo V2  2000   PD | `8DC5D93C` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JDC Demo V3 - E-JagFest Demo by Lars Hannig  2000   PD | `AA25060D` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JDC Example by Lars Hannig  PD | `1187FF8F` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Joypad-TeamTap Tester by Matthias Domin  2000   PD | `239662F4` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Joypad-TeamTap Tester by Matthias Domin  2000   PD   a1 | `2A65C0D4` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Joypad-TeamTap Tester by Matthias Domin  2000   PD   a2 | `29E9E6AB` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| JSSDemo  Jaguar Sound System  V1.0b  08.11.2002   PD | `6D3BAE95` | IDENTICAL | 545,656 -> 139,989 (**-74.3%**) | 0 (no activity) | 1089 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| JSSDemoII  Jaguar Sound System  V1.0b  10.11.2002   PD | `FFC859F8` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Kasumi Ninja  1994   a1 | `CE5F0C11` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Ladybug Demo  PD | `0EB3579B` | IDENTICAL | 0 (no activity) | 24 (no change) | 0 / 0 | no |
| Ladybug Demo  rom   PD | `549844D6` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Mandelbrot Demo  PD | `39181742` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Memory Dump by Matthias Domin  1999   PD | `D9EC011A` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Music Demo  2002   ScatoLOGIC | `33D8C132` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Native Demo  bin   1997 | `85A70180` | IDENTICAL | 0 (no activity) | 442,644 (no change) | 0 / 0 | no |
| Native Demo  jag   1997 | `B5BDFFA5` | IDENTICAL | 442,976 -> 20,976 (**-95.3%**) | 3,770 (no change) | 1288 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| padtest | `94D88145` | IDENTICAL | 533,381 (no change) | 552 (no change) | 0 / 0 | no |
| Painter  1996   PD | `27C7C6A3` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Painter  bin   1996   PD | `71269584` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| PAULA Preview Demo 2 by Robert Jurziga  PD | `3CF18719` | IDENTICAL | 0 (no activity) | 442,690 (no change) | 0 / 0 | no |
| PAULA Preview Demo by Robert Jurziga  PD | `DA061A4E` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Phase Zero  2000   PD | `4203E23D` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| PlaySFX V1.0 by Robert Jurziga  2003   PD | `0B216C1A` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| QSOUND Demo  PD | `BC62A2BF` | IDENTICAL | 595,870 -> 117,441 (**-80.3%**) | 0 (no activity) | 1268 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| Rayman Demo  1995   UBI Soft | `E6BD422C` | IDENTICAL | 0 (no activity) | 0 (no activity) | 0 / 0 | no |
| Skyhammer  World | `3C044941` | IDENTICAL | 495,773 (no change) | 334,217 (no change) | 0 / 0 | no |
| SlamRacer Demo  PD | `FBDEDA89` | IDENTICAL | 610,152 -> 85,618 (**-86.0%**) | 22,332 (no change) | 1283 / 0 | no *(qualifies, no filedb identity -- see policy)* |
| SlamRacer Intro  PD | `63D6C1C6` | IDENTICAL | 626,348 -> 76,672 (**-87.8%**) | 454,445 -> 123,086 (**-72.9%**) | 1324 / 1195 | no *(qualifies, no filedb identity -- see policy)* |
| Towers II  World | `CAF33BD6` | IDENTICAL | 556,994 -> 136,673 (**-75.5%**) | 1,646 (no change) | 1488 / 0 | no *(qualifies, no filedb identity -- see policy)* |

### CD spot-checks (no rows possible; titledb v1 is cart-only)

| title | CRC32 | A/B | DSP interp/f off->on | GPU interp/f off->on | fires/f (DSP/GPU) | row |
| --- | --- | --- | --- | --- | --- | --- |

### Not swept (honest list)

- **CD titles beyond the 6 spot-checks** — no row is possible either way
  (cart-only titledb) and CD runs cost ~10× cart runs: Alice's Mom's Rescue,
  Ants, Battle Morph (Songbird), Beebris, Blue Lightning (Alt 1/Alt 2),
  BrainDead 13, Dragon's Lair, Elansar, Fast Food 64 (+ Holiday Snacks),
  Frog Feast, Frogz 64, Highlander, Iron Soldier 2 CD (both), Klax,
  Myst (Demo, Demo Alt), Ocean Depths, Philia, Robinson's Requiem,
  Saucer Wars, Simone, Space Ace, Vid Grid (×3), World Tour Racing (×2),
  Baldies `.chd` (same title as the swept `.cue`).
- **filedb retail CRCs not present in the corpus**: Raiden `31812799`
  (FF_VERIFIED; the swept image is the `(alt)` dump `0509C85E`),
  Trevor McFur (alt) `95143668`, Skyhammer `4471BFA0`, Towers II `83A3FB5D`
  (corpus Towers II dump is `CAF33BD6`, not a filedb row — swept clean,
  −75.5% DSP, no row per policy), Hyper Force, Breakout 2000, Flip Out (alt),
  Missile Command VR, Battle Sphere Gold, Air Cars (alt, bad dump),
  Brutal Sports Football (bad dump row `0FDCEB66`).  Per policy these earn
  nothing until swept.

### Interpretation notes

- GPU `interpreted/f ≈ 442,7xx` flat across many titles = the GPU consuming
  its full deterministic slice budget every frame (see
  mc3d-stall-attribution.md §1); a reduction from that plateau is idle spin
  the extrapolator now skips.
- `0 (no activity)` = that processor executed nothing in the window (title
  parked on 68K-only code, or didn't reach RISC-driven content headless —
  e.g. Kasumi Ninja `[a1]` variant vs. the retail dump that boots fine).
- Zero-win titles keep the option available manually; nothing here changes
  the global default (still `disabled`) or the DSPExec/GPUExec suppressor
  list (blit memo / dram_timing / pipeline timing / clock scale / vjtrace /
  gdb — see the #595 conflict warning in `libretro.c`).

### Reproduction

Per-title artifacts (4 CSVs + 4 logs + status) were produced under
`/tmp/idleskip-sweep/results/<slug>/` by two sequential workers; the verdict
is `cmp dsp_off.csv dsp_on.csv && cmp gpu_off.csv gpu_on.csv`.  Numbers in
the tables are transcribed mechanically from the TRAILER lines (no manual
rounding beyond 1 decimal).
