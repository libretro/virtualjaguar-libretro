# Emulation Bug Hunt TODOs

This tracks actionable issues found while auditing old inline comments that
describe guesses, timing gaps, or known emulation shortcuts.

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
- **Skyhammer:** gameplay freezes are gone but audio is still
  clipped/over-loud (same family as Iron Soldier 2 audio).

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
