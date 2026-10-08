# Virtual Jaguar libretro v3.7.1

**Broken-games release, continued. Xenowings is now playable, Club Drive stops crashing
when you drive, and the homebrew carts that froze the Accurate blitter now run.**

v3.7.1 is the second broken-games release (milestone "v3.7.1, Broken games, continued"). It
fixes titles that froze, crashed, showed a black screen or ignored input. There is no
performance work and no new core option. The savestate format moves from v15 to v16; v3.7.0
and older states still load (see Savestates).

The Xenowings work makes the core emulate three more entries from Atari's own hardware-bug
list (TOM bug 13, JERRY bug 13, JERRY bug 2), and a new document lists where the manual is
wrong or silent (see Documentation).

---

## Highlights

- **Xenowings is playable.** It needed four fixes: two documented Atari hardware bugs
  (TOM/JERRY bug 13 and bug 2) and 68000 prefetch emulation. Gameplay rendered black,
  dropped back to the loader, and ignored input before. Headless-verified only; not yet
  confirmed in RetroArch (https://github.com/libretro/virtualjaguar-libretro/pull/819, https://github.com/libretro/virtualjaguar-libretro/pull/822, https://github.com/libretro/virtualjaguar-libretro/pull/823, https://github.com/libretro/virtualjaguar-libretro/pull/824, https://github.com/libretro/virtualjaguar-libretro/pull/825, https://github.com/libretro/virtualjaguar-libretro/issues/811).
- **Club Drive no longer crashes when you drive**, and its ground and horizon colours now
  match the real game; the cloud sky box is still missing (https://github.com/libretro/virtualjaguar-libretro/issues/831) (https://github.com/libretro/virtualjaguar-libretro/pull/828, https://github.com/libretro/virtualjaguar-libretro/pull/829, https://github.com/libretro/virtualjaguar-libretro/issues/611).
- **Seven homebrew carts that froze on the Accurate blitter now run**, and a blit that
  never ends no longer freezes RetroArch (https://github.com/libretro/virtualjaguar-libretro/pull/817, https://github.com/libretro/virtualjaguar-libretro/issues/800, https://github.com/libretro/virtualjaguar-libretro/issues/794).
- **Six homebrew binaries now load at the right address**, so they draw instead of
  sitting on a black screen (https://github.com/libretro/virtualjaguar-libretro/pull/826, https://github.com/libretro/virtualjaguar-libretro/issues/818).
- **White Men Can't Jump's intro logo is fixed** (https://github.com/libretro/virtualjaguar-libretro/pull/827, https://github.com/libretro/virtualjaguar-libretro/issues/736).
- **Memory Track saves work after a no-content boot plus a disc insert** (https://github.com/libretro/virtualjaguar-libretro/pull/815, https://github.com/libretro/virtualjaguar-libretro/issues/810).
- **Real-BIOS CD run-ahead and rewind stay in sync** (https://github.com/libretro/virtualjaguar-libretro/pull/814, https://github.com/libretro/virtualjaguar-libretro/issues/804).

---

## Game fixes

### Cartridge titles

- **Xenowings (https://github.com/libretro/virtualjaguar-libretro/issues/811).** Four separate faults stood between the title and a playable
  game. All are headless-verified (scripted button presses and framebuffer checks); none is
  yet confirmed on a RetroArch run.
  - *Black gameplay, TOM bug 13 (https://github.com/libretro/virtualjaguar-libretro/pull/819).* At the start of gameplay the GPU swaps its
    object-list buffer with `load (r0),r2` followed by `moveq #0,r2`. Atari's bug list
    (JTRM Rev 8 p.136, "Scoreboard failure on successive writes") says the late load data
    lands after the `moveq`, so the object list pointer gets the real address. The core
    wrote the load data first, so the pointer became 0 and the screen stayed black. After the
    fix the stage 1 playfield renders (94% of the frame lit in the PR's headless check).
  - *Back to the loader, 68000 prefetch (https://github.com/libretro/virtualjaguar-libretro/pull/823).* A few frames into gameplay the game
    runs a self-modifying anti-tamper check. On a real 68000 the two words it patches are
    already in the prefetch queue, so the old instructions still execute once. The core now
    models that, only where it is observable. The PR measured instructions retired within
    0.07% of the base on Doom, Alien vs Predator and Tempest 2000. Gameplay then stays up
    (990 consecutive lit gameplay frames, in both HLE and BIOS boot).
  - *Input did nothing, JERRY bug 13 and bug 2 (https://github.com/libretro/virtualjaguar-libretro/pull/822, https://github.com/libretro/virtualjaguar-libretro/pull/825).* The DSP joypad reader
    hit the same late-load rule on the DSP (https://github.com/libretro/virtualjaguar-libretro/pull/822) and then bug 2, "Scoreboard failure on
    indexed addressing mode stores" (JTRM Rev 8 p.134): a `div` followed by an indexed
    `store` writes the register's old value, not the quotient. After both, 835 frames differ
    from a no-input run in the PR's check, and screenshots show the ship moving, firing and
    scoring.
  - *Guard (https://github.com/libretro/virtualjaguar-libretro/pull/824).* The GPU idle-loop fast-forward now refuses a loop in which the
    late write-back fired, so the bug-13 rule and the fast-forward cannot disagree. In
    Skyhammer the rule fires 94 times in 3000 frames and nothing observable changed:
    identical frame hashes, audio and savestates.
- **Club Drive (https://github.com/libretro/virtualjaguar-libretro/issues/611, https://github.com/libretro/virtualjaguar-libretro/pull/828, https://github.com/libretro/virtualjaguar-libretro/pull/829).** The game froze or crashed a few seconds into
  driving. GPU `NORMI` returned one too high for every input (`$80000000` gave 9 where the
  chip gives 8, zero gave 0 where the chip gives -32). The jag_sim chip
  netlists decode to the lower values. Club Drive uses `NORMI` to pick a corner for each
  polygon it clips against the screen, so a polygon crossing two screen edges came out
  twisted, the edge walker bailed, and the GPU returned into garbage. With the fix, six driving input patterns from a
  saved state each ran 2400 frames clean with the picture moving; on the previous build
  four of the six crashed. The DSP has the same `NORMI` block and gets the same fix
  (https://github.com/libretro/virtualjaguar-libretro/pull/829; only Music Demo executes DSP `NORMI` in the cart corpus, with no
  visible or audible change). Club Drive's ground and horizon colours now match the real game,
  confirmed by the maintainer; its cloud sky box is a separate, still-open bug
  (https://github.com/libretro/virtualjaguar-libretro/issues/831). See Testing for the corpus effects.
- **Seven homebrew carts hung the Accurate blitter (https://github.com/libretro/virtualjaguar-libretro/issues/800, https://github.com/libretro/virtualjaguar-libretro/pull/817).** Chroma-Luma (bin),
  DEMO1 (bin), DEMO1B (two dumps), JagMania (Jul 8), Ladybug Demo and Ladybug Demo (rom)
  each froze inside one blit under the default Accurate blitter. The blitter was not slow:
  these are uploaded executables with no vector table, and several end their 68K main with
  `ILLEGAL`. The 68K then ran the vector table as code and walked its stack down into the
  blitter registers. The core now parks the `ILLEGAL` vector at a loop at `$1000` for any
  image that is neither a cartridge nor a CD. All seven now run to the end of a 600 frame
  test under the Accurate blitter, in both boot modes. Some of these demos used to produce
  noise from the runaway 68K; they now sit parked, so a few show a black or silent screen
  where they used to show garbage.
- **A blit that never ends no longer freezes the host (https://github.com/libretro/virtualjaguar-libretro/issues/794, https://github.com/libretro/virtualjaguar-libretro/pull/817).** Some blits
  never finish on real hardware (phrase mode below 8bpp whose X step never reaches the
  count). The core now hangs the emulated blitter the way the chip does: status reads busy,
  further starts are ignored, and it clears on reset. RetroArch keeps running. Where
  repeating the step is not idempotent the core runs one defined pass, and the crash
  watchdog logs `approx=1`. Across the 155-cart corpus, both blitters, both boot modes,
  there are now no timeouts.
- **Six homebrew binaries loaded $1000 low (https://github.com/libretro/virtualjaguar-libretro/issues/818, https://github.com/libretro/virtualjaguar-libretro/pull/826).** Headerless 68K binaries
  linked at `$5000` were being loaded at `$4000`, so their first `JSR` ran font data. The
  loader gained `$5000` as a candidate and breaks ties by which base makes `JSR`/`JMP`
  targets land on a function start. Exactly six of the 155 corpus titles change:
  Chroma-Luma (bin), JagMania (Jul 8), JagMarble (2000), FORCE Design Legion Force Jidai
  Intro Demo 0!, Joypad-TeamTap Tester [a1] and Music Demo (ScatoLOGIC). Each goes from a
  black screen to video (Music Demo also plays audio). The Music Demo fix is also the
  actual cause of https://github.com/libretro/virtualjaguar-libretro/issues/794: loaded low in BIOS mode its 68K sprayed the blitter registers.
  One thing remains for Music Demo: in BIOS mode it logs a single `dsp_pc_escape` at frame
  5, while HLE does not.
- **White Men Can't Jump intro logo (https://github.com/libretro/virtualjaguar-libretro/issues/736, https://github.com/libretro/virtualjaguar-libretro/pull/827).** Only the apex of the first logo
  (the Trimark pyramid) showed, and the High Voltage logo never appeared. The game starts
  the GPU decoding the logo and then fades it in with 128 blits without waiting. On real
  hardware each blit stalls the 68000 so the decode wins; with zero-time blits the fade
  finished while the logo was still decoding. The title now turns on Blitter Bus Timing by
  default through a per-title row. It is limited to this one title; it is not on for
  everything. Fast vs Accurate blitter makes no difference. The new `wmcj_intro_logo` check, which needs the
  first held picture to be at least 25% lit, went from 6.4% to 43.8%. A 6000-frame driven run
  reached the menus, team select, the city map and a live match with no new freeze, but
  https://github.com/libretro/virtualjaguar-libretro/issues/736 covers more than the logo and this release does not claim the whole game is clean.

### Jaguar CD titles

- **Memory Track saves after a no-content boot plus a disc insert (https://github.com/libretro/virtualjaguar-libretro/issues/810, https://github.com/libretro/virtualjaguar-libretro/pull/815).**
  Two faults. The core reported save RAM only when content was loaded, so the frontend saw
  nothing to save on a no-content launch. And the insert path never reinstalled the NVM BIOS
  module, which the insert's reboot wipes, so a game started by an insert saw the Memory
  Track with no NVM BIOS behind it. Save RAM is now exposed whenever the Memory Track is
  enabled, and the insert reinstalls the module. Covered by new `test_disk_control` cases
  (marker written before the insert survives it). **Not yet verified in RetroArch:** the
  actual `.srm` file name for a no-content launch, and a real game saving then reloading
  after an insert. With the Memory Track disabled, nothing is exposed.
- **Real-BIOS CD run-ahead and rewind (https://github.com/libretro/virtualjaguar-libretro/issues/804, https://github.com/libretro/virtualjaguar-libretro/pull/814).** With `cd_boot_mode=bios`,
  Hover Strike diverged under run-ahead because two pieces of state lived outside the
  savestate: the flag that gates the 68K boot-stub injection, and the remainder that
  decides whether a CD FIFO refill takes 2 or 3 ticks. Both are now saved. Run-ahead at
  warmup 400, 150, 600 and 800 now agrees with a plain run. This is the savestate change
  below.

---

## Savestates

- **States are now v16.** v3.7.0 (v15) and older states still load, with the new fields left
  at their live values (exactly v3.7.0 behaviour). v16 states do not load in older builds.
  A v15 file written by a v3.7.0 build loads in v3.7.1 with video identical to v3.7.0.
- **Three chunks are appended after the HLE chunk:** `CDB1` and `CDX1` (real-BIOS CD boot
  flag and refill phase, https://github.com/libretro/virtualjaguar-libretro/pull/814) and `BLH1` (the "blitter hung" flag and its stuck count,
  https://github.com/libretro/virtualjaguar-libretro/pull/817). A missing or unrecognised chunk loads as the default, so a v16 state that
  lacks `BLH1` loads with a normal blitter.
- Format details and the chunk layout: `docs/savestate-compat.md`.

---

## Documentation

- **New `docs/jtrm-errata.md` (https://github.com/libretro/virtualjaguar-libretro/pull/821, https://github.com/libretro/virtualjaguar-libretro/issues/820).** A list of where Atari's manual is
  wrong, contradicts itself or says nothing, plus which entries in Atari's own
  hardware-bug list (JTRM Rev 8 pp.133-141, Hardware Bugs & Warnings, Software Reference)
  the core emulates. Each entry gives what the manual says, what is true, the evidence
  (netlist, Verilator, game, PR or commit) and where it lives in the emulator.
- **110 corrections to the distilled `docs/jtrm-*.md` files**, each re-read against the
  PDF. Examples: DSP local RAM is `$F1B000-$F1CFFF`, 27 opcode numbers were wrong
  (`CMP` is 30, not 15), `SH` with a positive count shifts right, and `MULT` is unsigned
  while `IMULT` is signed. https://github.com/libretro/virtualjaguar-libretro/pull/821 changes only docs and comments; no behaviour changes.
- **Cart and CD boot matrices regenerated** for this release (https://github.com/libretro/virtualjaguar-libretro/pull/830). The site pages
  were audited for v3.7.0 (https://github.com/libretro/virtualjaguar-libretro/pull/808).

---

## Testing

Release gate, v3.7.1 against the v3.7.0 tag, each built from a clean tree.

- **Cart corpus, 155 titles.** Fast blitter: 155/155 reach game code in HLE and 154/155 in
  BIOS (was 155 and 153; the v3.7.0 LOAD_FAIL, JagMarble BIOS, now completes). Accurate
  blitter: **155/155 in both modes**, up from 148 HLE and 136 BIOS in v3.7.0.
- **CD corpus: 28 of 28 discs reach game code in both modes**, unchanged from v3.7.0.
- **NORMI corpus sweep (https://github.com/libretro/virtualjaguar-libretro/pull/828).** Across 155 ROMs, HLE and BIOS, both blitters, the only
  titles that change are Club Drive, Checkered Flag and Native Demo. Club Drive's
  rendering was confirmed correct by the maintainer. Native Demo (bin), HLE, was already
  a runaway GPU from frame 1; its garbage path reshuffles to a black, silent screen.
- Several Fast-blitter rows for homebrew demos (DEMO1, DEMO1B, Ladybug and others) moved
  from audio or static video to silent or black. The matrix bisect (https://github.com/libretro/virtualjaguar-libretro/pull/830) puts the flip
  at https://github.com/libretro/virtualjaguar-libretro/pull/817: in each case the old signal was a runaway 68K, not real output.
- The cart boot matrix runs only the Fast blitter by default; the Accurate sweep is not
  committed (it needs `CART_MATRIX_PROBE_ARGS`). The matrices record a stage plus coarse
  video and audio flags, so they cannot see most of what changed here. Xenowings is not in
  the corpus.

---

## Developer and CI

- `scripts/pr-link-issue.sh` now signals failure modes with distinct exit codes (2 no tag,
  3 API error) and the workflow reads those instead of matching error text; `release.yml`
  target count corrected from 16 to 24 (https://github.com/libretro/virtualjaguar-libretro/pull/812).
- `test_cd_bios_boot` now fails when the CD transfer wedged (it printed PASS before)
  (https://github.com/libretro/virtualjaguar-libretro/pull/813, https://github.com/libretro/virtualjaguar-libretro/issues/799).
- The voicemodem pair test waits for its peer instead of the wall clock; 120/120 under
  heavy load where develop failed about 1 run in 6 (https://github.com/libretro/virtualjaguar-libretro/pull/816, https://github.com/libretro/virtualjaguar-libretro/issues/796).
- New and extended tests: `test_blitter_hung`, `test_upload_illegal_park`,
  `test_club_drive_611`, `xenowings_ingame_video`, `wmcj_intro_logo`, `test_m68k_prefetch`,
  `test_state_compat` cases for v16, and NORMI cases in `test_gpu_ops` and `test_dsp_ops`
  taken from the netlist decode. ROM-gated tests skip without the private ROMs.
- `cart_boot_matrix.sh` gains a `CART_MATRIX_PROBE_ARGS` knob and refuses to write without an
  explicit `CART_MATRIX_OUT`, so an Accurate sweep cannot overwrite the published Fast rows.
- New test-ABI counters: `gpu_load_wb_fixups`, `dsp_load_wb_fixups`, `dsp_div_store_fixups`.

---

## Known issues

- **Closed without a code change:** the Dragon's Lair / Space Ace / BrainDead 13 interactive-scenes report
  (https://github.com/libretro/virtualjaguar-libretro/issues/737) does not reproduce on v3.7.0 or later. These games start
  on A/B/C, not START (RetroPad START is mapped to Jaguar Option by default). The AvP noise report
  (https://github.com/libretro/virtualjaguar-libretro/issues/633) could not be reproduced either; reopen either issue if it still happens.
- **Myst CHD on Raspberry Pi 5** is confirmed working on the v3.7.0 release build
  (https://github.com/libretro/virtualjaguar-libretro/issues/774).
- **Club Drive** still draws a flat colour where the real game shows a cloud sky box
  (https://github.com/libretro/virtualjaguar-libretro/issues/831).
- **White Men Can't Jump** is not declared fixed beyond its intro logo (https://github.com/libretro/virtualjaguar-libretro/issues/736).
- **Xenowings and Memory Track:** verified headless only, see above.
- **GPU bug 2** (the TOM half of the indexed-store-after-divide bug) is not modelled; only
  the DSP half is, because nothing needed the GPU half (https://github.com/libretro/virtualjaguar-libretro/pull/825).
- **Chroma-Luma and JagMania** were still black after the hang fix alone; https://github.com/libretro/virtualjaguar-libretro/pull/826 gives
  them video.

---

## Upgrading

- **Savestates:** v15 and older load; v16 states do not load in older builds.
- No core option changes.

## Downloads

Pre-built libretro cores are attached to the GitHub release, built by `release.yml` (24
targets), Each binary has a matching debug archive
and SHA256 checksums are in `SHA256SUMS.txt`. Every push to `develop` publishes a nightly
pre-release: https://github.com/libretro/virtualjaguar-libretro/releases/tag/nightly

## Maintainers

Joseph Mattiello, with the Virtual Jaguar libretro contributors. Original Virtual Jaguar
by David Raingeard (Potato Emulation) and James Hammons.
