# True-color internal rendering — shadow precision framebuffer

**Date:** 2026-08-07
**Epic:** #338 (enhancement suite), track 3 ("true-color internal rendering")
**Status:** design approved in-session; awaiting spec review

## Problem

Jaguar 3D titles (Battlemorph, Iron Soldier 2, Checkered Flag) show visible
banding in gouraud-shaded surfaces. The precision loss happens at **blitter
write time**, not at the output stage: the blitter's gouraud iterator carries
intensity with 16 fraction bits (`gd_i`), then discards them writing
`(gd_c << 8) | (gd_i >> 16)` as a 16-bit CRY pixel into the game's framebuffer
in main RAM. TOM's `CRY16ToRGB32` LUT at scanline time multiplies 4+4-bit
chroma by the surviving 8-bit intensity — the fraction is gone. Output-stage
filtering (BigPEmu's approach, RetroArch debanding shaders) can only smooth
already-quantized data.

## Goal

Render gouraud-shaded pixels to the host framebuffer at full precision
(chroma × 24-bit intensity) while the game-visible 16-bit framebuffer stays
**bit-identical** to stock. Default off. Pace-neutral by construction (no
timing code touched).

## Non-goals (v1)

- RGB16 (5-6-5) and 24bpp/mixed-mode rendering — stay stock.
- Chroma interpolation — CRY gouraud interpolates intensity only; chroma
  stays 4+4.
- Performance optimization — correctness first (user decision); benchmark
  recorded informationally, no gate.
- Hi-res / Nx — this is deliberately the 1x prototype of the shadow-buffer
  architecture that track 1 (shadow hi-res) will extend.

## Architecture (approach A: shadow precision framebuffer)

Two arrays mirror main RAM's 1M 16-bit pixel words, allocated lazily only
when the core option is on (~8 MB total):

- `shadowRGB[1M]` — packed RGB888 result of the full-precision conversion.
- `shadowTag[1M]` — the 16-bit stock value the entry was derived from, plus
  a valid bit (so zero-init never false-matches RAM zeros).

**Coherence is by value-check at read time, not invalidation hooks.** A
reader compares RAM's current 16-bit value with the entry's tag; mismatch →
entry is stale → fall back to the stock LUT. No CPU/GPU/DSP/OP write path is
touched, which keeps the stock pipeline provably intact and makes the
bit-identity guarantee structural.

False-positive staleness (RAM coincidentally equals a stale tag) paints a
plausible full-precision pixel — cosmetic, self-healing on the next write.

## Components and data flow

1. **Blit time** (`src/tom/blitter.c`): the gouraud (`GOURD`/`GOURZ`) and
   intensity-shade write paths, when the option is on, additionally compute
   `rgb888 = cry_to_rgb_full(gd_c[i], gd_i[i])` (chroma tables × 24-bit
   intensity) and call `shadow_store(dstAddr, stock16, rgb888)`. The stock
   16-bit write is unchanged.
2. **Scanline time** (`src/tom/op.c`): the OP's 16bpp bitmap paths (scaled
   and unscaled), which read framebuffer phrases from RAM and write pixels
   into the line buffer at `tomRam8[0x1800 + …]`, also resolve
   `shadow_lookup(srcAddr, value16)` and write the result (hit → shadow
   RGB888; miss → `CRY16ToRGB32[value16]`) into a parallel **shadow line
   buffer** at the same line-buffer offset. Every other OP write site (CLUT,
   BLEND_CR/BLEND_Y paths) stores the stock LUT conversion of the 16-bit
   value it just wrote, so the shadow line buffer is always fully populated.
   The per-line background fill / line-buffer clear that runs before object
   processing initializes the shadow line buffer the same way (stock LUT of
   the background value), so pixels no object touches render correctly.
3. **Output** (`src/tom/tom.c`): `tom_render_16bpp_cry_scanline` copies the
   shadow line buffer to the host backbuffer instead of running the LUT.
4. **Module**: new `src/tom/shadowfb.{c,h}` owns the arrays,
   alloc/free/clear, and the store/lookup inlines. C89, lint-clean.
5. **Option**: `virtualjaguar_true_color` in `libretro_core_options.h`,
   default `disabled`; runtime toggle allocates/clears or frees.

## Lifecycle / edge cases

- Shadow is a derived cache: never savestated; cleared on savestate load and
  option toggle; freed and statics reset in `retro_deinit` (iOS static-reset
  rule — cores can't dlclose there).
- Allocation failure: log a warning, feature stays off, core runs stock.
- Pixels from non-gouraud blits, GPU stores, or the 68K have no shadow entry
  and render stock. Mixed-precision frames are fine — stock is banded, not
  wrong.

## Testing / validation

- **Identity gate (OFF)**: `frame_hash_ab` across the A/B corpus must be
  bit-identical to develop (same methodology that validated #337).
- **Effect evidence (ON)**: unique-color count on shaded scenes
  (Battlemorph, IS2, Checkered Flag) rises substantially vs stock;
  screenshots via the visual-verify tooling; RetroArch check on a real game
  (house rule — headless can't judge "looks right").
- Full `make TEST_EXPORTS=1 test`, `bash scripts/c89-lint.sh` on new/touched
  files, `make benchmark` recorded.
- Pace-neutrality: structural (no timing code), spot-checked against the
  BM/Doom calibration docs.

## Strategic sequencing (user directive, 2026-08-07)

The next track after this one must be something that **proves the core is
better than BigPEmu for most users most of the time**. Assessment:

- **Shadow hi-res blitter upscaling** (epic track 1) is the visible headline
  BigPEmu structurally lacks (it filters at the output stage only). This
  spec's shadow buffer, value-check coherence, and shadow line buffer are
  its 1x prototype — hi-res generalizes the same interfaces to Nx.
- **Ecosystem moat + "it just works"**: RetroAchievements, runahead-grade
  determinism (already banked), every RetroArch platform, plus the
  per-title enhancement defaults DB (track 4) so users never hand-tune.
- **Compatibility breadth** is BigPEmu's actual crown; a corpus-driven
  boot/compat audit against its title list should become its own epic track
  feeding the per-title DB.

Recommendation: true-color (this spec) → shadow hi-res (headline) with the
compat audit running as a parallel background track.
