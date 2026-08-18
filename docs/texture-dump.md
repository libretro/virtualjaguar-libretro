# Texture dump mode (issue #369, deliverable 1 of 2)

Design spec for `virtualjaguar_texture_dump`: write every unique blit
source tile a title uses to disk as PNG + manifest, so pack authors have
something to redraw and developers get a window into what titles
actually blit. This is the first half of #369; the replacement pipeline
(second half, v3.5) consumes the contract defined here.

Status: **design approved pending review — not implemented.**

## Goals and non-goals

Goals (v3.4.0):

- Dump each unique blit source exactly once per session as a viewable
  PNG, plus an append-only manifest row.
- A **stable identity contract**: the hash a tile dumps under in v3.4.0
  is the key the v3.5 replacement pipeline looks up, and the filename
  community authors save redrawn art under. Stable from day one is the
  target; the manifest carries a `texdump v1` version marker so that if
  a re-key is ever forced, v2 is visible rather than silent.
- Pace-neutral and inert by construction: zero work when disabled
  beyond one branch; when enabled, zero effect on the emulated machine
  (provable via savestate digests).

Non-goals (v3.4.0):

- No replacement/lookup path (v3.5; >1x replacements ride the Stage 2
  shadow surface, #367).
- No capture of OP-only sprites or GPU-computed surfaces (Doom's
  texture mapper). #369 scopes the HD-pack flow to the blitter; art
  that never passes through the blitter as a source is invisible here.
- No pack file-format spec beyond directory layout + hash filename +
  manifest columns. That minimal surface *is* the seed of the pack
  format; v3.5 extends it, never rewrites it.

## Capture approach: register-described window at launch

Hook once at the shared blit-launch site (beside `BlitMemoLaunch`,
before fast/accurate engine dispatch). When the option is on and the
blit reads source data (`SRCEN`), walk the source rectangle **as
described by the blit registers** (source channel base, flags, pixel
size, pitch/width, `B_COUNT`) directly out of emulated memory at launch
time. Hash it, dedupe, and on first sight render + write the PNG and
manifest row.

Why register-described rather than tapping the engines' inner loops:

- **Pace-neutral by construction.** All work is host-side at launch;
  no per-pixel instrumentation in any engine; the bus model never sees
  a cycle of it.
- **Engine-independent identity.** Fast and accurate blitters have
  known read-pattern divergences (e.g. A2_PIXEL writeback). A hash
  derived from the register description is identical under both
  engines — a property the test suite asserts, and the reason a
  day-one stable contract is defensible at all.
- Exotic blits (scaled, phrase-alignment edge cases) may read slightly
  different bytes than the described window. For identity that is
  acceptable: the key must be deterministic and collision-free, not a
  byte-exact replay of the engine's read stream.

Source-channel selection (whether A1 or A2 is the reader) follows the
same B_CMD decode the engines themselves use, computed once at launch.
Implementation must verify the decode against `docs/jtrm-blitter.md`
(address generators, B_CMD bits) — not against source-code comments.

Skipped (never hashed, never dumped): blits without `SRCEN`
(pattern/Gouraud fills), described source windows larger than 1 MB
(garbage-register guard), and blits whose described window falls
outside populated address space.

## The identity contract (what feeds the hash)

Key = **FNV-1a 64** over a canonical little-endian serialization:

```
'VJTD' | version=1 | src_bpp | width(px,u16) | height(rows,u16)
      | source bytes, row-major, packed exactly as stored in memory
```

- `src_bpp` ∈ {1, 2, 4, 8, 16, 32} from the source channel's pixel-size
  field.
- Source bytes are the raw memory contents — index values for ≤8bpp,
  raw 16/32-bit pixel values otherwise — in stored byte order.
- Filename: `<hash as 16 lower-case hex digits>.png`.

**The palette is deliberately NOT part of the key.** This diverges from
SNES/N64 HD-pack precedent (Mesen, Rice hash bytes+palette) for a
hardware reason: those systems replace at the render-to-screen stage,
where the palette is in hand. Our replacement happens at **blit time**,
and the Jaguar blitter never sees a palette — CLUT lookup is an Object
Processor display-time concept. At replacement time the pipeline has
only the source bytes; two CLUT variants of one indexed tile are
*indistinguishable at the swap point*. Keying dumps on bytes+palette
would therefore mint keys the replacement pipeline can never
discriminate, stranding every pack drawn against them. Bytes-only keys
are the honest contract. Palette information is advisory metadata
(below), never identity.

Collision risk: a title's unique-tile population is thousands to tens
of thousands; birthday bound at 2^64 is negligible. FNV-1a 64 is
already this repo's digest idiom (savestate digests, `frame_hash_ab`).

## Rendering (advisory, not identity)

The PNG a hash dumps as is a *preview* for the author's benefit; the
bytes are the contract.

- **≤8bpp (indexed):** rendered through a snapshot of the relevant TOM
  CLUT slice taken at first-sight launch. The CLUT CRC is recorded in
  the manifest. If the same key is later seen under a different CLUT,
  no new PNG is written (same key, one file); the manifest gains an
  additional `clut=` sighting row. Authors of palette-animated art get
  the first palette as their preview and the manifest tells them
  others exist.
- **16bpp:** the blitter cannot know whether values are CRY or RGB16 —
  that is also display-time interpretation. Option
  `virtualjaguar_texdump_16bpp` = `cry` (default) | `rgb` | `both`
  chooses the preview rendering; `both` writes `<hash>-cry.png` and
  `<hash>-rgb.png`. CRY conversion reuses the existing tables in
  `src/tom/tom.c` — no second implementation.
- **32bpp:** direct RGB render.
- PNGs are fully opaque. Transparency on the Jaguar is a *blit*
  property (`TRANSEN`/`DCOMPEN`), not a tile property; observed flags
  are recorded per manifest row instead.

## PNG encoder: in-tree miniz, one build define

No new vendored code. miniz 3.1.1 (already vendored for CHD in
`deps/libchdr`) ships `tdefl_write_image_to_png_file_in_memory_ex()`;
our unity TU currently disables the compressor with
`MINIZ_NO_DEFLATE_APIS`, and upstream wrapped every disable in
`#ifndef` guards precisely so dependents can restore them. Adding
`-DMINIZ_DEFLATE_APIS` to the existing `unity.o` rule enables the
encoder on every one of the 30+ targets, MSVC included, because miniz
already compiles on all of them today. Cost ≈ 10–20 KB on a ~1.9 MB
dylib. Encode happens at most once per unique hash; the in-memory PNG
is written through the libretro VFS (`rfopen`/`rfwrite`), same as every
other file this core touches.

Rejected alternatives: hand-rolled writer (new code to maintain vs one
define); native platform encoders (ImageIO/WIC/libpng would be a
per-platform matrix across a 16-target release CI, and
consoles/Emscripten need the portable path anyway, so both would end up
shipped).

## On-disk layout

```
<system_dir>/vj_texdump/<CRC32 8-hex>/
    manifest.tsv
    <hash16>.png
    <hash16>-cry.png / <hash16>-rgb.png     (16bpp 'both' mode only)
```

- Directory named by cartridge CRC32 only (same identity `titledb`
  keys on); the human-readable title lives in the manifest header, so
  no filename sanitization across platforms.
- `manifest.tsv`, append-only, tab-separated:
  - header: `# texdump v1 <tab> crc=<CRC32> <tab> title=<name>`
  - first-sight row per hash:
    `hash <tab> WxH <tab> bpp <tab> frame=<n> <tab> src=<addr> <tab>
    clut=<crc|-> <tab> flags=<transen,dcompen|->`
  - additional-palette sighting: `hash <tab> clut=<crc> <tab> frame=<n>`
- Counts/statistics are deliberately not tracked (would force manifest
  rewrites); a summary line goes to the frontend log at unload.

## Runtime behaviour and bounds

- Core options: `virtualjaguar_texture_dump` (disabled default,
  runtime-toggleable — capture is passive, no restart needed) and
  `virtualjaguar_texdump_16bpp`, the latter visible only while dump is
  enabled (same visibility machinery as `virtualjaguar_bios_type`).
- Dedupe set: open-addressed uint64 table, fixed 2^17 entries (1 MB
  host RAM, allocated on first enable, freed at `retro_deinit`). On
  overflow: warn once to the log, stop recording new tiles, keep
  running.
- All on the emulation thread; no locks, no queues. Worst realistic
  first-frames burst (a title blitting hundreds of unique tiles at
  boot) costs hashing + a few hundred small PNG encodes spread over
  the frames where each is first seen — a dev-facing feature is
  allowed a one-time hitch, and dedupe makes steady state near-free.
- Savestate: **zero fields, no version bump.** All dump state is
  host-transient; loading a state mid-session merely re-encounters
  hashes the dedupe set absorbs. (Lesson of #400: enhancement-path
  state outside the blob is the savestate blind spot — the design
  answer here is to *have no state the machine can see*.)
- iOS/static reset: `retro_deinit` frees the set, closes the manifest,
  resets every static (no dlclose on iOS).

## Test gates (all in `make test`)

1. **Contract freeze (determinism):** `test/roms/yarc.j64` (committed,
   public), 600 frames, dump on → the set of manifest hashes equals a
   committed golden list. Two consecutive runs produce identical sets.
   This is the CI tripwire that makes "stable from day one" real: any
   change that moves a hash fails loudly and forces a deliberate v2.
2. **Engine independence:** the same run under
   `virtualjaguar_blitter=fast` vs accurate yields identical hash
   sets. This asserts the property that justifies the contract; if it
   ever fails it is a real divergence finding, not a test to relax.
3. **Inertness, off:** dump disabled → per-frame framebuffer hashes
   identical to baseline (existing `frame_hash_ab` pattern).
4. **Inertness, ON (the strong claim):** dump *enabled* → per-frame
   framebuffer hashes AND savestate digests at frames 300/600 identical
   to disabled (`hires_state_digest` pattern: proves the emulated
   machine cannot observe the feature).
5. **PNG validity:** parse an emitted file's signature, IHDR, and chunk
   CRCs in the test harness (chunk CRC-32 verified against
   `src/core/crc32.c` — no external decoder dependency).
6. `scripts/c89-lint.sh` on every new file; new sources added to the
   MSVC compile list in `c-cpp.yml`.

## File plan

- `src/tom/texdump.c` / `.h` — capture, hash, dedupe, CLUT snapshot,
  render, manifest, miniz PNG write. (~350 lines.)
- `src/tom/blitter_mmio.c` (or the shared launch site) — one call
  beside `BlitMemoLaunch`.
- `libretro.c` / `libretro_core_options.h` — options + visibility.
- `Makefile.common`, `Makefile` (`-DMINIZ_DEFLATE_APIS` on `unity.o`),
  `.github/workflows/c-cpp.yml` MSVC list, `exports.list` additions
  under `TEST_EXPORTS` for the probes.
- `test/tools/test_texdump.c` + `test/expected/texdump_yarc.txt`.
- `docs/texture-dump.md` — this document, plus an authoring section
  (directory layout, what the hash means, palette caveats) written at
  implementation time.

## Open items carried into implementation

- Verify source-channel B_CMD decode against `docs/jtrm-blitter.md`
  before trusting the engines' variable naming.
- Confirm `tdefl_write_image_to_png_file_in_memory_ex` links cleanly
  from outside the unity TU on MSVC (symbol visibility) — fallback is
  a tiny TU-local wrapper compiled into unity.
- Golden-list size for yarc.j64 unknown until first run; if yarc blits
  too few unique sources to be a meaningful tripwire, add a second
  committed ROM from `test/roms/`.
