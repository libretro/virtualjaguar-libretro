# Internal-resolution upscaling — shadow hi-res framebuffer

**Date:** 2026-08-07
**Epic:** #338 (enhancement suite), track 1 ("internal resolution upscaling")
**Status:** design for review. **No code changes.** Nothing here is implemented.
**Predecessor:** `docs/true-color-shadowfb-design.md` + `src/tom/shadowfb.{c,h}`
(PR #341, merged) — the 1x prototype of this architecture.

---

## 0. Executive summary — read this before anything else

The epic framed this track by analogy to DuckStation's software-renderer
upscaling. **That analogy breaks on the Jaguar, and the break is the most
important finding in this document.**

DuckStation can upscale because the PS1 hands its rasterizer *vertex
coordinates* and the rasterizer computes edges. Run the rasterizer at Nx and
the edges are recomputed at Nx. On the Jaguar the GPU RISC computes polygon
edges **in software** and hands the blitter a per-scanline **span**:
`A1_PIXEL` start + `B_COUNT` INNER width. The blitter is a span filler, not an
edge-computing rasterizer. Running its walk at Nx replicates the same
silhouette N pixels wider — bit-for-bit identical to nearest-neighbour
upscaling.

I measured this rather than assuming it (§2). In Cybermorph gameplay,
**658,759 of 659,263 gouraud blits have OUTER == 1** (one span, one scanline),
`UPDA1F` is **never** set, and the destination walk is phrase-mode with no
consumed fraction. Checkered Flag issues 1.25M flat span fills with the same
shape and no `GOURD` at all. For those titles there is **no sub-pixel edge
information anywhere in the blitter interface** — none to recover, at any N.

What *does* carry sub-pixel information, measured in the same census:

| Information source | Where | Measured in |
|---|---|---|
| Fractional **source** walk on the inner loop (`XADDCTL=add-increment` + fractional `A1_FINC`) | blitter | Doom: 475,375 blits, **16bpp destination** |
| Fractional **source** walk on the outer loop (`UPDA1F` + fractional `A1_FSTEP`) | blitter | Wolfenstein 3D: 20,992 blits — but **non-16bpp destination**, see below |
| OP hardware scaler (`HSCALE`/`VSCALE`, 3.5 fixed point) | object processor | not yet censused (Stage 0 work) |

Those are texture walks. The source bitmap holds **more information than the
1x destination samples keep**, so sampling it at Nx recovers real detail. That
is genuine upscaling, and it is where this track should aim.

**But only one measured title is actually reachable by the design below.** The
shadow surface as specified covers 16bpp CRY destinations only (§3.3, §4).
Doom's destination is 16bpp on 684,834 of 684,834 shaded blits — it qualifies.
Wolfenstein 3D's is **not**: only 164 of 21,443 blits write a 16bpp
destination, so ~99% of its rendering goes to a CLUT surface the shadow does
not cover. Wolf3D has the sub-pixel information and cannot use it without a
CLUT-destination extension (new entry format + OP CLUT resolve, both of which
§6.5 deliberately leaves on the stock path). **Stage 2 is therefore a
one-title stage until Stage 0 finds a second beneficiary** — see R1, which is
the strategic question this document exists to surface.

**Therefore the design below is not "run the blitter at Nx".** It is:

1. an Nx shadow surface + Nx output path where **everything defaults to
   nearest-neighbour** (Stage 1, deliberately produces no visual change), then
2. **source supersampling** for the blit shapes that provably carry a fraction
   (Stage 2 — Doom class; one measured title today), then
3. **OP scaled-object supersampling** (Stage 3 — SCBITOBJ, which also helps 2D
   scaled-sprite titles).

Sub-pixel polygon edges are explicitly **out of scope forever**: there is no
information source for them. Anyone who wants smoother Cybermorph gradients
wants track 3 (true-color, already merged), not this track.

---

## 1. What the merged 1x prototype established

`src/tom/shadowfb.{c,h}` (PR #341) fixed the architectural pattern this design
must generalise, not replace:

- **Shadow arrays keyed by main-RAM word address.** `shadowRGB[1M]` +
  `shadowTag[1M]`, allocated lazily only when the option is on.
- **Coherence by value-check at read time.** `shadowTag[idx] == value16 |
  SHADOWFB_TAG_VALID`, where `value16` is the word the reader *just read from
  stock RAM*. Mismatch ⇒ stale ⇒ fall back to stock. **Zero invalidation hooks
  in any write path** — that is what makes the bit-identity guarantee
  structural rather than reviewed.
- **Tagged shadow line buffer** parallel to the OP line buffer at
  `tomRam8[0x1800]`; the CRY scanline renderer substitutes only on tag match.
  PR #341 explicitly chose tag-checking over exhaustively populating every
  writer, because the writer set is not closed (GPU and 68K can write the line
  buffer directly through TOM space).
- **Two touch points only**: blitter store sites (`blitter_generic` A1 and A2
  destination branches, plus `BlitterMidsummer2`'s gouraud dwrite — the
  accurate engine is the default), OP resolve sites (`OPProcessFixedBitmap`,
  `OPProcessScaledBitmap`).
- **Lifecycle**: derived cache. Never savestated; invalidated on state load and
  option toggle; freed with all statics reset in `retro_deinit` (iOS cannot
  dlclose cores).

All five properties carry forward unchanged. One safety *argument* does not —
see §5.4.

---

## 2. Evidence: the blit census

Method: a throwaway instrumentation patch at the single blit-launch site
(`BlitterWriteWord`, `offset & 0xFF == 0x3A`) recorded `B_CMD`, `B_COUNT`
inner/outer, `A1_FLAGS`/`A2_FLAGS` XADDCTL and pixel size, and the fractional
X parts of `A1_FPIXEL`/`A1_FSTEP`/`A1_FINC`, for every blit. Run headless via
`test/tools/frame_hash_ab` with scripted `--press` input, 2400 frames per
title. **The patch was reverted before this commit; there is no code in this
PR.** It should be re-landed as a proper `VJ_BLIT_CENSUS` probe if Stage 0
continues (see §8, R1).

| Title | Blits | GOURD | SRCSHADE | dest 16bpp | OUTER==1 | mean span | fractional walk consumed | verdict |
|---|---|---|---|---|---|---|---|---|
| Cybermorph (gameplay) | 675,517 | 659,263 | 0 | 667,246 | 98.6% | 23.9 px | **none** (phrase mode, `UPDA1F`=0, `XADDCTL=INC` on only 2,618) | integer spans → Nx = NN. Allocates the full shadow, gains nothing |
| Checkered Flag | 1,255,561 | **0** | 0 | 1,199,061 | 99.7% | 36.8 px | **none** | flat solid spans (`SRCDATA`+LFU) → Nx = NN. **Worst case: allocates the full shadow and pays the whole N² cost for zero gain** |
| Doom | 690,345 | 0 | 684,834 | **684,834** | 69% | 31 px | **A1 (source) `XADDCTL=INC` on 475,375; fractional `A1_FINC` on 689,917** | **real detail available, and destination is 16bpp — the one qualifying title** |
| Wolfenstein 3D | 21,443 | 0 | 0 | **164** | 1.2% | 286 px | `UPDA1F` on 20,992; fractional `A1_FSTEP` on 21,159; A1 `XADDCTL`=add-zero | information present but **destination is not 16bpp CRY** on ~99% of blits → **out of scope for the shadow as specified** |
| Iron Soldier (from state) | 388 / 900 frames | 0 | 0 | 0 | 0.5% | 320 px | none | blitter used only for phrase buffer copies — see R2 |
| Zool 2 (2D control) | 1,795 | 0 | 0 | 61 | 3.4% | 1106 px | none | 2D phrase copies → no benefit, as expected |
| Val d'Isere | 995 | 0 | 0 | 1 | 100% | 833 px | none | run almost certainly never left the menus — inconclusive |

Reading notes:

- Doom's blits are `DSTA2` (684,834 of 690,345): **A1 is the source, A2 the
  destination**. A1 walks with a fractional increment; A2 walks in pixel mode.
  That is a texture-mapped span with sub-texel source stepping — exactly the
  shape that supersamples.
- Wolfenstein's blits are also `DSTA2`, with A1 `XADDCTL` = add-zero and a
  fractional `A1_FSTEP` driving the **outer** loop (mean OUTER ≈ 79). That is
  the vertical wall-column scaler: one source texel column resampled down a
  destination column at a fractional rate. **But its destination is not 16bpp
  CRY** (`dst16bpp` = 164 of 21,443), so the shadow as specified never
  allocates for it. The information is there and this design cannot reach it;
  see §0 and R1.
- Cybermorph's `A1_FPIXEL` and `A1_FINC` fractions are frequently non-zero
  (511,216 / 521,553 blits) but **inert**: `XADDCTL` is phrase-mode on 670,993
  of 675,517 blits, so the fraction never reaches the address generator. Do not
  mistake a stale register for consumed information — this is precisely the
  trap the census was built to avoid.
- Blit *pixel volume* is modest: Cybermorph 10.2k, Checkered Flag 19.3k written
  pixels per frame against a 76,800-pixel screen. This matters for §7
  (performance): blitting is **not** the dominant Nx cost.
- Caveat: input was generic (`pause`/`a` presses at fixed frames), so Iron
  Soldier, Val d'Isere and any title needing menu navigation may not have
  reached representative gameplay. Those rows are marked inconclusive, not
  negative.

---

## 3. Where Nx lives

### 3.1 Mapping

The shadow is keyed by **stock main-RAM word index**, exactly as at 1x
(`idx = (addr & 0x1FFFFE) >> 1`). Each stock 16-bit pixel word owns an
**N² block** of shadow subpixels, in sub-row-major order (`sy * N + sx`).

```
stock word w  ->  shadow subpixels  sub[w * N*N + sy*N + sx]
```

Deliberately a *block* layout, not a 2-D hi-res raster. We do not know the
game's framebuffer stride — the game chooses it, it changes between titles and
between buffers, and nothing in the hardware tells us. Both consumers (the blit
walk and the OP fetch) address the framebuffer *per stock pixel*, so a
per-stock-pixel block is exactly the shape they want, and it makes the
coherence tag naturally per stock word. Cost: no 2-D neighbourhood locality, so
this layout cannot support cross-pixel filtering later. Accepted (YAGNI); if a
filter is ever wanted it goes at the output stage, not here.

### 3.2 Entry format — composes with true-color for free

```c
typedef struct { uint16_t value16; uint16_t frac16; } shadow_sub;   /* 4 bytes */
```

`value16` is the stock-format CRY pixel this subpixel would have been;
`frac16` is #341's sub-quantization intensity fraction. The renderer converts
with `ShadowFBCryRGB(value16, frac16)` when true-color is on and
`CRY16ToRGB32[value16]` when it is off. **Hi-res and true-color are orthogonal
options that compose with no extra plumbing**: hi-res is pixel *count*,
true-color is pixel *precision*, and one entry carries both.

### 3.3 Allocation — paged, lazy

A full mirror is not affordable above N=2:

| N | full 2 MB mirror (sub) | + tag (1M × 4B) | total |
|---|---|---|---|
| 2 | 16 MB | 4 MB | 20 MB |
| 3 | 36 MB | 4 MB | 40 MB |
| 4 | 64 MB | 4 MB | 68 MB |
| 8 | 256 MB | 4 MB | 260 MB |

**Rejected.** Instead: page the shadow at **8 KB of stock RAM** (4096 words) —
256 pages cover the 2 MB space. A page is allocated the first time a shadow
writer touches it.

```
page = { uint32_t tag[4096];  shadow_sub sub[4096 * N*N]; }
page bytes = 16 KB + 16 KB * N*N
```

| N | bytes/page | 38 pages (double-buffered 320×240 16bpp ≈ 300 KB) |
|---|---|---|
| 2 | 80 KB | 3.0 MB |
| 3 | 160 KB | 6.1 MB |
| 4 | 272 KB | 10.1 MB |
| 8 | 1040 KB | 38.6 MB |

Policy: hard cap on total pages (proposed default 64 MB, a core option later if
needed). On cap, **stop allocating and degrade to nearest-neighbour** for
un-shadowed pages. Never evict — eviction would produce visible detail
flicker; running out and staying NN is the calmer failure. Allocation failure
logs once and falls back exactly as `ShadowFBSetEnabled` already does.

Pages are only ever allocated for **16bpp CRY destinations**. CLUT (1/2/4/8bpp)
and 24bpp surfaces, and Z buffers, get no shadow at all (§4).

### 3.4 Coherence

Unchanged from 1x: `tag[w] == value16 | VALID` where `value16` is the stock
word the reader just read. Mismatch ⇒ the block is stale ⇒ replicate the stock
pixel N² times. **Plus one addition required by Nx** (§5.4): the tag carries a
short **frame epoch**, and an entry is trusted only if its epoch is the current
or previous frame. Games redraw the 3D view every frame, so the trusted window
costs nothing in practice and it bounds the stale-structure artifact class.

Proposed tag layout: `value16 (16) | VALID (1) | epoch (8)` in a `uint32_t`,
epoch = frame counter & 0xFF, accepted if `epoch == now || epoch == now-1`.

---

## 4. Per-mode scalability table

The single invariant that makes all of this safe:

> **The shadow never decides anything; it only carries content.**
> Every control decision a blit makes — transparency test, colour key, Z
> inhibit, clip, LFU selection, which pixels get written — is made **once, at
> 1x, from stock data**, and applied uniformly to all N² subpixels of that
> stock pixel. Only the *value* written into the subpixels may differ.

That is what keeps the shadow's geometry locked to the stock geometry. It also
means: 1x silhouettes everywhere, forever. Accepted (§0).

| Blit shape (`B_CMD`) | Nx treatment | Fidelity | Evidence |
|---|---|---|---|
| `PATDSEL` solid fill, integer walk | replicate pattern pixel N² | **NN** — no loss, no gain | Cybermorph 664,561 |
| `SRCDATA`-constant + LFU span fill | replicate N² | **NN** | Checkered Flag ~1.19M |
| `GOURD`/`GOURZ` span, phrase mode, `UPDA1F`=0 | intensity re-interpolated along the span at `B_IINC/N`; sub-rows replicated | **marginal** — finer gradient only, edges NN. True-color already does this better at 1x | Cybermorph 658,759 |
| `SRCEN` phrase copy, integer walk (sprites, buffer copies) | replicate N² | **NN** | Zool 2, Iron Soldier, Val d'Isere |
| `SRCEN`, source `XADDCTL=INC` with fractional `FINC` | **supersample source at `finc/N` per subpixel column** | **real gain** | Doom 475,375 |
| `SRCEN`, `UPDA1F` + fractional `FSTEP` outer walk, **16bpp dest** | **supersample source at `fstep/N` per sub-row** | **real gain** | no measured title yet — Wolf3D has the walk but a CLUT dest |
| same walk, **non-16bpp dest** | no shadow page | **stock** | Wolf3D 20,992 |
| `SRCSHADE` | follows its underlying copy; shade applied per subpixel | as the copy | Doom 684,834 |
| `BCOMPEN` 1bpp glyph paint | replicate N² | **NN** — a 1bpp font has nothing to recover | — |
| `DCOMPEN` / colour-key transparency | key tested **once at 1x**; block written all-or-nothing | **NN silhouette**, supersampled interior | — |
| LFU with `DSTEN` (read-modify-write) | shadow-dest ⊕ shadow-src, per subpixel (§5) | faithful | — |
| `ADDDSEL` (additive) | per subpixel, from shadow operands | faithful | — |
| `ZBUFF` / `GOURZ` compare, `DSTWRZ` | **cannot scale.** Z lives in game-visible RAM that must stay bit-identical; one compare per stock pixel, inhibit applied to the whole block | **NN** | — |
| `CLIP_A1` window clip | evaluated at 1x, whole block in or out | **NN at clip edges** | — |
| Destination not 16bpp CRY (1/2/4/8bpp CLUT, 24bpp, Z) | no shadow page at all | **stock** | — |
| Any blit while the page cap is hit | no shadow | **NN** | — |

Two rows deserve emphasis because they are where the picture can look *worse*
than a plain 2× frontend scale: `DCOMPEN` colour keys and `CLIP_A1` produce
blocky N-wide silhouettes sitting next to supersampled interiors. Doom's
sprites are colour-keyed over its supersampled walls, so this is not
hypothetical — it is R6.

---

## 5. The read-back problem

Games read their own framebuffer constantly: blit source = a previously
blitted destination, `DSTEN` read-modify-write, Z compares, effects that
re-read pixels. The rule splits on **which result is being computed**, not on
which register is involved.

### 5.1 The rule

1. **Everything that produces the stock 16-bit write reads stock RAM. Always.
   No exceptions, no options.** The stock destination bytes must be bit-
   identical to develop, and the only structural way to guarantee that is for
   the stock computation to never observe the shadow. This is non-negotiable.
2. **Everything that produces the shadow subpixels reads the shadow first,
   value-checked against the stock word the stock path just read; on tag
   mismatch, replicate the stock pixel N² times.** This applies to *both*
   operands — source (`A2`/`A1` texture) and destination (`DSTEN` for LFU /
   `ADDDSEL`).
3. **Control decisions come from step 1 only** (the §4 invariant).

### 5.2 Why it is safe

The shadow is write-only from the emulation's point of view: nothing ever
copies a shadow subpixel back into main RAM, into a blitter register, into the
line buffer's stock bytes, or into any savestated structure. The 68K, GPU, DSP
and OP cannot observe it. So a wrong shadow value can only ever be a wrong
*picture*, never wrong *emulation* — game logic, collision, and Z tests are
untouched by construction, the same argument #341 shipped.

Allowing shadow-to-shadow composition (rule 2 covering the source side too) is
what preserves detail through multi-pass rendering. A title that renders into a
scratch buffer and then blits that buffer to the front buffer would otherwise
lose every supersampled pixel at the copy. With rule 2, the copy carries the
subpixels along; the copy's *control* decisions still come from stock, so the
copy writes exactly the same pixels it always did.

### 5.3 Artifacts this produces

- **Detail popping.** Where the shadow source misses (tag mismatch, unallocated
  page, non-CRY intermediate), that region reverts to NN while neighbours stay
  supersampled. Visible as a resolution seam that can appear and disappear
  between frames.
- **1x silhouettes against supersampled interiors** (§4) — the strongest
  candidate for "this looks worse than off".
- **Stale sub-pixel structure** — §5.4.

### 5.4 A #341 safety argument that does NOT survive at Nx

PR #341's false-positive story is: RAM coincidentally equals a stale tag, so we
paint a full-precision pixel derived from the wrong write — but the result is
bounded within one stock intensity quantization step, so it is cosmetic and
self-heals on the next write.

**That bound evaporates at Nx.** A tag match on a stock word whose N² subpixels
were derived from *different geometry* (last frame's polygon edge crossing that
pixel, a different texture) renders arbitrary sub-pixel structure inside the
pixel — not a bounded colour nudge. It is a new artifact class and it needs its
own mitigation, which is why §3.4 adds the frame epoch to the tag. Do not
inherit #341's bound; it is stated here so a reviewer does not.

---

## 6. OP compositing at Nx

### 6.1 The hard constraint: the OP runs once

The OP **must be executed exactly once per Jaguar scanline**, producing N
sub-rows in one pass. It must not be run N times.

Reason, from `docs/jtrm-object-processor.md` gotcha 9: for scaled objects the
OP **writes the updated `REMAINDER` back to the object in RAM** after each
scanline. Running the OP N times would perform N writebacks and corrupt
game-visible state. The same applies to any OP side effect (`GPUOBJ` interrupt
firing, `OBF` interaction, `BRANCHOBJ` flag reads). One pass, N sub-rows.

### 6.2 Nx line buffer

The 1x design keeps `shadowLineRGB/Tag[720]` parallel to `tomRam8[0x1800]`.
At Nx this becomes `shadowLine[N][720 * N]` — N sub-rows, each N× wide, each
entry `{value16, frac16}` plus the tag. At N=4 that is 4 × 2880 entries ≈ 46 KB
of statics; fine, but it should follow the same "allocated only when enabled"
rule as the RAM shadow.

Tag semantics are unchanged and still the safety net: a sub-row entry is only
substituted when its tag matches the **stock 16-bit value actually sitting in
the stock line buffer** at the corresponding pixel. Any writer we did not
enumerate (GPU or 68K writing the line buffer through TOM space) therefore
falls back to stock automatically — the property PR #341 chose tagging for.

### 6.3 Fixed bitmaps (`OPProcessFixedBitmap`)

For each stock destination pixel the OP writes at 16bpp, resolve the source
phrase word against the RAM shadow (`ShadowFBLookup`-equivalent) and copy its
N² block into the N sub-rows at the N-wide destination slot. On miss, replicate.
This is a direct generalisation of today's `ShadowFBLineFromRAM`.

`REFLECT` mirrors the block horizontally as well as the pixel order.
`FIRSTPIX` is integer sub-phrase scrolling — nothing to recover.
`TRANS` and `INDEX` are 1x decisions (§4 invariant).

### 6.4 Scaled bitmaps (`OPProcessScaledBitmap`) — the second real win

`HSCALE`/`VSCALE` are 3.5 fixed-point (`$20` = 1.0×). When a title scales an
object up, the source bitmap is sampled at a *fractional* rate and the 1x
destination throws the fraction away — exactly the Doom/Wolf3D situation, but
in the OP and applying to **2D scaled-sprite titles too**, not just software
3D. Sampling the source at N× the rate recovers real detail.

Constraint: the sub-sample walk must use a **local copy** of the scaling
accumulator. The stock `REMAINDER` writeback to RAM must be byte-identical.

This has **not** been censused yet — §8 R1 lists the OP-side census (histogram
of `HSCALE`/`VSCALE` ≠ `$20` per title) as Stage 0 work. It is plausible this
is the single highest-value item in the whole track, because it is the only one
that helps 2D titles.

### 6.5 What stays on the stock path in v1

- **CLUT paths (1/2/4/8bpp)** — no shadow, NN. Same deliberate choice #341
  made. A CLUT index carries no sub-pixel information anyway; the only gain
  would come from scaled CLUT objects, which is a Stage-3+ extension.
- **RMW blend paths (`BLEND_CR`/`BLEND_Y`)** — no shadow, NN. Blending
  shadow-to-shadow is possible (§5 rule 2 permits it) but it needs both
  operands present and is pure scope; defer.
- **Per-line background fill** — NN.

Every one of these degrades by tag mismatch, i.e. by doing nothing, which is
the property that made the 1x version reviewable.

---

## 7. Output and subsystem interactions

### 7.1 libretro geometry — N is fixed at load time

`retro_get_system_av_info` currently advertises `max_width = 652`,
`max_height = 256`. `RETRO_ENVIRONMENT_SET_GEOMETRY` can only change base
dimensions **within** the advertised maximum; it cannot grow past it. So:

- `max_width = 652 * N`, `max_height = 256 * N`, computed at `retro_load_game`.
- **N is read once at load. Changing the option mid-game does not take effect
  until restart** (log a one-line notice). The alternative —
  `SET_SYSTEM_AV_INFO` — is serviced by many frontends with a full video
  teardown, and `libretro.c` already carries a comment about iOS Metal dropping
  the next `video_cb` on a mere `SET_GEOMETRY`. Not worth it. YAGNI.
- `videoBuffer`'s allocation (today sized for 1024×512) scales by N²: at N=4
  that is 4096×2048 XRGB8888 = 32 MB. Fold into the same cap discussion as §3.3.

### 7.2 Dynamic resolution changes

The core already changes geometry mid-game (320×240 NTSC / 320×256 PAL, and
`tomWidth` growth to 326). That path is unaffected in shape: `game_width` and
`game_height` become `tomWidth * N` / `tomHeight * N`, the pending-geometry
latch in `retro_run` still applies them before rendering, and the pitch is
derived from the already-scaled width: `game_width << 2` bytes per row
(XRGB8888).

**Aspect ratio stays 4/3.** Nx changes pixel count, not picture shape.

### 7.3 The blank-tail-row repair

`retro_run`'s repair loop (which blacks out rows between `written` and
`game_height` when TOM and the presented geometry disagree) compares row counts
in **stock** units. At Nx it must compare `written * N` against the Nx height,
or it will mis-repair — blacking out real rows or leaving stale ones. Easy to
get wrong silently; call it out in the Stage 1 review checklist.

### 7.4 Crash watchdog

`CrashDetectFrameTick(videoBuffer, game_width, game_height)` hashes ~256 sampled
pixels of the presented buffer to drive `video_stall`. It keeps working at Nx —
it is a change detector, not a fingerprint — but it must be handed the Nx
dimensions so its sampling stride is derived correctly. `video_stall`'s
semantics shift slightly (it now watches the enhanced buffer), which is
acceptable; note it in the watchdog docs when the option ships.

### 7.5 Savestates, run-ahead, rewind

- **Never serialized.** The shadow is derived. `ShadowFBInvalidate()` on state
  load extends to zeroing page tags (and, given the frame epoch, is nearly free
  anyway).
- **Invalidation cost is independent of N** — tags are per *stock* word, so
  zeroing them costs the same at N=8 as at N=1. Only derived content is lost,
  and it degrades to NN until repainted, which for a 3D title is one frame.
  This is the good news and it is worth stating loudly: run-ahead and rewind do
  **not** pay an N² re-derivation cost per state load.
- **But run-ahead does pay the per-frame enhancement cost on its discarded
  frames**, because the core cannot tell a discarded frame from a shown one.
  With run-ahead 1 that is roughly 2× the enhancement work. Document it; do not
  try to fix it in v1.
- The regression suite's determinism / frameskip / savestate / rewind rows must
  stay green with the option **on** as well as off (PR #341's CI matrix already
  runs all four).

### 7.6 Identity and pace gating

- **Identity, option OFF**: `test/tools/frame_hash_ab` per-frame hashes
  bit-identical to `develop` across the A/B corpus. Same methodology as #337
  and #341. Non-negotiable gate for every stage.
- **Identity, option ON**: frame hashes *cannot* match — the dimensions differ.
  So the ON-gate is stronger and different: **savestate digests at frames
  300/600/900 must be byte-identical between ON and OFF runs.** That proves the
  enhancement is invisible to emulation, which is the actual claim. Pair it
  with `test/tools/fb_row_digest` / `fb_ab_sweep.sh` over the stock RAM
  framebuffer for a second, independent witness.
- **Stage 1 has a unique extra gate**: with everything NN, the ON frame must be
  *exactly* an N× box replication of the OFF frame, assertable mechanically.
  That is the whole point of shipping a stage with no visual change.
- **Pace**: structural (no timing code is touched), spot-checked against
  `docs/battle-morph-pace-calibration.md` and `docs/doom-pace-calibration.md`
  using their documented `frame_hash_ab` state-arrival methodology with the
  option OFF, and the savestate-digest identity above with it ON.

### 7.7 Performance

Cost scaling, by stage of the pipeline:

| Stage | Scales as | Per-frame magnitude at 320×240 |
|---|---|---|
| Blit shadow writes | N² × **blit pixel volume** | measured 10–20k stock px/frame (§2) → 160–320k subpixels at N=4 |
| OP resolve | N² × **screen area** | 76,800 → 1.23M at N=4 |
| Scanline render | N² × **screen area** | 76,800 → 1.23M at N=4 |
| Frontend upload | N² × screen area | 300 KB → 4.8 MB at N=4 |

**The dominant cost is the OP + scanline stage, not the blitter.** This is the
opposite of the intuition the epic was written with, and it comes straight from
the census: Jaguar titles blit far fewer pixels per frame than the screen holds.
Practical consequence: optimisation effort belongs in the OP resolve and the
Nx scanline renderer, and the per-title benefit/cost ratio is roughly constant
across titles (it is screen-area driven) rather than proportional to how much
3D a title draws.

Measurement plan: `make benchmark` (same host, commit-to-commit) at each stage
for N ∈ {1(off), 2, 3, 4}, plus a Doom gameplay scene via
`test/tools/test_frame_timing --csv`, plus `docs/profiling.md`'s Instruments
flow to confirm the OP/scanline attribution above. Publish ms/frame per N in
the Stage 1 PR; that number decides whether N>2 ever ships.

---

## 8. Staged delivery plan

Each stage is independently shippable, default off, and must clear its gates
before the next begins.

### Stage 0 — finish the evidence (no product code)

Land the census as a proper env-gated probe (`VJ_BLIT_CENSUS`) plus an OP-side
counterpart (`HSCALE`/`VSCALE` histogram, SCBITOBJ counts). Run it over the
corpus with **scripted input that actually reaches gameplay** — Battlemorph
(CD), Iron Soldier 1/2, Missile Command 3D, Club Drive, Hover Strike,
Skyhammer, Tempest 2000, Rayman, Atari Karts.

Record `dst16bpp` per shape, not just the walk flags — Wolfenstein 3D is the
cautionary case (§2): the right walk on the wrong destination format is not a
beneficiary.

**Gate:** a table like §2 covering the corpus. As of this document **exactly
one measured title (Doom) qualifies**. If Stage 0 finds no second beneficiary
and no heavy OP-scaler use, the honest outcome is **not to build Stage 1 at
all** — a paged Nx surface, an Nx OP pass and an Nx scanline renderer are a
large permanent cost centre to carry for one title's walls. In that case either
extend scope to CLUT destinations first (which unlocks Wolf3D and is a
prerequisite for a second beneficiary), or close the track and redirect the
effort to track 3 (true-color, which is what Cybermorph and Checkered Flag
actually need) and track 4.

### Stage 1 — Nx plumbing, everything nearest-neighbour (N=2 only)

Paged shadow store, Nx OP resolve, Nx shadow line buffer, Nx scanline render,
geometry/output, allocation cap, lifecycle. **Every pixel NN.** Ships with no
visual change whatsoever — that is deliberate: it de-risks the plumbing with a
mechanically assertable correctness criterion.

**Proves:** allocation and cap behaviour, geometry and dynamic-res handling,
blank-tail repair at Nx, watchdog at Nx, savestate/rewind/run-ahead, the
performance floor.

**Gates:** `frame_hash_ab` identity vs develop with option OFF; savestate-digest
identity ON vs OFF; ON frame == exact 2× box replication of OFF frame; full
`make TEST_EXPORTS=1 test`; regression matrix green ON and OFF; c89-lint clean;
`make benchmark` recorded for N=2.

### Stage 2 — fractional-walk source supersampling (Doom class)

Implement both fractional shapes — source `XADDCTL=INC` with fractional `FINC`,
and `UPDA1F` with fractional `FSTEP` — but only where the destination is 16bpp
CRY. Everything else stays NN. **This is the first stage that produces a
visibly better picture, and as of today it benefits exactly one measured title
(Doom).** The `FSTEP` shape is implemented because it is cheap alongside the
`FINC` shape, not because a measured title uses it on a 16bpp destination.

**Gates:** Stage 1 gates, unchanged, plus: Doom screenshots via
`test/tools/cd_visual_verify`-style capture showing finer texture detail; stock
RAM byte-identical (savestate digest); RetroArch eyeball pass (house rule —
headless cannot judge "looks right"); benchmark delta; and the R6 uniformity
check run early, not at the end.

**Not in Stage 2:** CLUT destinations. Unlocking Wolfenstein 3D needs a
different entry format (index + palette identity rather than `{value16,
frac16}`) and a supersampled CLUT resolve in the OP, which §6.5 leaves on the
stock path. That is its own stage and its own design.

#### Triage rule — check the OP resolve hit rate BEFORE the blit shapes

**Production and delivery are separate failure points, and only delivery fails
silently.** The blitter can pass every Stage 2 gate and store every
supersampled block bit-identically, and the OP resolve can then reject 100% of
them at the value+epoch check in `shadow_hires_block()`
(`src/tom/shadowfb.c`) — putting **0.0000% supersampled pixels on screen with
no log line and no other symptom**. Nothing about the blit shapes, the gate
predicate, or the stored blocks is wrong in that state; the detail is created
and then thrown away.

The epoch half of that check is the one that fails silently and totally. A
title whose 3D engine takes more than `HIRES_EPOCH_WINDOW` presented frames
per rendered view (slow or double-buffered engines) has *every* block rejected
for age, not merely some. This has now bitten two titles, both diagnosed only
after a full investigation of the innocent half of the pipeline:

- **Doom** (~10–15 Hz double-buffered engine) — fixed by `404cb11`, window
  2 → 16.
- **Alien vs Predator** (its 3D buffer is always older than 2 presented
  frames) — same root cause, same constant; see
  `docs/avp-renderer-analysis.md` §6 for the A/B, which shows OP resolve hits
  going 42,827,520 → 0 while the blitter-side production stays bit-identical.

**So: before investigating blit shapes, the Stage 2 gate, or the census
predicate, read the resolve hit rate.** It is a permanent counter now, not
throwaway instrumentation — run with `virtualjaguar_crash_detect=verbose` and
the heartbeat prints, every 600 frames while hi-res is active:

```
[CRASH-DETECT] hires_resolve frame=2400 N=2x hits=79588699 misses=68948450 \
  (epoch=24224010 value=44388198 nopage=336242) rate=53.6% window_rate=98.2%
```

Read `window_rate` (the last 600 frames), not `rate` — the cumulative figure is
diluted by menus and boot. Healthy AvP gameplay reads 97–98%.

The **first** heartbeat of a run prints `window_rate=n/a (first window)` and
seeds the baseline instead of reporting one. Only a heartbeat takes that
baseline and only verbose runs heartbeats, so if verbose is switched on
mid-run there is nothing to subtract — a number there would be the cumulative
figure wearing a window label, which is the misreading this line exists to
prevent. The same `n/a` appears if the counters are ever seen to go backwards.
Every subsequent line carries a true window. The bucket names
each point at a different subsystem, so the line localizes the fault as well as
detecting it:

| Bucket | Meaning | Where to look |
|---|---|---|
| `epoch=` dominant, `window_rate` ≈ 0 | Entries match by value and are rejected for age only | **The silent killer.** `HIRES_EPOCH_WINDOW` vs the title's render cadence. Do not raise it reflexively: 16 is measured to saturate the benefit (`404cb11`), and widening it trades against the R3 stale-structure class (§5.4). |
| `value=` dominant | Entries exist but RAM no longer holds the value they were derived from | Normal coherence miss; also every never-written word inside an allocated page. Suspect a write path that bypasses the store site. |
| `nopage=` dominant | No shadow page for the address at all | Production never got here: the blitter store site, the Stage 2 gate, or the allocation cap (`SHADOWFB_HIRES_CAP_BYTES`, §3.3). |
| all zero while `shadowHiresActive` | No resolves attempted | The OP is not reaching the 16bpp resolve site for this title at all (§6.5 leaves several object paths on the stock path). |

The counters (`shadowHiresResolveHits` / `…MissValue` / `…MissEpoch` /
`…MissNoPage`) are also in the test ABI, so a harness can `harness_dlsym()` them
and assert on the rate directly rather than parsing the log.

### Stage 3 — OP scaled-object supersampling (SCBITOBJ)

Sub-sample the source bitmap through a **local** copy of the scaling
accumulator. Benefits scaled-sprite titles including 2D ones.

**Gates:** Stage 2 gates, plus explicit proof that `REMAINDER` writeback to RAM
is byte-identical (this is the savestate-digest gate, but call it out by name in
review), plus per-title screenshots for whichever titles Stage 0 identified.

### Stage 4 (optional) — gouraud sub-span intensity, N>2

Only if Stages 2–3 land and the cost curve permits. Marginal by §4.

### Explicitly never

Sub-pixel polygon edges (no information source). Z buffer at Nx. Writing any
shadow value back into stock RAM. Running the OP more than once per scanline.

---

## 9. Risks, ranked, each with the experiment that resolves it

**R1 — Only one measured title benefits, so the whole track may not be worth
its permanent cost.**
Measured: Cybermorph and Checkered Flag consume no sub-pixel data at all;
Wolfenstein 3D has the data but renders to a non-16bpp destination the shadow
does not cover; Doom is the sole qualifying beneficiary. A paged Nx surface, an
Nx OP pass and an Nx scanline renderer are a large permanent cost centre —
carried by *every* title, including ones that gain nothing (Checkered Flag
allocates 1.2M 16bpp-destination blits' worth of shadow for zero benefit). If
Stage 0 finds no second beneficiary, **do not build Stage 1**; either extend
scope to CLUT destinations first, or close the track. The epic's "internal
resolution upscaling" framing should be corrected publicly either way.
*Experiment:* Stage 0 corpus census with gameplay-reaching input, recording
`dst16bpp` per shape, plus the OP `HSCALE`/`VSCALE` histogram. This is the
single highest-value experiment remaining and it is now a **go/no-go for the
track**, not just for its scope.

**R2 — Iron Soldier's polygons may not go through the blitter at all.**
388 blits in 900 frames from a save state, all phrase-mode buffer copies, and
`src/tom/op.c` notes IS is the one title using 24bpp objects. If IS-class titles
rasterize with **GPU stores**, the shadow needs a GPU-store hook — a much larger
surface that #341 deliberately avoided, and one that would reopen the
"structurally untouched write paths" guarantee.
*Experiment:* instrument GPU `STOREW`/`STOREP` (and `STORE`) targeting the
active framebuffer range, on Iron Soldier 1/2 and Battlemorph; count stores per
frame against blit pixel volume. Also re-run the IS census from an in-gameplay
state to rule out "the save state was on a menu".

**R3 — Stale-tag sub-pixel structure (§5.4).**
The #341 bounded-error argument does not hold at Nx; a false tag match can paint
arbitrary structure inside a pixel.
*Experiment:* implement the frame-epoch tag from §3.4 and instrument OP resolve
with hit / miss / epoch-rejected counters over the corpus at trusted-window
sizes 1, 2 and ∞. If ∞ shows a materially higher hit rate, quantify what the
extra hits look like before widening the window.

**R4 — The N² cost sits at the OP/scanline stage and may make N>2 unshippable.**
Screen-area driven, so it hits every title equally and hits handhelds hardest.
*Experiment:* Stage 1 `make benchmark` + `test_frame_timing` at N ∈ {2,3,4} on
the standard ROM and a Doom scene, on both a desktop host and an ARM handheld
class target; publish ms/frame. Decide N>2's fate from that table, not from
preference.

**R5 — Frontend geometry churn.**
`max_width`/`max_height` are fixed at load; the dynamic-res titles are the
stress case, and `libretro.c` already documents iOS Metal dropping a frame on
`SET_GEOMETRY`.
*Experiment:* run the dynamic-res corpus (including the DEMO1B/DEMO1C traps
noted in the A/B sweep tooling) at N=2 under RetroArch GL, Vulkan and Metal;
watch for dropped frames, torn textures and wrong pitch.

**R6 — The picture can look worse where control decisions stay at 1x.**
Colour-keyed sprites and clipped edges give blocky N-wide silhouettes against
supersampled interiors. Doom has exactly this combination.
*Experiment:* Doom screenshots at N=2 and N=4 with sprites over supersampled
walls, side by side with the option off at the same frontend scale factor. If it
reads as worse, Stage 2 needs a "supersample only where the whole block is
uniform" fallback — which is a design change, so run this early in Stage 2, not
at the end.

**R7 — Unbounded page allocation.**
A title that blits across large RAM regions could allocate far more than the
38-page working set §3.3 assumes.
*Experiment:* page-count histogram over the corpus at N=2 (one counter, one log
line at exit); set the cap from the 99th percentile, not from a guess.

---

## 10. Who benefits, honestly

**Measured to benefit (Stage 2): Doom, and only Doom.** Fractional source
texture walks onto a 16bpp CRY destination, so Nx recovers real detail in walls
and floors.

**Has the information but is out of scope:** Wolfenstein 3D — the fractional
outer-loop source walk is there on 20,992 blits, but the destination is not
16bpp CRY (164 of 21,443), so the shadow never allocates. Reaching it needs a
CLUT-destination extension (§8, Stage 2 "not in").

**Measured NOT to benefit from Nx:** Cybermorph and Checkered Flag — integer
spans, no sub-pixel information; Nx is exactly nearest-neighbour. Worse, both
have 16bpp destinations, so they **allocate the full shadow and pay the whole
N² cost for nothing**. Checkered Flag is the measured worst case (1,199,061
16bpp-destination blits, zero gain). These titles are already served by the
merged true-color path, which is the right answer for their banding. This is
the most concrete argument that track 4's per-title enhancement DB is a
**prerequisite** for shipping this track, not a companion to it: without it,
enabling hi-res costs most of the corpus performance for no picture. Zool 2 and
2D sprite titles generally — integer phrase copies, nothing to recover, unless
they use the OP scaler (Stage 3).

**Unknown, pending Stage 0:** Iron Soldier 1/2 (R2), Battlemorph, Club Drive,
Hover Strike, Skyhammer, Missile Command 3D, Val d'Isere, Atari Karts, Tempest
2000, Rayman. Some of these are the corpus's most-cited 3D titles; the honest
position today is that we do not know, and Stage 0 is cheap.

**Structurally cannot benefit:** anything rendering through CLUT objects, 24bpp
objects, or Z-buffered paths (§4); anything whose picture is dominated by
1x-decided silhouettes (§5.3).

---

## 11. Open questions

0. **Is there a second qualifying title at all?** Doom is currently the only
   one (fractional source walk **and** 16bpp CRY destination). This is the
   go/no-go for the track. (R1)
1. **Is a CLUT-destination shadow worth building to unlock Wolfenstein 3D?**
   It needs a different entry format and a supersampled OP CLUT resolve, and it
   may be the cheapest route to a second beneficiary. (§0, §8)
2. **Do IS-class titles rasterize with GPU stores rather than the blitter?**
   If yes, does that warrant a GPU-store shadow hook, given it weakens the
   "no write path touched" guarantee? (R2)
3. **How much does the OP hardware scaler actually get used?** This may be the
   highest-value item and it is entirely uncensused. (§6.4)
4. **Is the frame-epoch trusted window 1 or 2 frames?** Double-buffered titles
   argue for 2; the stale-structure risk argues for 1. (R3)
5. **Should Stage 2 refuse to supersample blocks whose 1x control decision was
   non-uniform** (clip/key boundary), to avoid R6? That is a design change, so
   it needs an early answer.
6. **Does the page-block shadow layout foreclose anything we will want?** It
   rules out cross-pixel filtering in the shadow. I believe output-stage
   filtering covers every case we would want, but I am not certain.

---

## 12. Relationship to the rest of epic #338

- **Track 3 (true-color)** — merged, and this design composes with it for free
  via the `{value16, frac16}` entry (§3.2). For Cybermorph and Checkered Flag,
  true-color is the *only* one of the two that helps.
- **Track 2 (texture packs)** — depends on this track's shadow surface for
  >1x replacements. Note that §4's textured-copy row means a replacement
  texture would be the *source* of the supersampled path, which is a natural
  fit, but nothing in Stages 1–3 pre-builds for it. YAGNI.
- **Track 4 (per-title enhancement DB)** — a **prerequisite**, not a companion.
  §10 shows most of the corpus pays the full N² cost for no picture at all
  (Checkered Flag being the measured worst case). Without a per-title default
  that keeps hi-res off except where Stage 0/2 proved a benefit, shipping this
  track makes the core slower for nearly everyone and better for one title.
