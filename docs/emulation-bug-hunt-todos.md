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
- `test_hle_bios` now covers HLE workspace state, exception vectors, I2S
  defaults, PAL timing, and custom `VP` rollover.
- `make test` now includes event queue coverage for zero/negative-time event
  handling.

## High Priority

- `src/tom/op.c`: replace the fixed `opCyclesToRun = 30000` budget with a
  resumable Object Processor scheduler. Current comments explicitly call this
  value arbitrary and warn that overloaded OP lists should suspend/reenter.
- `src/tom/op.c`: audit scaled bitmap clipping and horizontal remainder logic.
  Multiple comments note that edge clipping, `firstPix`, `iwidth == 0`, and
  scaled phrase alignment are guesses that affect road/ground rendering.
- `src/jerry/jerry.c`: verify JERRY interrupt register behavior around
  `$F10020-$F10021`. Existing comments say the masking/latch handling is
  incomplete and may expose the wrong bits.
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
