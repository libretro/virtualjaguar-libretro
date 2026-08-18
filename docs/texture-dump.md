# Texture dump + replacement (issue #369)

Design spec for `virtualjaguar_texture_dump` (deliverable 1: write
every unique blit source tile a title uses to disk as PNG + manifest,
so pack authors have something to redraw) and
`virtualjaguar_texture_replace` (deliverable 2: present pack art in
place of those tiles).  The replacement pipeline consumes exactly the
identity contract dump mode freezes — see "Replacement pipeline" below.

Status: **both implemented.**  Dump: capture module `src/tom/texdump.c`,
hook in `src/tom/blitter_mmio.c`, options in `libretro.c` /
`libretro_core_options.h`, test gates in `test/tools/test_texdump.c` +
`test/expected/texdump_yarc.txt`.  Replacement: `src/tom/texreplace.c`
(+ `ShadowFBStoreRGB` in `src/tom/shadowfb.c`), gates in
`test/tools/test_texreplace.c`.

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

## Authoring guide (for pack authors)

Turn on **Texture Dump Mode** (Options → Diagnostics) and play. Every
unique source tile the game pushes through the blitter lands in

```
<RetroArch system dir>/vj_texdump/<cart CRC32 as 8 hex digits>/
    manifest.tsv         one row per unique tile (+ palette sightings)
    <hash16>.png         preview, named by the tile's identity hash
```

What to know before redrawing:

- **The filename is the contract.** `<hash16>` is the FNV-1a 64 key of
  the tile's raw bytes (plus bpp and dimensions). The v3.5 replacement
  pipeline will look art up under exactly this name — never rename the
  files. If the manifest header ever says something other than
  `texdump v1`, hashes from a different contract version are not
  interchangeable.
- **The PNG is a preview, not the truth.** For indexed (≤8bpp) tiles it
  was rendered through whatever CLUT the game had loaded the first time
  the tile was seen; the manifest row records that CLUT's CRC. A tile
  the game recolours via palette swaps appears ONCE (one hash, one
  file) with extra `clut=` sighting rows telling you other palettes
  exist. That is deliberate: the blitter never sees a palette, so two
  CLUT variants of one tile are indistinguishable at the point where
  replacement will happen. Draw for the tile, not for one palette.
- **16bpp tiles** can be CRY or RGB16 — the hardware doesn't say. If a
  preview looks like noise, switch *Texture Dump: 16bpp Preview* to the
  other interpretation (or `both`) and reload; the hash (and therefore
  the filename) does not change.
- Transparency is a property of each blit, not of the tile; `flags=`
  in the manifest records the comparator bits seen on first sight.
  Previews are fully opaque.
- The manifest is append-only across sessions; play more of the game
  and new tiles simply append. Dedupe is per-session, so a replayed
  session may re-append rows for tiles already listed — rows are
  advisory, files are identity.

## Replacement pipeline (deliverable 2 of 2)

`virtualjaguar_texture_replace` presents pack art in place of dumped
tiles.  Module: `src/tom/texreplace.c`; hooks around the engine
dispatch in `src/tom/blitter_mmio.c`; presentation via
`ShadowFBStoreRGB` in `src/tom/shadowfb.c`.

### Pack layout

```
<system_dir>/vj_texpacks/<CRC32 8-hex>/<hash16>.png
```

Mirrors the dump layout, so an author's workflow is: dump, redraw,
move one directory over.  Only files named exactly `<16 hex>.png` are
read; anything else in the directory (manifest copies, notes, PSDs) is
ignored.  Accepted PNGs: 8-bit depth, non-interlaced, color types
0/2/3/4/6 (gray, RGB, indexed, gray+alpha, RGBA).  The whole pack is
decoded ONCE into a host-side hash→pixels map — at content load, or on
first enable — never at blit time.

### Architecture: host-only, presentation-riding

The pipeline never touches the emulated machine.  Not "restores it
afterwards" — never touches it:

1. At blit launch (pre-dispatch) the SOURCE window is hashed with the
   same shared helpers dump mode uses (`TexDumpDescribe` /
   `TexDumpSerialize` / `TexDumpHashKey` — one implementation, frozen
   contract).  A map hit with matching dimensions arms the launch, and
   the DESTINATION window is described before the engines write back
   the pointer registers.
2. The blit dispatches completely stock.
3. Post-dispatch, the destination window is walked host-side: every
   pixel whose RAM word now equals the source pixel captured
   pre-dispatch (the **per-pixel straight-copy witness**) gets its pack
   RGB888 stored into the true-color shadow framebuffer, tagged with
   that RAM value.  The OP's existing shadow presentation path
   (`ShadowFBLineFromRAM` → the CRY scanline renderer) then presents
   the pack art; the value-check makes every entry self-invalidating
   when RAM moves on.

The witness makes wrong models harmless: transparent (BCOMPEN /
DCOMPEN) pixels, shaded or Gouraud outputs, exotic step patterns —
anything whose destination value is not the source value — simply
never stores, and the stock pixel presents.

Consequences, all load-bearing:

- **Zero savestate fields, zero RAM writes, zero bus-model time.**
  Savestates, rewind, run-ahead and netplay see a bit-identical
  machine with or without a pack; only the presented frame differs.
  `test/tools/test_texreplace.c` asserts framebuffer hashes, savestate
  digests AND battery destination RAM are identical to
  replacement-off, while the shadow surface provably carries the pack
  art.
