# GPU compute offload via libretro hw-render — exploration

**Date:** 2026-08-07
**Epic:** #338 context; follow-up to `docs/hires-upscaling-design.md` risk **R4**
(the N² OP/scanline compositing cost, screen-area-driven, worst on handhelds).
That design is committed at `docs/hires-upscaling-design.md` (merged in
PR #347); section/risk references below (§2 census, §5.2, §7.7, R1, R4)
point into it.
**Status:** research only. **No code changes.** This document evaluates whether
the core could offload rendering stages to GPU compute through libretro's
hardware-render API, and recommends against building it now.

**Currency note (2026-08-19):** kept for the negative result — the
recommendation to skip GPU compute stands. Checked both re-open conditions
this document names against what has since shipped: the hi-res track it
was a follow-up to shipped in v3.2.0 with two measured beneficiaries at N=2
(Doom, Alien vs Predator; see `docs/hires-stage0-census.md`), not the
"second beneficiary" scenario the doc treats as a trigger for N>=4; and
v3.4.0's texture dump/replacement (`docs/texture-dump.md`, issue #369) is
explicitly 1x-only — its own non-goals defer >1x replacement (which would
ride the Stage 2 shadow surface, the condition-5 trigger here) to v3.5.
Neither condition is met; not re-checked beyond that.

**Honesty convention:** claims marked **[verified]** have a source link in §9.
Claims marked **[inference]** are reasoned from verified facts plus our own
code/measurements. Claims marked **[unknown]** are unresolved, each with a
named experiment that would resolve it.

---

## 0. Executive summary

**Recommendation: (i) — skip GPU compute for now. CPU SIMD is sufficient for
the Nx composite stage at N=2, and the hi-res track itself is still gated on
its own go/no-go (only one measured beneficiary title).** Keep option (iii)
(composite-stage compute behind optional hw-render with software fallback) on
file as the documented contingency if N≥4 ever becomes a shipping goal on
desktop. Reject (iv) (paraLLEl-style LLE blitter) outright — the Jaguar's
blitter/CPU interleaving makes the synchronization cost structurally fatal in
a way the N64 RDP's does not. Option (c) (output-stage-only compute) adds
nothing over frontend shaders and MetalFX; do not build it.

The single most decision-relevant finding is an inversion, now softened on
iOS by a maintainer correction but intact in structure: **GPU compute is
available and mature on exactly the platforms that do not need it (desktop
Vulkan), and unverified (iOS — wrappers exist, compute-class cores untested)
or fragile (Android driver matrix) on exactly the devices R4 worries about**
(iOS under Provenance: per the maintainer, Metal and Vulkan hw-render
wrappers exist — compute reachability there is testable, not absent; Android
Retroid-class: fragile drivers; iOS RetroArch: unverified MoltenVK path).
Meanwhile the Nx composite at N=2 costs roughly 4× a 320×240 lookup+store
pass per frame — order 300 K subpixel operations, single-digit milliseconds
even on a little phone core, and NEON-friendly. The problem GPU compute would
solve on handhelds is one the handheld CPUs can already absorb at the only N
we currently have evidence justifies.

---

## 1. Our pipeline today (what would move)

The core is a pure software renderer emitting XRGB8888 (`libretro.c`:
`video_cb(videoBuffer, game_width, game_height, game_width << 2)`).
Per frame, interleaved with 68K/GPU/DSP execution (`JaguarExecuteNew()` in
`src/core/jaguar.c` is event-driven per halfline):

1. **Blitter** (`src/tom/blitter.c`) rasterizes spans into a game framebuffer
   in main RAM, launched register-by-register by the GPU RISC or 68K —
   measured 280–520 blits/frame, 10–20 K written pixels/frame (blit census,
   `docs/hires-upscaling-design.md` §2).
2. **Object Processor** (`src/tom/op.c`, `OPProcessList` per even halfline)
   walks the object list in game RAM and composites objects into the TOM line
   buffer (`tomRam8[0x1800]`), one scanline at a time, *mid-frame*, at points
   interleaved with CPU writes to that same RAM.
3. **Scanline renderers** (`src/tom/tom.c`, `tom_render_16bpp_*_scanline`)
   convert the line buffer to XRGB8888 into `videoBuffer`.
4. The merged 1x shadow-fb (`src/tom/shadowfb.c`, PR #341) and the designed
   Nx generalisation hook stages 1–3 with a value-checked, write-only shadow
   surface.

The hi-res design's cost table (its §7.7) attributes the N² cost to stages
2–3 — **screen-area driven** (76,800 stock px → 1.23 M at N=4), not
blit-volume driven. That is the cost center this document asks about
offloading.

One property of the designed Nx layer matters more than everything else here:
**the shadow surface is write-only from the emulation's point of view**
(hi-res design §5.2 — nothing copies a shadow subpixel back into RAM,
registers, or savestated structures). An offload of *that layer only* would
therefore need **zero GPU→CPU readback** and could not perturb determinism by
construction. This is what makes option (a)/(iii) architecturally clean — and
also what makes option (b) categorically different and worse.

---

## 2. The API surface (research question 1)

All **[verified]** against `libretro.h` and libretro docs unless noted.

- **`RETRO_ENVIRONMENT_SET_HW_RENDER` (env 14).** The core fills a
  `retro_hw_render_callback` (fields: `context_type`, `context_reset` /
  `context_destroy` lifecycle callbacks, `get_current_framebuffer`,
  `get_proc_address`, `bottom_left_origin`, `cache_context`) and the frontend
  creates a rendering context for it. Returns `false` if the frontend cannot
  provide the requested API — that return is the software-fallback hinge.
- **Context types:** `RETRO_HW_CONTEXT_OPENGL`, `_OPENGL_CORE`, `_OPENGLES2/3`,
  `_VULKAN`, `_D3D11/12`. There is **no Metal context type** — on Apple
  platforms, hw-render cores reach the GPU only through GL (deprecated) or
  Vulkan-over-MoltenVK.
- **Rendering model:** for GL, the core renders into a frontend-provided FBO
  (`get_current_framebuffer`); for Vulkan, the **context negotiation
  interface** (`RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE`,
  env 43, experimental) lets core and frontend agree on device/extensions,
  and the core hands the frontend a `VkImage` per frame through the
  `retro_vulkan` interface. After hw-render is enabled, `retro_video_refresh_t`
  receives only **`RETRO_HW_FRAME_BUFFER_VALID`** (meaning "the frame is in
  the hw context") or `NULL` (dupe) — a software buffer pointer is no longer
  legal on that path.
- **Lifecycle:** `context_reset` fires when the context exists (all GPU
  resources must be (re)created there); `context_destroy` fires before
  teardown. On Android and on driver resets this can happen mid-session;
  cores must be able to rebuild everything. `cache_context` is a hint, not a
  guarantee.
- **`RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER` (env 56)** reports the
  frontend's active driver so a core can request the right context type
  first, instead of failing through the list.
- **Hw-render-OPTIONAL is real and proven.** Beetle PSX HW ships exactly the
  dual path we would need, in one binary: a `renderer` core option
  (`hardware`/`software`), and — quoting its docs — *"If the provided
  frontend video driver is not Vulkan or OpenGL (3.3 or higher) then the core
  will fall back to the software renderer at 1x internal resolution."*
  **[verified]** The pattern: attempt `SET_HW_RENDER` during
  `retro_load_game`; on `false`, run the software path and keep passing the
  software buffer. Caveats that come with it (also verified from the same
  docs): renderer switching **requires a core restart**, and *"when using the
  run-ahead latency reduction feature, the 'second instance' setting will
  break the hardware renderer."*

**[inference]** For us the dual path is not optional polish — it is
mandatory: the deterministic software renderer must remain the canonical
emulation path (savestate identity, RetroAchievements, runahead, headless CI
all depend on it), so any hw path would be enhancement-presentation only,
selected per session, never a replacement.

---

## 3. Precedent (research question 2)

### 3.1 paraLLEl-RDP (Themaister) — the existence proof, and its price

All **[verified]** from the parallel-rdp README and the libretro architecture
writeup (§9):

- **What it is:** bit-exact LLE N64 RDP rasterization in Vulkan compute,
  validated against Angrylion ("all 163 tests passed", exact fixed-point
  match), with internal upscaling.
- **The readback answer — our question 2's core.** N64 UMA coherence is
  handled by **importing RDRAM itself into Vulkan** via
  `VK_EXT_external_memory_host` and rendering into it as an SSBO with masked
  8/16-bit writes: *"import RDRAM from the emulator straight into Vulkan and
  render to it over the PCI-e bus."* CPU reads of GPU-rendered memory are
  then handled by a **timeline of synchronization points** the CPU waits on;
  strict mode syncs fully at `SYNC_FULL`, and the default sync mode costs
  *"~1 ms per frame"* of CPU stall. The Jet Force Gemini shadowmap case (CPU
  blurs an RDP-rendered buffer) is the documented example of why async modes
  break.
- **Hard requirements:** Vulkan 1.1 + `VK_KHR_8bit_storage` +
  `VK_KHR_16bit_storage` (mandatory), `VK_EXT_external_memory_host` for
  emulator integration; subgroup extensions recommended. The README is
  explicit about its floor: *"paraLLEl-RDP does not aim for compatibility
  with ancient hardware and drivers."*
- **Performance floor:** ~0.2 ms/frame GPU time on a GTX 1660 Ti;
  2000–5000 VI/s on mid-range desktop GPUs. But: **Intel iGPU results were
  "disappointing", underperforming multithreaded CPU rendering** due to poor
  occupancy/register spilling. So the minimum *viable* class in practice is a
  competent discrete GPU or a strong modern iGPU — not the bottom of the
  Vulkan support matrix.

**[inference]** The transferable lessons: (1) the readback problem is solved
by making GPU memory *be* the emulated RAM plus explicit CPU-visible sync
points — which is affordable only when CPU reads of GPU-written memory are
**rare and batched**; (2) even a world-expert implementation carries a
"decent GPU" floor and a per-frame sync tax; (3) it took a purpose-built
Vulkan backend (Granite) and is measured in person-years, not person-weeks.

### 3.2 The wider hw-render core experience

**[verified]** highlights from core docs and issue trackers (§9):

- **Beetle PSX HW:** dual renderer with software fallback (§2); framebuffer
  readback effects need an explicit "software framebuffer" option (accuracy
  vs perf tradeoff); savestate load under the hw renderer can need a scene
  change to refresh textures; second-instance runahead breaks the hw
  renderer.
- **Runahead × hw-render generally:** the libretro runahead guide steers
  hw-rendered cores to their software renderers; GL/Vulkan renderers are
  described as buggy with runahead.
- **Driver matrix:** paraLLEl-RDP has been broken by Intel *driver updates*
  (beta driver regressions filed against the driver, not the core) — a
  recurring class of issue a hw core inherits forever.
- **Android:** Adreno's stock Vulkan drivers historically lack
  `VK_KHR_8bit_storage`/`16bit_storage` (no Snapdragon GPU supported them
  when surveyed; recent Samsung Mali parts gained 16-bit but not 8-bit), and
  RetroArch grew a feature request specifically to load Mesa Turnip *user
  space replacement drivers* to make paraLLEl viable on Adreno. Android-
  specific performance problems "outside the control of parallel-rdp" are
  acknowledged by the author.

---

## 4. Platform support matrix — the Apple/handheld reality check (question 3)

| Platform / frontend | GL hw-render | Vulkan hw-render (compute) | Verdict for a compute path |
|---|---|---|---|
| Windows / Linux desktop RetroArch | yes | **mature** (paraLLEl proof) **[verified]** | works; also the platform that least needs it |
| macOS RetroArch | glcore available; GL deprecated by Apple **[verified]** | **yes via MoltenVK** since RetroArch 1.15.0 (default driver); Mupen64Plus-Next + paraLLEl RDP/RSP listed working on macOS **[verified]** | viable; MoltenVK ≥1.2.3 even has `VK_EXT_external_memory_host` **[verified]** |
| iOS / tvOS RetroArch | GLES ≤3.0 only (Apple never shipped ES 3.1 → **no GL compute shaders**) **[verified]** | Vulkan driver exists via MoltenVK since 1.15.0, but **paraLLEl-class compute cores on iOS are unconfirmed** — release notes confirm Mupen64Plus-Next availability, not the paraLLEl renderer **[unknown]** | unknown — resolve by testing Mupen64Plus-Next/paraLLEl on an iOS RetroArch build |
| **Provenance (iOS/tvOS)** | GLES3 hw-render via IOSurface-backed FBO (PVThinLibretroFrontend) **[verified in repo]** | **Metal and Vulkan hw-render wrappers exist** per the Provenance maintainer (2026-08-07) — this corrects an earlier draft of this table that inferred absence from the changelog alone. Compute-shader reachability through those wrappers is **untested**, not absent | **testable today in the user's own frontend** — run a Vulkan-compute core through the Provenance Vulkan wrapper and observe which path initializes |
| Android handhelds (Retroid class, Adreno) | GLES 3.1+ compute nominally present | Vulkan present but **fragile**: stock Adreno drivers missing 8/16-bit storage, Turnip-sideload culture, per-device driver roulette **[verified]** | technically possible if shaders avoid 8/16-bit storage (32-bit packing — our choice to make, at shader-complexity cost **[inference]**), but the support burden is the worst in the matrix |

**Said loudly, as instructed: the R4 devices are the hole in the matrix.**
R4's worry is handhelds. On iOS the maintainer reports Provenance exposes
Metal and Vulkan wrappers (untested for compute-class cores), and native
RetroArch defaults to Vulkan-via-MoltenVK; what remains unverified on both is
no compute-capable context whatsoever today; iOS RetroArch's
MoltenVK-compute path is unverified; Android's is driver roulette. A
Vulkan-compute composite stage would demonstrably help on desktop —
where the CPU Nx path is least likely to need help — and would be
unavailable or unreliable precisely where R4 predicts the N² cost hurts.
This inverts the naive conclusion "GPU compute fixes the handheld cost."

---

## 5. The mapping: what could move, with the math (question 4)

Frame parameters used throughout: 320×240 stock (76,800 px), 60 fps,
XRGB8888 out. Census numbers from `docs/hires-upscaling-design.md` §2.

### 5.1 Option (a) — Nx composite/scanline stage on GPU compute

**Shape.** Stock emulation (blitter, OP side effects, line-buffer stock
bytes, all RAM) stays on the CPU, byte-identical — that path is untouchable.
What moves is the *derived* Nx layer: shadow-page writes at blit time, the Nx
OP resolve, the Nx scanline render. Because that layer is write-only from
emulation's POV (§1), the GPU never has to answer a CPU read — **no
readback, no sync points on the emulation timeline** **[inference, from the
hi-res design's structural argument]**.

**Data flow.** The GPU cannot read our RAM at the *moment each blit/OP fetch
happened* (RAM has moved on by frame end), so the CPU must capture a
command+data stream per frame: per blit, the register state plus the stock
source/dest words actually read (for the value-check tags); per OP line, the
object fetches plus the stock line-buffer values. Volume estimate
**[inference]**:

- blit-side: 10–20 K written px/frame × (2 B stock value + params amortized)
  ≈ 40–100 KB/frame;
- OP/scanline-side: ≈ screen area 76.8 K px × 2 B stock + tags ≈ 200–400
  KB/frame;
- total ≈ **0.3–0.5 MB/frame ≈ 20–30 MB/s upload** — trivial against PCIe or
  UMA bandwidth. (Even naively re-uploading the whole 2 MB RAM per frame is
  only 120 MB/s.) **Bandwidth is not the obstacle.** Dirty-page upload is an
  optimization, not a requirement.

**Output.** The compute pass writes the Nx XRGB8888 frame into the
frontend's `VkImage`; the core submits `RETRO_HW_FRAME_BUFFER_VALID`. At
N=4: 1280×960×4 ≈ 4.9 MB stays GPU-side — the 4.8 MB/frame CPU→frontend
upload cost from the hi-res design's §7.7 *disappears*, a real secondary win.

**What it actually costs [inference]:** re-implementing, in compute shaders,
the blitter's fractional-walk supersampling shapes, the OP fixed/scaled
resolve, CRY→RGB conversion with the `{value16, frac16}` entry, and the tag
coherence logic; plus the streaming infra, the Vulkan (and realistically
GL-fallback) backends, context loss handling, and permanent dual-path
maintenance. Order of magnitude: **months to a year of focused work**, not
weeks — paraLLEl-RDP's much larger scope took a domain expert years, and
Beetle PSX's hw renderers remain a live maintenance stream a decade on.

### 5.2 Option (b) — the blitter itself as GPU compute (paraLLEl-style LLE)

**Rejected, with numbers.** The read-back problem is not incidental here; it
is the workload. Games read their own blitted RAM constantly: blit sources
are previous blit destinations, `DSTEN` read-modify-write reads the
destination *within* the blit, Z compares read game-visible Z RAM, and the
GPU RISC computes spans from data the blitter just wrote. Unlike the N64 —
where the RDP consumes batched command lists and CPU reads of RDP output are
rare enough that paraLLEl's per-`SYNC_FULL` waits cost ~1 ms/frame — the
Jaguar blitter is programmed register-by-register by the 68K/GPU RISC and
consumed immediately:

- Census: **280 blits/frame (Cybermorph) to 520/frame (Checkered Flag)**,
  each potentially read back before the next is programmed. Sync granularity
  is per-blit, not per-frame.
- At even an optimistic 50 µs per submit+wait GPU round trip (MoltenVK would
  be worse), 280–520 round trips = **14–26 ms/frame of pure sync overhead**
  — over budget before any shading happens. **[inference]**
- The alternative — RDRAM-style host-memory import with masked GPU writes so
  CPU reads "just work" — still requires the CPU to *wait* for each blit's
  completion before the very next 68K/GPU instruction that reads the result,
  because our emulation is sequential within a halfline. There is no slack in
  the timeline to hide GPU latency. **[inference]**
- It would also put game-visible RAM under GPU writers, dragging
  RetroAchievements reads, savestate capture, and byte-identical replay into
  the coherence problem (§6).

paraLLEl works because the N64's architecture put an asynchronous,
command-buffer-shaped boundary between CPU and rasterizer. The Jaguar put a
shared register file and a shared RAM bus there. **LLE-on-GPU does not
transplant.**

### 5.3 Option (c) — output-stage-only compute

Upscaling/filtering the finished 320×240 XRGB8888 frame in a core-side
compute pass adds **nothing** over what already exists frontend-side:
RetroArch's slang shader pipeline runs on every video driver including Metal,
and Apple platforms additionally have MetalFX upscaling at the frontend
level. A core-side output pass would carry the entire §2 lifecycle/driver
burden to duplicate a solved problem, and unlike (a) it has no access
advantage — by output time the sub-pixel information (frac16, fractional
walks) has already been consumed or lost. **If (c) is ever proposed, the
answer is "write a slang shader instead."** The only core-side input a
frontend shader cannot see is the shadow layer's extra precision — and using
it *is* option (a), not (c).

---

## 6. Determinism and subsystems (question 5)

- **Savestates / byte-identical replay.** Under (a)/(iii), all savestated
  state remains CPU-side and the shadow stays derived/never-serialized (PR
  #341 lifecycle) — savestate digests are untouched by construction, the
  same ON-vs-OFF gate the hi-res design already specifies. Under (b), GPU
  work would have to be flushed and bit-reproduced at every save — paraLLEl
  achieves bit-exactness, but at the price of full sync points; combined with
  §5.2's per-blit granularity this is another independent reason (b) fails.
- **Runahead / rewind.** RetroArch guidance: hw-rendered cores should use
  software renderers with runahead; second-instance runahead breaks hw
  renderers (Beetle docs) **[verified]**. Our runahead-grade determinism is a
  shipped feature; any session with the hw path enabled would forfeit it.
  This is acceptable only because the dual path keeps software as default —
  but it means the hw path's audience shrinks to non-runahead users.
- **RetroAchievements.** rcheevos reads guest RAM through
  `retro_get_memory`/`SET_MEMORY_MAPS` host pointers (asserted by
  `test/tools/test_memory_map.c`). Under (a)/(c) game RAM never leaves the
  CPU → **unaffected [verified for our mapping, inference for the general
  claim]**. Under (b), framebuffer/Z regions of RAM become GPU-written and
  achievement logic reading them joins the coherence problem.
- **Frame pacing.** A hw core hands the frontend a GPU-timeline frame;
  pacing now includes GPU scheduling and (on Apple) MoltenVK translation.
  paraLLEl's measured cost is ~1 ms/frame CPU stall in sync mode on desktop
  **[verified]**; our option (a) has no CPU-visible sync so the risk is
  limited to queue-submission jitter **[inference]** — real but minor, and
  irrelevant while the recommendation is not to build it.

---

## 7. Cost/benefit and verdict (question 6)

### 7.1 Grounding option (i): can the CPU just do it?

The Nx composite at N=2 is ~4× a 320×240 lookup+store pass per frame:
307,200 subpixel writes plus a tag compare and a `CRY16ToRGB32`-class lookup
each — roughly 3–4 MB/frame of memory traffic, ≈ 200–250 MB/s at 60 fps
**[inference]**. Phone-class LPDDR4X sustains tens of GB/s; even a
Cortex-A55 does this in a few ms/frame, a big core in well under 1 ms. The
work is block-replication over contiguous rows — the best possible shape for
NEON (and the repo already ships NEON blitter paths in
`src/tom/blitter_simd_neon.c`). At N=4 the stage is 1.23 M subpixels/frame
(~15 MB/frame traffic): heavy for little cores, plausible on big phone
cores, easy on desktop. **Conclusion: at N=2 — the only N with any measured
justification today — CPU SIMD is comfortably sufficient on every target
including handhelds. GPU compute becomes interesting only at N≥4, and §4
shows the devices that would need help at N≥4 are the ones without a usable
compute API.**

### 7.2 Ranked recommendation

1. **(i) Skip — recommended.** CPU NEON/SIMD for the Nx stage at N=2.
   Rationale: §7.1 feasibility; §4 inversion (compute unverified/fragile where it
   would matter); the hi-res track's own R1 gate (one measured beneficiary
   title) means we may never build the stage this would accelerate; and the
   engineering cost of the alternative is months-to-a-year plus a permanent
   driver-matrix/dual-path maintenance stream (§3.2, §5.1).
2. **(iii) Composite-stage compute behind optional hw-render with software
   fallback — contingency, not now.** Architecturally sound because the
   shadow layer needs no readback (§5.1); Beetle PSX proves the dual-path
   binary pattern (§2). Trigger conditions in §8.
3. **(c) Output-stage compute — never as core code.** Frontend slang
   shaders/MetalFX already own this stage (§5.3). Anything we would put
   there belongs either in a frontend shader preset we publish, or in (iii).
4. **(iv) Full paraLLEl-style LLE blitter — rejected.** Per-blit sync
   granularity is structurally fatal (§5.2): the Jaguar lacks the
   asynchronous command boundary that makes the N64 approach work, and it
   would drag game-visible RAM, savestates and RetroAchievements into GPU
   coherence for negative expected value.

### 7.3 What it would unlock if built anyway (honest upside of (iii))

N=4+ composite on desktop GPUs at negligible GPU cost (the workload is a
fraction of paraLLEl's 0.2 ms/frame class); removal of the N² frontend
upload (4.8 MB/frame at N=4 stays GPU-side); and a foundation that
texture-pack sampling (epic #338 track 2) could later build on. None of it reaches
handhelds under the current support matrix, and none of it matters unless
the hi-res track passes its Stage 0 go/no-go with more than one beneficiary.

---

## 8. What would change our mind

Any of the following flips the recommendation from (i) toward (iii):

1. **The hi-res track passes its R1 gate with multiple beneficiaries** and
   ships Stage 1/2, *and* Stage 1 benchmarks show N=2 misses frame budget on
   a target device class despite NEON — i.e. the CPU claim in §7.1 fails
   empirically. (`make benchmark` + `test_frame_timing` at N ∈ {2,4} on an
   ARM handheld, per the hi-res design R4 experiment.)
2. **N≥4 becomes a shipping goal on desktop** (user demand, per-title DB in
   place) — the one regime where compute is both available and clearly
   superior.
3. **A compute-class core is confirmed working through Provenance's Vulkan
   wrapper or native iOS RetroArch** (MoltenVK 1.3/1.4, 2025, brought Vulkan
   1.3/1.4 — plausibility is up), or testing
   confirms paraLLEl-class compute cores run well under iOS RetroArch's
   MoltenVK driver (the §4 **[unknown]**; the experiment is: run
   Mupen64Plus-Next with paraLLEl RDP on an iOS RetroArch build and measure).
   Apple handhelds are the largest R4 population; if compute becomes real
   there, the §4 inversion weakens materially.
4. **Android stops being driver roulette** for the Retroid class — e.g.
   RetroArch ships Turnip driver loading and it proves reliable, or stock
   Adreno drivers reach parity — *and* our shaders' 32-bit-packing
   workaround for the missing 8/16-bit storage extensions benches acceptably.
5. **Track 2 (texture packs) is greenlit at >1x** — replacement-texture
   sampling is a natively GPU-shaped workload that would amortize the (iii)
   infrastructure across two tracks.

Conversely, if the hi-res track closes at its Stage 0 gate (no second
beneficiary), this document closes with it: there is no composite stage to
offload, and no other stage survives §5.

---

## 9. Sources

Verified facts above trace to:

- [libretro.h (libretro-common)](https://github.com/libretro/libretro-common/blob/master/include/libretro.h) — `SET_HW_RENDER` (env 14), `retro_hw_render_callback`, context types, `RETRO_HW_FRAME_BUFFER_VALID`, negotiation interface (env 43), `GET_PREFERRED_HW_RENDER` (env 56), `SET_HW_SHARED_CONTEXT` (env 44)
- [libretro docs — OpenGL accelerated cores](https://docs.libretro.com/development/cores/opengl-cores/)
- [parallel-rdp README](https://github.com/Themaister/parallel-rdp/blob/master/README.md) — Vulkan 1.1 + 8/16-bit storage requirements, `VK_EXT_external_memory_host`, timeline sync design, "does not aim for compatibility with ancient hardware"
- [Reviving and rewriting paraLLEl-RDP (libretro.com)](https://www.libretro.com/index.php/reviving-and-rewriting-parallel-rdp-fast-and-accurate-low-level-n64-rdp-emulation/) — RDRAM-as-SSBO import, sync-mode ~1 ms/frame, GTX 1660 Ti 0.2 ms/frame, Intel iGPU "disappointing", Angrylion bit-exactness tests, Jet Force Gemini readback case
- [Beetle PSX HW docs](https://docs.libretro.com/library/beetle_psx_hw/) — software fallback quote, renderer-restart requirement, software-framebuffer readback option, second-instance runahead breakage
- [RetroArch runahead guide](https://docs.libretro.com/guides/runahead/) — hw-renderer guidance
- [RetroArch 1.15.0 release notes](https://www.libretro.com/index.php/retroarch-1-15-0-release/) — Vulkan-via-MoltenVK default driver on Apple platforms, macOS core list incl. Mupen64Plus-Next + paraLLEl, Metal-build GL support
- [MoltenVK 1.2.3 release (Phoronix)](https://www.phoronix.com/news/MoltenVK-1.2.3-Released) — `VK_EXT_external_memory_host` support
- [parallel-rdp issue #47 — Snapdragon/Adreno support](https://github.com/Themaister/parallel-rdp/issues/47) — missing 8/16-bit storage on Adreno, Mali/Tegra working, Android perf issues
- [RetroArch issue #18143 — Turnip driver loading for paraLLEl on Android](https://github.com/libretro/RetroArch/issues/18143)
- [RetroArch issue #17464 — second-instance runahead failure](https://github.com/libretro/RetroArch/issues/17464)
- [Provenance repository](https://github.com/Provenance-Emu/Provenance) — PVThinLibretroFrontend GLES3 hw-render via IOSurface-backed FBO (changelog); Metal + Vulkan hw-render wrappers confirmed by the maintainer 2026-08-07 (earlier draft wrongly inferred absence)
- In-repo: `docs/hires-upscaling-design.md` (blit census §2, cost table §7.7, write-only shadow argument §5.2, R4), `src/tom/tom.c`, `src/tom/op.c`, `src/tom/blitter.c`, `src/tom/shadowfb.c`, `libretro.c`
