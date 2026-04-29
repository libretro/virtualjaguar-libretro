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
- `test_hle_bios` now covers HLE workspace state, exception vectors, I2S
  defaults, JERRY JINTCTRL decode, deferred geometry updates, PAL timing, and
  custom `VP` rollover.
- `make test` now includes event queue coverage for zero/negative-time event
  handling.

## High Priority

- `src/tom/op.c`: replace the Object Processor runaway-list guard with a real
  resumable scheduler. The current `OP_RUNAWAY_GUARD_OBJECTS` limit prevents
  malformed lists from hanging the emulator, but it does not model OP cycle
  consumption or overloaded-list suspend/reentry timing.
- `src/tom/op.c`: audit scaled bitmap clipping and horizontal remainder logic.
  Multiple comments note that edge clipping, `firstPix`, `iwidth == 0`, and
  scaled phrase alignment are guesses that affect road/ground rendering.
- `src/tom/tom.c`: validate VMODE writes and Object Processor start behavior.
  The code currently starts OP side effects on VMODE writes with comments
  questioning whether VIDEN should gate this.

## Medium Priority

- `src/tom/tom.c`: replace hard-coded visible-window constants with values
  derived from TOM timing registers where possible. This should be tested
  against NTSC, PAL, and games that reprogram HDB/HDE/VDB/VDE.
- `src/tom/op.c`: document or test the transparent index / encoded black
  behavior in 4/8 BPP paths. Comments disagree about whether index 0 is
  transparent in all relevant modes.
- `src/jerry/dac.c`: finish SSI/I2S default modeling. HLE sets internal state
  for `SCLK`/`SMODE`, but the read path still reports placeholder values for
  several DAC/I2S registers.

## Lower Priority / CD Branch

- `src/cd/cdintf.c`: many CD interface methods remain stubs.
- `src/cd/cdrom.c`: FIFO, I2S-to-JERRY, DSA command timing, and interrupt
  generation have several comments marked as approximate or hard-coded.
- `src/core/filedb.c`: cartridge/file-type detection still has known edge
  cases noted by inline comments.