- **Packs are true-color.**  Replacement RGB is presented through the
  shadow surface, so there is no RGB→CRY quantization anywhere in the
  pipeline.  Alpha < 128 in a pack pixel means "keep the stock pixel"
  (transparency at blit time is still the game's business).
- **The shadow surface is forced on** while a pack is active, but the
  Gouraud-precision stores (the True Color feature proper) stay gated
  on `virtualjaguar_true_color` via the separate `shadowFBPrecision`
  flag — a replacement-only activation leaves every non-replaced pixel
  bit-identical to stock.
- The option is **visible only when a pack directory exists** for the
  loaded content (same `SET_CORE_OPTIONS_DISPLAY` machinery as the
  16bpp-preview knob), and defaults to disabled.

### Tier status and limits

- **Tier 1 (shipped): 16bpp source tiles, 1x, straight-copy blits** —
  the sprite/UI blit class dump mode was built for.  Presentation
  requires the CRY 16bpp OP path (the dominant framebuffer case);
  RGB16-mode displays and GPU-computed surfaces (Doom's texture
  mapper) are out of scope, same as dump mode.
- **Known coherence limit:** a replacement entry's RGB is pack art,
  not a function of the tagged value.  A later non-blit write of the
  *same* 16-bit value to a replaced address keeps presenting pack RGB
  until the next store to that word.  Stock true-color has the same
  tag scheme but is immune by construction (its RGB is derived from
  the value); for replacement this is a documented cosmetic edge.
- **Interaction with internal resolution:** at Nx the hi-res shadow
  surface wins wherever its entries hit, so replaced tiles may present
  stock there.  >1x replacements riding the Stage 2 surface are the
  planned fix (tier 3, design notes on #369).
- **Indexed (≤8bpp) sources (tier 2, not yet shipped):** the pipeline
  cannot inject RGB into an indexed blit — the destination holds
  palette indices and the CLUT is applied per-object at OP render
  time.  The planned design (notes on #369) keeps the host-only
  architecture: a byte-granular index shadow filled at the same
  post-blit witness point, resolved at the OP's indexed line-buffer
  write sites through the object's own palette base, presenting via
  the same shadow line buffer.  Authors will supply index-map PNGs
  (gray or indexed color type; the sample value IS the Jaguar palette
  index), which keeps palette-swapped sprites faithful.  Until it
  ships, indexed pack entries load but never present (counted in the
  session summary).

### Test gates (all in `make test`)

`test/tools/test_texreplace.c`, on `test/roms/yarc.j64` plus a
scripted 9-tile blit battery (16bpp A2- and A1-sourced, a 16×16, a
dimension-mismatched entry, an RGBA entry with alpha holes, an 8bpp
and a 32bpp tier-skip entry):

1. `nopack_inert` — enabled with no pack ≡ disabled (framebuffer,
   savestate digests, battery destination RAM, gate off).
2. `pack_machine_inert` — enabled WITH a pack: the machine is still
   bit-identical to disabled.
3. `pack_presents` — every replaced destination word resolves
   (value-tagged) to the pack pixel's RGB; mismatched/holed/other-tier
   entries produce exactly no stores.
4. `presentation_path` — `ShadowFBLineFromRAM` lands pack RGB in the
   line buffer the scanline renderer reads.
5. `determinism` — two identical pack runs agree on everything.

The synthetic pack is built from FIRST PRINCIPLES: the test computes
each tile's hash from the documented contract and writes the PNGs
itself, so a lookup hit also cross-checks the frozen contract against
an independent implementation.  (It caught a real test bug on first
run: an LFU nibble of 3 is a NOT-copy, and the per-pixel witness
correctly refused to store a single pixel.)

### Authoring guide (replacement)

1. Turn on **Texture Dump Mode** and play; collect
   `<system dir>/vj_texdump/<CRC32>/`.
2. Redraw any tile you like.  Keep the dumped pixel dimensions — a
   resized PNG is skipped (one log line names the first offender).
   Draw in full RGB; alpha means "keep the original game pixel here".
3. Save under the SAME filename into
   `<system dir>/vj_texpacks/<CRC32>/`.
4. Enable **Texture Replacement** (Options → Video; it appears once
   the pack directory exists).  No restart needed.  The log reports
   `pack ...: N/M PNGs loaded` at load and a session summary at exit.

## Open items carried into implementation — resolution

- **B_CMD source-channel decode**: verified against
  `docs/jtrm-blitter.md` — bit 0 SRCEN reads source from **A2**, or
  from **A1 when DSTA2 (bit 11)** is set. Implemented exactly so in
  `TexDumpLaunch()`; the engines' variable naming agrees.
- **miniz linkage**: `tdefl_write_image_to_png_file_in_memory_ex`
  links cleanly from outside the unity TU (plain extern function, no
  visibility annotations); the CI MSVC job compiles unity.c with
  `/DMINIZ_DEFLATE_APIS` to keep cl.exe honest. The fallback wrapper
  was not needed. The define lives in `LIBCHDR_CFLAGS`
  (Makefile.common) rather than on the `unity.o` rule alone so the
  theos_ios path — which compiles unity.c without that rule — gets it
  too.
- **Golden-list population**: yarc.j64 blits only its two boot-time
  code copies through SRCEN (it renders via the OP), and so does
  jagniccc.j64 — no in-tree public ROM is a meaningful tripwire by
  itself. Instead of a second ROM, `test/tools/test_texdump.c` drives
  a **synthetic blit battery** through the real bus/launch path
  (26 scripted tiles: every bpp, both source channels, plus a
  palette-change re-blit that must not mint a new hash). The committed
  golden list (`test/expected/texdump_yarc.txt`, 28 hashes) freezes
  the ROM tiles and the battery together.
