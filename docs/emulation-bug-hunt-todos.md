# Emulation Bug Hunt TODOs

This tracks actionable issues found while auditing old inline comments that
describe guesses, timing gaps, or known emulation shortcuts.

## Cross-cutting finding from cluster investigation (2026-04-30)

A round of parallel sub-agent investigation snapshotted DSP RAM, 68K regs,
TOM/JERRY/DAC regs, low main RAM, and rendered framebuffers at multiple
frames for the still-broken cart titles in **both real-BIOS and HLE
modes**. Headline result: **4 of 5 hang/crash titles** (Hyper Force,
Iron Soldier, Ruiner Pinball, Super Burnout) have **identical or
near-identical behavior in BIOS vs HLE** — same stuck PC, same nonblack
pixel count, same DSP state. So those titles are **real emulation bugs,
not HLE-init issues**. Real BIOS doesn't fix them either.

The narrowest reproducible per-title clue from the snapshots:

- **Wolfenstein 3D (audio, both BIOS and HLE)**:
  - HLE: completely silent (RMS=0, first-audio=-1) for the entire run
    (verified up to frame 1800 / 30s).
  - BIOS: brief BIOS-chime audio at frames 34-~600, then **silent
    forever**.  The BIOS-chime is not Wolf3D's own audio, it's the
    BIOS's startup tone played before the cart takes over.  Wolf3D's
    own game audio never starts in either mode.
  - User-reported (real RetroArch on iOS): also no audio in either
    mode, plus a BIOS-specific quirk that pressing A/B during the
    BIOS logo skips initialization and Wolf3D then fails to boot
    entirely (no other game does that).
  - Root cause: Wolf3D's DSP escapes work RAM by frame 1800.  At
    that point dsp_pc=0x000003FA (HLE) / 0x00181C43 (BIOS) — both
    in main RAM, not DSP work RAM (0xF1B000-0xF1CFFF).  Atari Karts
    HLE keeps DSP at 0xF1B3FA correctly with active LTXD/RTXD.  So
    Wolf3D's DSP code (whether BIOS-loaded or cart-loaded) has a
    JUMP-through-register instruction where the register holds a
    garbage value, sending the DSP into main-RAM/68K code.  Tried
    memcpy'ing the BIOS DSP audio engine
    (jaguarBootROM[0x214E..0x2916], 1992 bytes) into DSP RAM and
    starting the DSP with D_PC = engine entry / mainloop / DSPGO=1
    — same DSP-escape symptom because the engine reads DSP
    registers we never initialize and uses them as jump targets.
    Reverted the engine copy; needs full DSP register-bank state
    replication.
- **Skyhammer (HLE, audio)**: agent's snapshot showed 68K at
  `0x008022EE` for frames 1-60. Followup boot_timeline at frames
  60/300/600/1200/3600/7200 shows PC actually progresses through cart
  code (0x832F4A → 0x8B6378 → 0x1644C → 0x16610 → 0x8AC7E2 → 0x8AC930)
  — Skyhammer's 68K is NOT stuck. The DBF loop at 0x22EE is just a
  long delay (D0=0xFFFFFF, ~167M cycles, ~6 seconds at 13.3 MHz).
  Audio clipping is purely a DSP-side / I2S issue — the cart's main
  68K thread runs fine. Cart-disassembly at the DBF address is not
  the right approach; root cause is in JERRY/DSP timing or HLE
  audio-engine missing state.
