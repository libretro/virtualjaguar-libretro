# Virtual Jaguar libretro v3.3.0

Hi-res completion and correctness release: the internal-resolution track now
adds real detail on **every** video mode and on scaled sprite objects, not just
CRY gouraud — which is what finally makes 2D titles benefit. Alongside it, a
run of root-caused game and infrastructure fixes, several of them found by
reading the original Atari/Flare TOM netlists rather than guessing.

## Highlights

- **Hi-res for RGB16 direct mode** (#382). All hi-res detail was previously
  CRY-only: every other video mode rendered at 1x and replicated rows. Measured
  across ~3,000,000 rendered scaled calls, Val d'Isère and Primal Rage are
  **100% RGB16** — so an entire class of titles was getting pure upscaling with
  zero added detail, including titles whose blits already produced supersampled
  content that was then discarded. Alien vs Predator now shows 6.9 million
  non-uniform sub-pixel blocks where it previously had none.
- **Hi-res Stage 3: OP scaled-object supersampling** (#367). Scaled bitmap
  objects (SCBITOBJ) now carry real sub-pixel content instead of box
  replication — the piece the design calls "the second real win", and the one
  that helps 2D titles rather than only 3D ones.
- **Stage 3 extended to scaled CLUT objects** (#367 §6.5). A corpus census
  found scaled **CLUT** objects are the dominant scaled traffic in exactly the
  2D titles Stage 3 targets — 81.1% of truly-scaled destination pixels in
  International Sensible Soccer and 75.7% in Val d'Isère — so Stage 3 had been
  reaching under a quarter of the scaled content there. Promoting the 8bpp path
  takes **Val d'Isère from 0.0000% to 7.16% sub-pixel variance**: it had no
  hi-res detail at all before this.

## Bug fixes

- **Val d'Isère Skiing & Snowboarding: ground renders correctly** (#354). The
  perspective floor was a flat mottled band ("looks like a static image"). Root
  cause was the **OB register byte order**, settled from the original
  Flare/Atari TOM design netlists (`OB.NET:55-67`) after the JTRM turned out not
  to specify it: OB exposes the object phrase **least-significant-word first**
  (`OB0 $F00010` = phrase[15:0]), not straight big-endian. The game's IRQ3
  handler gates its floor renderer on `load ($F00014)` / `cmpq #0`, which under
  the old ordering returned the low long — always carrying TYPE, so never zero.
  Every one of ~72,800 interrupts fell through to a do-nothing epilogue.
- **Doom savestate rollback is deterministic again** (#400). `savestate_features
  = 3` promises a bit-exact rollback, which is what RetroArch's single-instance
  run-ahead is built on; Doom diverged at replay frame 227. Not a Doom bug — the
  hi-res shadow surface's frame epoch was outside the state blob, and its
  256-frame wrap (which clears every cached block) replayed at a different frame
  after a rollback. Now carried in the state (**v11**).
- **Cybermorph projectiles render under the accurate blitter** (#425).
- **Hi-res 2x pixel-dot noise on texture edges** (#396). Stage 2's half-step
  sub-sampling could step past the source bitmap's last pixel and line.
- **Periodic audio skip every ~36 s NTSC / 8 s PAL** (#393) — read-cursor drift
  against the I2S capture cadence.
- **Power Driv Rally sound stops mid-game** (#355).
- **Blit-memo arena-full log spam** (#411/#426) — a per-frame INFO line at frame
  rate on titles whose blit stream outruns the shadow arena.

## Enhancement suite

- Per-title enhancement defaults DB (#368) is in and applies known-safe presets
  automatically; user-set values always win.
- Soft patching for cartridges documented and its contract guarded (#409).

## Timing / accuracy

- Netlist-grounded GPU pipeline cost model, and the 68K charged real blitter bus
  time (both experimental, default off).
- The Timing core options are reorganised: the two clock-speed multipliers come
  first, the three experimental models are grouped beneath them, all three are
  marked **(Experimental)**, and the descriptions are cut to what a user needs at
  the point of toggling. Both overclock entries now point at the timing models as
  the first thing to try when an overclocked title misbehaves.

## Testing / diagnostics

- **vjtrace flight recorder** — event ring, memory watchpoints, per-frame field
  CSV, plus four standalone offline analyzers. Costs nothing in shipped builds
  and is armed on demand.
- **Blit memoization** (#411) — titledb-gated, bit-identical by construction,
  with a verify mode that never skips.
- `test_runahead_determinism` now covers a title carrying per-title enhancement
  defaults; it previously ran only stock-path titles, which is why #400 shipped.
  Its audio capture is also rewound per pass, so runs above ~300 frames no longer
  report false divergence.
- `test_frontend_pacing`'s fastest-frame bar is recalibrated off the slowest CI
  runner (#421). It was failing on roughly half of all develop pushes, on commits
  that could not affect pacing.
- `cue2cdi` converter ships as a release binary.

## Savestates

**State version 11.** States written by v3.2.0 (version 8) still load. The new
trailing field is the hi-res shadow epoch (#400).

## Stats

108 files changed, 15,993 insertions(+), 301 deletions(-) across 146 commits
since v3.2.0.

## Known issues

- Render-bound game loops still tick roughly 2x too fast in Doom and Hover
  Strike (#401): the GPU completes a frame in 1-2 fields where hardware needs
  4-8. The machine timing itself is correct; the experimental timing models are
  the current lever.
- Touchscreen taps can trip in-game auto-repeat (#399) — authentic hardware
  behaviour meeting 100-200 ms finger taps; an opt-in core option is the planned
  fix.
- Battle Morph (CD) runs too fast (#331), likely the unmodelled GPU/DSP pipeline
  hazards (#313).
- `hires_box_check` asserts the box-replication property Stages 2 and 3
  deliberately break. It is wired into neither `make test` nor CI, so nothing is
  failing, but it is misleading as written and needs a Stage-3-aware successor.

## Downloads

CI publishes 16 platform builds with the tagged release.

## Maintainers

Joe Mattiello (@JoeMatt)
