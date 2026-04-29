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