- **Raiden (HLE, won't boot)**: cart's startup at `0x802000`
  copies game code from cart `0x802026..0x802986` (0x960 bytes) into
  main RAM at `0x180000+`, then JMPs there. Code runs initial
  setup (LEA into A4 = `0x1803B2`) and then enters a polling loop
  at `0x18014A`: `TST.B $2C7(A4); BEQ.S -4` — i.e. spin forever
  until `RAM[0x180679]` becomes non-zero. Some interrupt handler
  is supposed to set that byte; our HLE generic-RTE stub at
  `0x404` just RTE's so the byte never gets set and the cart hangs.
  68K stack pointer A7 stays stuck at `0x001FFFFC` (one push deep
  from initial `0x00200000`), confirming no interrupts are
  actually firing during the spin (so it's not "exception
  double-fault" — agent's earlier hypothesis was based on wrong
  dlsym dereference and is incorrect).
  Fix path: cart probably installs its own IRQ handlers via the
  BSRs at `0x180000` and `0x180004` BEFORE the poll loop; need to
  trace what those BSRs do and identify why interrupts aren't
  arriving (TOM video IRQ enable, JERRY IRQ enable, vector base).
- **Ruiner Pinball**: identical 0x809CAE-stuck PC and 0% nonblack
  pixels in BIOS and HLE. Cart never gets past initialization.
  Disassembling around the stuck address shows a routine at 0x9CA0
  that does `CMPI.L #0, $00005B18; BEQ +6; JMP $00802000`, plus a
  separate routine at 0x2248 that calls a function pointer at
  `$0000402C`, runs `JSR $00802380`, then writes
  `MOVE.L #1, $00005B18` only if `RAM[$4068]` has bits 0+16 set and
  bit 9 clear. Probing those addresses each frame for 600 frames in
  both BIOS and HLE shows: **`$402C` and `$4068` stay 0 forever in
  both modes**, while `$5B18` cycles through various non-zero values
  (PRNG / cart-side scratch). So the cart is missing the
  initialization that should populate `$402C` (function pointer) and
  `$4068` (interrupt-flags accumulator). Real BIOS does not provide
  these — implies the cart itself is responsible, but a precondition
  for that init is failing (likely an interrupt that should fire
  early in boot but doesn't). v2.3.0 work: trace the cart's
  interrupt-handler installation path and identify what
  precondition needs to hold for the init routine to run.
- **Hyper Force, Iron Soldier, Super Burnout**: matching pixel
  counts and PC ranges in BIOS/HLE. Engine-level bugs, not init.
- **NBA Jam TE / Towers II / Tempest 2000**: empirical flicker score
  2.6× / 2.6× / 1.3× the Atari Karts baseline (per-pixel temporal
  stddev across a 16-frame window). Confirms the symptom and gives a
  regression-watcher metric.

A more accurate `JaguarReset` HLE init now writes `SCLK=0x13` and
`SMODE=0x15` (matching what the BIOS audio engine ends up programming —
INTERNAL + WSEN + FALLING; previous defaults `0x08` / `0x01` were the
BIOS *pre-engine* values). Verified Atari Karts negative control still
clean; **did NOT fix Skyhammer / IS2 audio clipping** — the DBF-loop
diagnosis above explains why a single register-init tweak isn't enough.

Raw investigation data left in `/tmp/`:
`/tmp/cluster_findings.md` (summary), `/tmp/hle_diff_*.md`,
`/tmp/flick_*.txt`+`.ppm`, `/tmp/boot_logs/*.txt`,
`/tmp/{hyper,iron,hover,ruiner,super}_*_1800.png`.


## Recently Addressed

- TOM frame rollover now follows the `VP` register instead of hard-coded
  NTSC/PAL field lengths. This targets visible "screen slap" jumps when games
  program custom video timing.
- TOM visible-line rendering no longer computes framebuffer pointers for
  offscreen halflines.
- Event scheduling now treats negative residual times as "due now" instead of
  feeding negative microseconds into CPU/GPU/DSP cycle conversion.
- JERRY JINTCTRL word writes are now decoded only at `$F10020`, so writes to
  the adjacent `$F10022` word cannot alter the 68K interrupt mask or pending
  latches.
- Libretro geometry changes now apply after the frame rendered with the
  previous pitch is submitted, avoiding a one-frame pitch mismatch when games
  reprogram TOM display registers.
- The dead `objectp_running` flag and its misleading VMODE write side effect
  have been removed; OP execution is driven directly by halfline display state.
- The unused pipelined `DSPHandleIRQs` export has been removed; live DSP
  interrupt dispatch goes through the non-pipelined handler used by
  `DSPSetIRQLine` and the IMASK-cleared path.
- OP scaled bitmap clipping now keeps fractional phrase width for all edge
  cases, avoiding divide-by-zero when small `hscale` values truncate an
  integer-scaled phrase width to zero.
- OP scaled bitmap rendering now starts the horizontal scale phase at zero for
  1:1 `hscale=$20` output and applies `firstPix` to the first source phrase,
  treats `iwidth == 0` as one phrase, keeps the visible edge pixel for
  reflected left-edge objects, and handles magnified `hscale` source stepping,
  with direct 4 BPP coverage for 1:1, 3:2, 2:1, left/right-edge clipping, and
  reflected edge clipping in `test_hle_bios`.
- OP fixed bitmap rendering now honors `firstPix` for 2/4/16/24 BPP paths,
  not just 1/8 BPP, and avoids applying it again after clipping skips whole
  source phrases, with direct 4 BPP coverage in `test_hle_bios`.
- TOM interrupt sources now latch pending status even when their CPU enable
  bits are clear; enabling a pending source asserts the 68K IPL2 line, with
  direct video IRQ latch coverage in `test_hle_bios`.
- Headerless raw homebrew loading is now conservative but supported for
  recognizable startup patterns at inferred `$4000`, `$20000`, or `$802000`
  bases; unknown raw files still fail instead of booting invalid RAM.
- JERRY SSI `SSTAT` reads now report the modeled status bits instead of the
  generic unmapped `$FFFF` placeholder.
- `make test` now includes `test_dsp_unit`, covering DSP interrupt dispatch,
  priority, return-address stack push, register banking, and execution basics.
- `test_hle_bios` now covers HLE workspace state, exception vectors, I2S
  defaults and `SSTAT` readback, JERRY JINTCTRL decode, OP scaled clipping,
  deferred geometry updates, PAL timing, and custom `VP` rollover.
- `make test` now includes event queue coverage for zero/negative-time event
  handling.
- `make test` now runs `test_audio_clipping`, an automated boot-time
  audio clipping detector. The test captures the libretro audio batch
  output, measures saturation density, longest run at +/-32767, and
  sustained RMS, and asserts on a negative control (Atari Karts) plus
  two known-broken regression watchers (Skyhammer, Iron Soldier 2)
  flagged with `--expect-clipping` so the test goes red the day a fix
  lands and the bug disappears. A/B-tested against `libretro/master`:
  Skyhammer's clipping is pre-existing (master clips harder, ~34%
  sample-saturation density vs ~25% on this branch) so it is not a
  regression introduced here.

## Game compatibility (v2.2.0)

Status of titles called out in the libretro tracker (issue #38 sub-list).
Verified against private ROMs unless otherwise noted.

### Fixed in v2.2.0

- Air Cars — title screen is no longer cut off.
- Atari Karts — vertical bar artifact on the side during gameplay is gone.
- Battlesphere Gold — menu black-screen on real-BIOS path is fixed (a
  separate fast-blitter menu rendering glitch still exists; see below).
- Club Drive — title screen is no longer cut off, missing textures restored.
- Cybermorph — missing/glitched textures fixed; the digitised Skylar voice
  also plays correctly now.
- Doom — vertical bar gone; the legacy "Doom resolution hack" core option
  is removed (proper PWIDTH pixel replication makes it unnecessary).
- Flashback - Quest for Identity — full-screen display restored
  (was anchored to the left).
- I-War — title cut-off and gameplay glitches fixed on the accurate
  blitter (fast blitter has a separate flickering issue; see below).
- Kasumi Ninja — title-screen flicker, missing textures, and post-title
  freezes fixed (some stages still show vertical tearing 3/4 down the
  screen, regardless of BIOS — see below).
- Missile Command 3D — minor flickering / glitches resolved.
- Pinball Fantasies — intro screens no longer cut off.
- Powerdrive Rally — intro screens no longer cut off; new-game crash gone.
- Skyhammer — gameplay no longer freezes (audio is clipped/loud, see below).
- Supercross 3D — full-screen display restored, textures readable.
- Syndicate — pink-patches-in-text fixed on the accurate blitter (fast
  blitter still has the original issue; see below).
- Trevor McFur in the Crescent Galaxy — loading-screen cut-off fixed.
- Val D'Isere Skiing & Snowboarding — full-screen display restored
  (a player-vs-snow z-order regression introduced; see below).
- Zero 5 — flickering resolved.
- Zoop — game no longer crashes on launch.
- HLE BIOS bridge — Raiden / Kasumi Ninja and similar titles that
  previously needed a real BIOS image now boot under HLE.

### Still broken / regressed (track these into v2.3.0)

- **Battle Sphere (accurate blitter):** the targeting reticle drawn
  over enemies leaves dark/dotted artefacts where the framebuffer
  should show through. Fast blitter renders this correctly. Likely
  involves phrase-mode `byte_merge` against `DCOMPEN` / `BCOMPEN`
  comparators with `BKGWREN` enabled — the inhibited bytes are
  taking the DSTDATA register value instead of the framebuffer
  pixel. A first attempt to widen `phrase_mode && !dsten` to also
  read framebuffer when comparators are on did **not** fix the
  bug, so the case is more nuanced than the obvious gate. Needs a
  logged COMMAND/A1/A2 register dump from one bad blit.
- **Battlesphere Gold (fast blitter):** menu rendering glitches
  (separate from the accurate-blitter reticle issue) regardless of
  BIOS. Fast-blitter-only.
- **Fight for Life:** flickering graphics, mainly in the menu —
  looks like black bars scrolling over text/button texture layers.
  Some layer compositing ordering is off.
- **Hover Strike:** title and graphics fixed, but the game
  **crashes during gameplay**, with or without real BIOS.
- **Hyper Force:** black screen after title; doesn't progress with
  or without real BIOS.
- **Iron Soldier:** boots through title and character select, then
  black-screens (music keeps looping, looks like a video-mode
  switch that fails). With or without real BIOS.
- **Iron Soldier 2:** title now displays correctly, but the music
  is clipped/over-loud on the title and the screen black-screens
  after character selection.
- **NBA Jam Tournament Edition:** occasional screen jumping. Same
  pattern as White Men Can't Jump (both High Voltage Software);
  likely an engine-shared timing/IRQ-phase issue. Tracked
  separately.
- **Raiden:** still requires real BIOS — won't boot on HLE.
- **Ruiner Pinball:** won't boot regardless of BIOS, PAL/NTSC, or
  blitter mode.
- **Super Burnout:** new-game now starts but doesn't render
  correctly — only the background layer draws (no cars, scenery,
  or HUD). Audio is present.
- **Tempest 2000:** rare flickering still occurs; the accurate
  blitter is also significantly slower than fast on this title,
  enough to drop below full speed on hardware where Jaguar should
  comfortably run. Performance-side TODO.
- **Towers II:** unplayable due to flickering/flashing. PAL mode
  is worse, and PAL audio is broken — looks like a timing issue.
- **White Men Can't Jump:** boots, but lots of flickering; can't
  get past main menu on HLE; real BIOS doesn't boot the splash
  at all. (See the more-detailed entry in High Priority below.)
- **Wolfenstein 3D:** if the real BIOS is allowed to fully boot
  before pressing a button the game starts (audio still broken);
  exiting BIOS early black-screens. HLE is broken.
- **Val D'Isere (regression):** the player sprite z-order with the
  snow layer is reversed — snow draws over the skier unless the
  player jumps above the horizon line. New regression in v2.2.0.
- **I-War (fast blitter):** title cut-off returns when the fast
  blitter is enabled. Accurate blitter has a separate floor-layer
  flicker during gameplay.
- **Kasumi Ninja (some stages):** vertical tearing 3/4 down the
  screen on certain stages, regardless of BIOS.
- **Syndicate (fast blitter):** pink-patches-in-text remains on
  the fast blitter (accurate blitter is fine).
- **Skyhammer / Iron Soldier 2 audio clipping:** boot-time
  audio is a saturated square wave (sustained +/-32767, ~25%
  saturation density on Skyhammer, ~21% on IS2).  Detected
  automatically by `test_audio_clipping`.
  - **Repro:** load with HLE BIOS, run ~300 frames, alternates
    +/-32767 with rare in-between values.
  - **Critical clue (2026-04-29):** with **real BIOS**,
    Skyhammer audio is **clean** (RMS ~3987, 0% saturation).
    With HLE, it's the saturated square wave. So the bug is
    **HLE-init delta**, not DSP arithmetic.  Atari Karts works
    on HLE — its DSP code is self-contained, while Skyhammer /
    IS2 likely depend on BIOS-loaded DSP audio engine state.
    (Atari Karts was A/B-tested as the negative control.)
  - **Pre-existing:** A/B-tested against `libretro/master` —
    master clips harder (~34% on Skyhammer) so this is a
    long-standing bug, not a regression introduced by this PR.
  - **DSP-state diff (2026-04-29):** snapshotted DSP RAM at
    frame 5/10 in BIOS vs HLE mode and binary-searched the
    embedded `jaguarBootROM` blob for the matching prefix:
    - The BIOS pre-loads a **1992-byte DSP audio engine** at
      `jaguarBootROM[0x214E .. 0x2916]` into DSP RAM offset 0
      and starts the DSP (DSPGO=1).
    - Engine prefix at offset 0:
      `98 00 B0 30 00 F1 D0 00 E4 00 E4 00 E4 00 E4 00`
      (MOVEI #$F1D000, R0 — wavetable ROM ptr — then NOP slots).
    - **But** copying this engine into DSP RAM and starting the
      DSP in HLE init does NOT fix the clipping. Both Skyhammer
      and IS2 OVERWRITE the engine with their own DSP code by
      ~frame 30, so having it pre-loaded is moot for them.
    - Atari Karts is unaffected by the engine copy
      (verified — same RMS 410.8, 0% saturation).
    - The DSP code Skyhammer loads in HLE-mode at frame 175 is
      **dramatically different** from the code it loads in
      BIOS-mode at frame 175 (DSP RAM contents diverge across
      ~95% of the audio engine area). So Skyhammer's 68K code
      is reading something at boot to choose which DSP audio
      routine to load, and HLE provides a different value than
      BIOS.
  - **Next steps:** trace what Skyhammer's 68K code reads
    early-boot from BIOS-area (`0xE00000+`), low main RAM
    (`0x0000-0x0800`), or BIOS-installed exception vectors.
    The BIOS leaves a vector table with handler addresses like
    `06066xxx`, `06067xxx`; HLE installs RTE stubs at different
    addresses. If Skyhammer JSRs through one of these vectors
    to a BIOS audio-init routine, our RTE stub returns
    immediately and the routine never runs. That's the most
    plausible remaining hypothesis.
  - Diagnostic tools: `/tmp/dsp_diff.c` and `/tmp/dsp_snapshot.c`
    in the conversation log capture DSP RAM and key DAC/DSP
    registers via dlsym — re-buildable from the recipe in
    docs/test-infrastructure.md if needed.

### Performance
- Accurate blitter is significantly slower than fast on several
  titles (Tempest 2000 is the loudest). The fast-blitter and SIMD
  paths cover the common case; the scalar accurate path needs more
  inner-loop tightening or an SIMD-accelerated DCOMPEN / BCOMPEN
  variant.

## High Priority

- `src/tom/op.c`: replace the Object Processor runaway-list guard with a real
  resumable scheduler. The current `OP_RUNAWAY_GUARD_OBJECTS` limit prevents
  malformed lists from hanging the emulator, but it does not model OP cycle
  consumption or overloaded-list suspend/reentry timing.
- `White Men Can't Jump`: private-ROM tracing shows the visible jumps happen
  with a stable `OLP` and object-list contents. The jumping object is a fixed
  bitmap consumed by OP write-back and restored outside direct 68K stores,
  through the 68K VBI path. Bad frames occur when the video interrupt lands
  while the 68K SR masks level-2 IRQs, delaying the object restore until active
  display (`VC=$0866` in the captured repro). Suppressing OP write-back hides
  the jump but contradicts hardware behavior. TOM timer IRQs are the immediate
  collision source: disabling TOM PIT interrupts removes the boot-logo jump,
  while alternate PIT reload formulas and line-buffer clearing probes made the
  frame sequence worse. A real TOM fix landed for `INT1` byte reads/writes
  exposing and clearing pending IRQ sources consistently with word accesses,
  but WMCJ still needs 68K/TOM interrupt phase accuracy rather than bitmap
  geometry.
- `src/tom/op.c`: continue auditing scaled bitmap semantics beyond the
  small-`hscale`, `firstPix`, 1:1 phase, `iwidth == 0`, and reflected
  edge fixes. Additional non-integer ratios and larger reflected phrase
  alignment cases still need repro or hardware coverage because they affect
  road/ground rendering.
## Medium Priority

- `src/tom/tom.c`: replace hard-coded visible-window constants with values
  derived from TOM timing registers where possible. This should be tested
  against NTSC, PAL, and games that reprogram HDB/HDE/VDB/VDE.
- `src/tom/op.c`: document or test the transparent index / encoded black
  behavior in 4/8 BPP paths. Comments disagree about whether index 0 is
  transparent in all relevant modes.
- `src/jerry/dac.c`: continue SSI/I2S modeling beyond basic `SSTAT` readback,
  including slave-clock timing and CD audio word-strobe behavior.
- `src/jerry/dsp.c`: use the new IRQ return-address coverage before changing
  live non-pipelined dispatch semantics.

## Lower Priority / CD Branch

- `src/cd/cdintf.c`: many CD interface methods remain stubs.
- `src/cd/cdrom.c`: FIFO, I2S-to-JERRY, DSA command timing, and interrupt
  generation have several comments marked as approximate or hard-coded.
- `src/core/filedb.c`: cartridge/file-type detection still has known edge
  cases noted by inline comments.

## v2.3.0 follow-up notes (cleanup + investigations)

These came out of the v2.2.0 manual pre-merge review.  None block
shipping v2.2.0; capture them so they don't get lost.

### Removed code we should remember

- **STUBULATOR ROM hook** (was: `// Maybe instead of this, we could try
  requiring the STUBULATOR ROM?`).  The Stubulator is a small Atari-era
  PD ROM that bootstraps homebrew without needing the real BIOS.  We
  removed the comment that suggested using it as a fallback.  Worth
  considering as a v2.3 test-harness fixture (not shipping it as a
  user-facing fallback) — it's a small, known-state cart we can drop
  in for HLE-vs-real-BIOS A/B tests.
- **Old `src/mmu.c`** removed in the source-tree reorganization.  The
  original file emulated a Jaguar MMU (paging) that no shipping cart
  ever used; the only consumer was an `Alpine` debugger mode that's
  also been removed.  If we ever add Jaguar dev-cart / networking / CD
  features that depend on memory protection, this is the file to
  resurrect (in master at the time of the reorg).
- **`vjs.hardwareTypeAlpine`** field + `alpineROMPath` (master had
  these, branch removed them).  Alpine was an Atari developer board
  with extra debug RAM at $C00000-$DFFFFF.  Removing the option leaves
  RetroArch users without an "Alpine mode" toggle; if homebrew
  developers ever ask for it back, restore the field, the file-load
  path in `src/core/file.c::JST_ALPINE`, and the option entry in
  `libretro_core_options.h`.
- **`NEW_SCOREBOARD` `#ifdef` arms in `src/jerry/dsp.c`**.  Master had
  19 references; branch has 15 (4 removed).  The remaining
  `#define NEW_SCOREBOARD` toggles a hotter scoreboard path; the
  removed `#else` arms were the cold/legacy variant kept for a
  comparison that nobody runs anymore.  No functional change, but if
  the new path ever regresses, the historical alternative is in
  master's `src/dsp.c`.

### Comments / TODOs that should not fall off

- **`src/tom/blitter.c` `!!! FIX !!!` comments** (DCOMPEN 8/16 BPP-only,
  1-bit expansion behaviour).  Pre-existing from upstream; wrap into
  the OP/blitter audit task already on this list.
- **HLE sound-engine auto-ack** in `src/jerry/dsp.c::DSPReadLong` —
  current behaviour zero-acknowledges DSP-RAM polls in the
  `DSP_SOUND_CMD_BASE..DSP_SOUND_CMD_END` range when the DSP isn't
  running and we're in HLE.  This is a workaround for games that
  poll sound-engine flags the BIOS engine would normally clear.
  Documented inline; revisit when the BIOS DSP audio engine is
  actually replicated (Wolf3D / Skyhammer / IS2 family).
- **`JERRYI2SCallback` SCLK/SMODE coupling** (commented at top of
  `src/jerry/dac.c`).  Output is hard-coded 48 kHz; SCLK changes
  affect DSP I2S rate but don't resample the host output.  Future:
  add proper resampling so games that vary SCLK at runtime for pitch
  effects work.
- **`test/baselines/{jagniccc,yarc}.png` are 241 px tall** (one row
  taller than the 240-line NTSC visible region).  The extra row comes
  from `miniretro` in our regression harness — it captures one row
  past `max_height` for some video paths.  Not a Virtual Jaguar bug;
  if we ever switch capture tools the baselines need re-shooting.

### Code-organization items for v2.3.0

- `src/jerry/dsp.c` is ~3000 lines with several long functions; split
  by responsibility (decode / dispatch / IRQ / scoreboard / state) so
  it's easier for humans + LLMs + static-analysis tools to reason
  about.
- `link.T` export gating — **done in v2.2.0**.
  Production `link.T` now exports `retro_*` only;
  `link-test.T` carries the wider symbol surface for the
  white-box test harnesses (DSP*, dsp_*, m68k_*, GPU*, gpu_*,
  JERRY*, TOM*, OP*, Jaguar*, jaguarMainRAM, jagMemSpace,
  regs, sclk, smode, lowerField, vjs, ...).  Makefile picks
  link-test.T when `TEST_EXPORTS=1`, which the `test` target
  sets automatically.
  Remaining for v2.3.0: macOS / iOS / tvOS dylibs ignore
  `--version-script` and currently still export everything
  with default visibility.  Slim with
  `-Wl,-exported_symbols_list,exports.list` + a per-platform
  exports list, gated the same way (test builds get the wide
  list).
- `GIT_VERSION` / `CORE_VERSION` are spread between Makefile and
  libretro.c.  Move to a generated `version.h` so both sites read
  the same source of truth.
- HLE constants block in `src/core/jaguar.c` was moved from inside
  `JaguarReset()` to file scope this PR — done.  Future: consider
  promoting the BIOS-known register addresses into a shared header
  so other subsystems (e.g. `test/test_hle_bios.c`) don't have to
  hard-code them.
- Optional: add static-analysis bots / linting / dead-code finders
  / `const`-correctness audits as a CI step.  `clang-tidy` and
  `cppcheck` would be good starting points; the codebase already
  has a C89 lint, so the infrastructure is there.
