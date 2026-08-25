> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# Read-only perf audit: bus/memory-map, event scheduler, frame loop, TOM, OP

Scope: src/core/jaguar.c, src/core/event.c, src/core/bus_arbiter.{c,h}, src/core/vjag_memory.h,
src/core/perf_iface.h, src/core/crash_detect.c, src/core/vjtrace.h, src/tom/tom.c, src/tom/op.c,
src/m68000/m68kinterface.c (only the instruction-hook call site). Repo:
virtualjaguar-libretro, worktree `.claude/worktrees/alien-predator-save-state-ad621b`, branch
`perf/560-rpi-soc-tuning`. All findings are grounded in lines actually read; no edits made.

A large amount of this territory has already been investigated by concurrent/prior sessions today
(per claude-mem observations attached to these files) — #540/PR#542 (M68KInstructionHook register
capture), #520 (interpreter dispatch/context threading ruled out), #517 (fast-math FP audit),
#560/PR#562/PR#567 (RPi SIMD + per-SoC -mcpu/-O3, the branch this worktree is on). I've tried not to
re-propose that work; where I re-examined something already covered I say so and give the delta.

---

## 1. Memory dispatch (bus decode)

**Structure is already good.** Both the UAE-facing fast path (`m68k_read/write_memory_{8,16,32}`,
jaguar.c:500-921) and the general-purpose path (`JaguarRead/WriteByte/Word/Long`, jaguar.c:954-1181)
are plain if/else ladders (no jump table, no page table), but the ladders are already ordered
correctly: main RAM ($0-$1FFFFF) is checked first in every one of the 6 functions, cart ROM
($800000+) second. That's the right order — RAM and ROM dominate 68K traffic (instruction fetch +
stack + most data). No reordering opportunity found.

**Finding 1.1 — bus dispatch functions are all extern/non-static, and only one build target gets
LTO.** `JaguarReadByte/Word/Long`, `JaguarWriteByte/Word/Long` (jaguar.c:954,988,1020,1074,1122,1147)
and `m68k_read/write_memory_*` (jaguar.c:500-921) are called from every other subsystem
(src/tom/tom.c, src/tom/op.c via `OPLoadPhrase`/`OPStorePhrase`, src/tom/gpu.c, src/jerry/dsp.c,
src/jerry/jerry.c, blitter, CD/CDROM) as ordinary extern C functions. Grep of Makefile: only the
`classic_armv7_a7` target sets `-flto=4 -fwhole-program -fuse-linker-plugin` (Makefile:219,
comment at 920-924 confirms LTO measurably shrinks that target's binary, "22.7% on non-LTO ld64").
None of the `rpi*` targets (the ones just tuned in PR #567 on this very branch), the generic Linux
`unix` target, tvOS/iOS, or Android set LTO. That means on every non-classic_armv7_a7 target, the
hottest cross-cutting functions in the whole emulator — the ones every processor's every memory
access funnels through — cannot be inlined at their call sites; every access pays a real
non-inlined call (and on ARM, argument marshalling + branch-and-link + return) on top of whatever
work the function does.
- Why it costs on in-order ARM: no speculative/OoO execution to hide call latency; a mispredicted
  or even correctly-predicted BL/BLR still costs pipeline bubbles the compiler could have removed
  by inlining a 10-branch if-ladder into the caller (which itself typically already knows or can
  narrow the address range, e.g. OP phrase fetches are almost always RAM).
- Proposed change: enable `-flto` (or GCC's `-flto=N -fuse-linker-plugin`) on the rpi* targets and
  the generic unix/Linux target, matching what classic_armv7_a7 already does. Needs a full
  `make TEST_EXPORTS=1 test` + acid pass afterward (LTO can expose latent C89 UB / missing
  `extern`/visibility issues that never bit non-LTO builds) and a binary-size/build-time check.
- Impact: MED-HIGH, unsure without measurement — these are the single most-called functions in the
  core, so even a few cycles/call compounds across millions of calls/sec, but the actual win
  depends heavily on how much the compiler can already do at each call site.
- Risk: LOW-MED for the compiler flag itself (pure codegen), but MED to validate — LTO can surface
  ODR/visibility bugs that were previously masked.
- Effort: S to flip the flag, M to validate (needs the acid suite + a real RetroArch smoke test on
  device, not just host).
- Sonnet-doable for the flag flip + build verification; a human/Opus call is needed only if LTO
  regresses correctness and someone has to bisect which TU triggered it.

**Finding 1.2 — `M68K_BUS_CHARGE` / `bus_arbiter_m68k_access` per-access cost is real but opt-in
only, so default-path impact is ~zero.** jaguar.c:447-451 gates the whole dram_timing charge on
`busArbiter.enabled`, which defaults to 0 (`bus_arbiter_init`, bus_arbiter.c:65) and is only turned
on by the `virtualjaguar_dram_timing` core option. When on, every 68K access pays a real
non-inlined call into `bus_arbiter_m68k_access` (bus_arbiter.c:136-185: a handful of range
compares in `bus_arbiter_dram_cost` plus carry arithmetic) on top of the base bus dispatch. Not a
new finding to act on — already correctly gated, and the model itself is already documented as
"contention_scale... settable only via VJ_DRAM_SCALE... while the correct physical cost... is
being pinned down" (bus_arbiter.h:113-117), i.e. known WIP. No action recommended; flagging only
because the task asked for the per-access cost. Impact: LOW (opt-in, off by default). 

**Finding 1.3 — `M68KGPURAMSyncRead`/`M68KGPURAMSync` do 2 range checks on every 68K read/write.**
jaguar.c:483-498 and 671-681. Cheap (2 compares, static function, almost certainly inlined into
its 6 call sites even without LTO since it's same-TU), correctly structured to early-exit before
touching GPU/DSP state on the overwhelming majority of accesses that don't touch RISC-local RAM.
No change proposed — this is the mailbox-handshake mechanism from issues #406/#456/#138 and is
already minimal for what it does.

---

## 2. Event scheduler (src/core/event.c) — the real find in this section

**Finding 2.1 — two 32-slot linear-scan event lists, touched 3-4x per dispatched event, dispatched
~1,500-2,500+ times per field.** `EVENT_LIST_SIZE` is 32 (event.c:22), duplicated into
`eventList[32]` (EVENT_MAIN) and `eventListJERRY[32]` (EVENT_JERRY). Every iteration of
`JaguarExecuteNew`'s `do {} while(!frameDone)` loop (jaguar.c:1683-1752) calls:
  - `GetTimeToNextEvent(EVENT_MAIN)` — scans all 32 slots (event.c:172-179)
  - `GetTimeToNextEvent(EVENT_JERRY)` — scans all 32 slots (event.c:185-192)
  - `SubtractEventTimes(timeDelta, <other list>)` — scans all 32 slots, unconditional subtract,
    no `valid` check by design (event.c:242-255, comment at 210-211/228-229 explains the tradeoff)
  - `HandleNextEvent(<chosen list>)` — scans all 32 slots to decrement (event.c:199-239), then
    dispatches one callback
  That's 4×32 = 128 struct-element touches per outer-loop iteration, not counting the callback
  itself. Re-arming a callback (`SetCallbackTime`, event.c:74-112) adds an early-exit linear scan
  for a free slot on top, called once per dispatched event.
- How many outer-loop iterations per field: each iteration corresponds to exactly one dispatched
  event, so the count is the sum of all EVENT_MAIN + EVENT_JERRY callback firings per field.
  Confirmed from callers: `HalflineCallback` re-arms itself every `JaguarGetHalflinePeriodUs()`
  (jaguar.c:1358, ~63.6us NTSC) → 524/field on EVENT_MAIN. On EVENT_JERRY,
  `JERRYI2SCallback` re-arms at `22.675737` us fixed (src/jerry/jerry.c:383) → ~735/field, and
  `DSPSampleCallback` re-arms at the I2S sample period (src/jerry/dac.c:329, comment at dac.c:23-24
  says "fires at 48 kHz") → ~800/field, plus JERRYPIT1/2 and UART when a game uses them. Total is
  in the range of ~1,500-2,000+ event dispatches per field for a title doing audio, i.e. roughly
  190,000-256,000 struct-element touches per field just in scheduler bookkeeping, before any
  emulation work happens.
- Why it costs on in-order ARM: `struct Event` is `{bool, int, double, void(*)(void)}`, likely
  24 bytes with padding — a linear scan through contiguous memory is cache-friendly, but the
  stride between the `double` fields defeats simple auto-vectorization (non-unit stride gather),
  so this is very likely a plain scalar loop: load-compare-branch or load-subtract-store per slot,
  32 times, 4 times, ~2000 times/field. On a Cortex-A7/A53-class in-order core with no branch
  predictor depth to spare, ~2000 × 128 = ~256,000 branchy scalar iterations/field is a
  non-trivial, currently-unmeasured tax that scales with the EVENT_LIST_SIZE constant rather than
  with the number of *actually active* events (typically well under 10 at once).
- Proposed change (pick one, ascending effort): (a) cheapest — track `numberOfEvents` per-list
  (the code already has a combined `numberOfEvents` at event.c:48, just needs splitting) and give
  `SubtractEventTimes`/`HandleNextEvent`'s "touch every slot" loops an early-exit once they've
  processed `numberOfEvents` valid ones — helps only if valid slots cluster near the front, which
  they mostly will if `SetCallbackTime` is changed to fill low indices preferentially (it already
  does, since it scans from 0). (b) proper fix — replace the two 32-slot arrays with a compact
  packed array of only the live events (swap-remove on completion) so the scan length equals the
  actual active-event count (typically 3-8), not 32. (c) most invasive — a small binary
  min-heap keyed on eventTime, turning `GetTimeToNextEvent` into O(1) and insert/remove into
  O(log n); probably overkill at n<10.
- Impact: MED, genuinely unsure without a profile — the work per element is small, but the
  iteration count (thousands/field × 128) is large enough that I'd want this measured (e.g. via
  the existing BENCH_PROFILE perf counters, or wall-clock A/B) before calling it HIGH or LOW.
  This is the single largest *unexamined* candidate this audit turned up.
- Risk: LOW-MED for (a)/(b) — pure refactor of an already-isolated module with existing savestate
  serialization (`EventStateSave/Load`, event.c:307-393) that would need to keep working
  (compaction changes slot *indices*, which the callback-ID-based savestate format
  (event.c:271-304) is actually robust to, since it doesn't serialize slot identity semantics
  beyond array position — would need care but the format already tolerates reordering since it
  saves/restores by position, not by a stable ID other than the callback pointer). (c) is MED risk
  (a heap is easy to get subtly wrong, and savestate format would need a bigger rework).
- Effort: S for (a), M for (b), M-L for (c).
- Sonnet-doable for (a)/(b) with a clear spec and the existing `test/` savestate/event tests as a
  correctness gate; (c) would benefit from a design pass first.

---

## 3. Frame loop (`JaguarExecuteNew`, jaguar.c:1683-1752)

Already well-shaped: one event-driven `do/while`, GPU/DSP slice budgets handed out once per
iteration (`GPUBeginSlice`/`DSPBeginSlice`), `M68KExecuteWithStalls` runs the 68K, `GPUExec`/
`DSPExec` run only the slice remainder not already consumed inline via `GPUSyncToM68K`/
`DSPSyncToM68K`. No redundant per-halfline work found beyond what's already discussed in §2 (the
loop's *iteration count* is the cost, not extra work inside each iteration). `PERF_INC`/`PERF_ADD`
(perf_counters.h) are BENCH_PROFILE-only (compiled out in shipping builds) — confirmed by their
use pattern matching other PERF_COUNTER sites; not re-verified against the header since it's out
of scope, but no evidence of a stray always-on counter in this loop.

**Finding 3.1 — `HalflineCallback` (jaguar.c:1272-1359) does 2 TOM register round-trips
(`TOMReadWord`, `TOMWriteWord`) plus a full `TOMExecHalfline` call on every halfline, including the
odd ones that do nothing but light-gun bookkeeping.** This is correct/expected — VC has to advance
every halfline per JTRM, and `TOMExecHalfline` already early-returns after `TOMLightgunHalfline`
for odd halflines (tom.c:1519-1520, see §4). Not flagging as a problem; noting I checked it because
the task asked "anything done per-halfline that could be per-frame" and the answer here is no —
VC/HC advancement and the OP's even/odd gating are genuinely per-halfline hardware behavior.

**Finding 3.2 — `GPUSyncToM68K`/`DSPSyncToM68K` early-out is a single flag check.**
`GPUSyncToM68K` (src/tom/gpu.c:1393-1401) returns immediately if `!GPU_RUNNING || halted ||
gpu_in_exec` before doing any of the cycle-domain math. Combined with the 2-range-check gate at the
call site (Finding 1.3), a 68K access that doesn't touch GPU/DSP RAM and a halted GPU/DSP both cost
only a few branches. This matches the task's ask ("cheap early-out before any bookkeeping") — yes,
it already has one. No action proposed.

---

## 4. TOM video (src/tom/tom.c)

**Already table-driven, not computed per pixel.** `CRY16ToRGB32[0x10000]`, `RGB16ToRGB32[0x10000]`,
`MIX16ToRGB32[0x10000]` (tom.c:463-465) are filled once in `TOMFillLookupTables` (tom.c:622-649,
called once from `TOMInit`, tom.c:1616-1621) and every scanline renderer
(`tom_render_16bpp_cry_scanline` etc., tom.c:787-1382) does a straight LUT index per pixel — no
per-pixel arithmetic conversion, no per-pixel function call in the 1x path. `TOMExecHalfline`
(tom.c:1469-1613) is a single early-return for odd halflines (line 1519-1520) before any OP/render
work, which is the correct place for that gate.

**Finding 4.1 — line-buffer BG clear is a byte-at-a-time loop, only when BGEN is set.**
tom.c:1541-1547:
```c
uint8_t * current_line_buffer = (uint8_t *)&tomRam8[0x1800];
uint8_t bgHI = tomRam8[BG], bgLO = tomRam8[BG + 1];
if (GET16(tomRam8, VMODE) & BGEN)
   for(i=0; i<720; i++)
      *current_line_buffer++ = bgHI, *current_line_buffer++ = bgLO;
```
1440 individual byte stores, run once per *rendered* halfline (i.e. once per output scanline, up
to ~240-256/field for a typical display area) whenever the title has BGEN set in VMODE.
- Why it costs: byte stores instead of a widened (16-bit pattern) or `memset`-style fill; on a
  simple in-order core this is still just a tight store loop the compiler *may* auto-vectorize
  (constant trip count, no aliasing hazard since bgHI/bgLO are locals) — I did not verify the
  actual generated code, so I can't say whether this is already free.
- Proposed change: replace with a 16-bit store loop (`SET16` per iteration, 720 iterations instead
  of 1440 byte stores) or, when `bgHI == bgLO`, a plain `memset`. Mechanical, low-risk.
- Impact: LOW-MED, unsure — only fires when BGEN is set (not all titles use it) and only 1440
  bytes/line; worth a quick compiler-explorer/objdump check before spending effort here, since a
  modern `-O3 -mcpu=...` build may already turn this into a wide store.
- Risk: LOW (pure output-identical rewrite, no semantic change if `bgHI`/`bgLO` composition into a
  16-bit store matches the existing byte order — needs a byte-order check since the code writes
  hi then lo explicitly, i.e. it's already big-endian-explicit, not a `SET16` macro call, so the
  replacement must preserve that exact byte order).
- Effort: S. Sonnet-doable mechanically, with the acid/framebuffer A/B tests as the correctness
  gate (must be pixel-identical to the byte-loop version).

**Finding 4.2 — hi-res (Nx) renderers are separate, gated code paths (`shadowHiresActive`), not
touched by 1x users.** Confirmed by reading `tom_render_16bpp_cry_scanline_hires` (tom.c:916-1021):
the extra per-subpixel work (shadow tag lookups, `replActive` hoisted out per the comment at
tom.c:930-933) only runs when the hi-res enhancement is on. Not a default-path finding; no action.

---

## 5. Object Processor (src/tom/op.c)

**Finding 5.1 — `OPObjectExists`/`OPDiscoverObjects` O(N²) is NOT a per-frame cost; it runs once,
at emulator teardown.** op.c:319-331 (`OPObjectExists`, linear search, comment at line 323
"Yes, we really do a linear search, every time") is only called from `OPDiscoverObjects`
(op.c:334-369), whose only caller is `OPDone()` (op.c:310-316), whose only caller is `TOMDone()`
(tom.c:1627), which is called once from `JaguarDone()` (jaguar.c:1593-1601) at core shutdown —
confirmed via `grep -rn OPDone\b` / `grep -rn OPDiscoverObjects\b` across `src/`, only hits in
op.c/op.h/tom.c. **This corrects the framing in the task brief and (per source comments) issue
#123: the O(N²) object-list walk is not "~1% of frame time," it is 0% of frame time — it never
runs during gameplay at all.** No action needed; flagging so nobody re-optimizes a cold path.

**Finding 5.2 — the per-halfline object-list walk (`OPProcessList`, op.c:476-776) is a straight
switch on 5 object types, each O(1) plus one/two phrase fetches (`OPLoadPhrase` →
`JaguarReadLong` ×2) per object.** No unexpected per-object overhead found: the loop is bounded by
`OP_RUNAWAY_GUARD_OBJECTS` (op.c:479, 771-774) so a malformed/huge list can't run away. The
`OBJECT_TYPE_GPU` case (op.c:654-698) genuinely does need to run the GPU inline (that's the
documented #406/#354-adjacent hardware behavior — the OP halts until the GPU releases it) and
already has a bounded guard (`OP_GPU_RELEASE_GUARD_CYCLES`) and an early-out for games that never
enabled GPU IRQ3 (`GPUOPInterruptEnabled()`, op.c:685-686) so it doesn't burn cycles needlessly.
No change proposed here.

**Finding 5.3 — bitmap pixel loops (`OPProcessFixedBitmap`, op.c:780-1240; sampled the 8bpp case at
op.c:1074-1126 and 16bpp at op.c:1127-1190+) are already function-call-free per pixel in the
default (non-shadowfb/non-hires) path.** Transparency (`flagTRANS`) and read-modify-write
(`flagRMW`) checks are per-pixel branches (unavoidable — they're genuinely per-pixel hardware
state), but the actual write is a direct store or a `BLEND_Y`/`BLEND_CR` macro (LUT-backed,
`op_blend_y[0x10000]`/`op_blend_cr[0x10000]`, op.c:67-68), not a function call.
`ShadowFBLineFromRAM`/`ShadowHiresLineFromRAM` (op.c:1168-1180) are called per-pixel but only when
`shadowFBActive`/`shadowHiresActive` — both off by default, so the stock path never pays for them.
No action proposed; this matches the pattern already established in tom.c (LUTs, hoisted flags).

---

## 6. crash_detect.c / vjtrace.c / perf_iface.h — release-build cost

**Finding 6.1 — `vjtrace` is fully compiled out of shipping builds.** `VJT_WATCH_RD`/`VJT_WATCH_WR`/
`VJT_EMIT` are `do {} while(0)` unless `VJ_TRACE` is defined (src/core/vjtrace.h:321-323 vs
340-342), and `VJ_TRACE` is only added to `CFLAGS` when `TEST_EXPORTS=1` (Makefile:158-165) — i.e.
only `make test`, never a normal `make` or a RetroArch-shipped core. Every `VJT_WATCH_*` call site
sprinkled through jaguar.c's memory dispatch (§1) costs literally zero instructions in a release
build. Confirmed, not previously stated as fact in this file's own header comment ("body is
entirely #ifdef'd out, so there is zero cost" — vjtrace.h:6) but I verified the Makefile gating
myself rather than trusting the comment. No action needed.

**Finding 6.2 — `crash_detect` defaults to ON (`virtualjaguar_crash_detect` = "enabled" in
libretro_core_options.h:226) and runs once per *frame* (called from top-level `libretro.c:5548`,
outside this audit's file scope but the call site is what determines "once/frame" vs "once/
instruction"), not per instruction or per halfline.** Its cost is bounded and small:
`fb_hash` (crash_detect.c:207-227) deliberately samples 256 evenly-spaced pixels
("costs 256 ops per frame" per its own comment at line 208, verified against the loop at 221-225
which steps by `total/256`), everything else in `CrashDetectFrameTick` (crash_detect.c:396+) is
O(1) counter/threshold comparisons. Confirmed cheap; no action needed. This directly answers the
task's question ("is it on by default and what does it cost") — yes/on, and ~256 ops + O(1)/frame.

**Finding 6.3 — `perf_iface.h`'s ~2,000 probes/frame are already documented as negligible by the
header's own design-rationale comment (lines 17-38), which I verified rather than took on faith**:
off (`vjPerfActive==0`, RetroArch declines or has `perfcnt_enable` off) costs one
predicted-not-taken branch per probe; on, it's a real indirect call/probe (~2,000/frame) but at
"slice" granularity (once per halfline/blit/event-loop-step, never per instruction), which the
comment itself calls "negligible." I found nothing in jaguar.c/tom.c/op.c that contradicts this —
probe sites are all at function-entry/exit of frame-loop-shaped functions, not inner loops.

---

## 7. Threading feasibility (design assessment, not code-level findings)

The architecture is fundamentally hard to parallelize safely because of how tightly the four
"processors" are coupled *within* a single scheduler slice, not just at frame boundaries:

- **(a) Audio mixing/resampling of already-produced DSP output.** This is the only candidate with
  a genuinely decoupled producer/consumer shape already in the code: `DSPSampleCallback`/
  `DACWordStrobe` (src/jerry/dac.c:213-330ish) capture I2S samples into a ring buffer
  (`i2sRingL`/`i2sRingR`) during the interleaved event loop, and `DACPrepareFrame`/`SoundCallback`
  (dac.c:332,376) do the linear-interpolation resample from ring to output buffer once per frame,
  reading only already-finalized samples. **However**, the actual resample work is small (on the
  order of a few hundred samples/frame of linear interpolation — cheap, memory-bound), and
  `audio_batch_cb` is a frontend callback that the libretro API contract expects to be invoked
  from the same thread as `retro_run` (RetroArch is not documented/tested to accept it from a
  worker thread). Feasibility: the *compute* part could move to a worker thread that finishes
  before the (still-synchronous) `audio_batch_cb` call, but the work is likely too small for the
  thread-wake/join overhead to pay for itself on a weak in-order core. Verdict: technically safest
  candidate, but LOW expected payoff — not recommended without a profile showing the resample loop
  itself is actually hot (I doubt it is).
- **(b) Framebuffer format conversion / hi-res upscaling on another thread.** Not separable as the
  code is structured today: CRY/RGB16→XRGB8888 conversion happens *inline*, pixel-by-pixel, during
  `TOMExecHalfline`'s scanline render (§4) — there is no distinct "raw Jaguar framebuffer" that
  gets converted in a second pass; the conversion *is* the render. Making this threadable would
  require restructuring TOM to render into an intermediate CRY/16bpp buffer and adding a real
  post-process conversion pass, which is a bigger architecture change than "make an existing pass
  threaded." Feasibility: MED-effort, MED risk (savestate/run-ahead correctness has to account for
  a buffer that isn't XRGB8888-final at the point a state is captured); not something I'd recommend
  without a separate design pass.
- **(c) DSP on its own thread with lock-stepped sync.** Not recommended. `M68KGPURAMSyncRead`/
  `M68KGPURAMSync` (jaguar.c:483-498, 671-681) force a synchronous GPU/DSP catch-up on *every* 68K
  access that falls in GPU/DSP local RAM — which includes ordinary mailbox polling loops, i.e.
  this can fire many times per halfline, not just at frame boundaries. Cross-thread synchronization
  at that granularity (a mutex/futex signal potentially multiple times per halfline, ×524
  halflines/field) would almost certainly cost more in wake/signal latency than serial execution
  saves, on top of turning a currently-deterministic instruction-interleaved model into one where
  determinism (needed for run-ahead and netplay-class savestate replay) depends on thread
  scheduling order unless a full lock-step barrier is added at every sync point — which then is no
  longer meaningfully "threaded" (all real work still serializes on the barrier). Verdict: not
  feasible without a fundamentally different coupling model between 68K and DSP; out of scope for
  an incremental change.
- **(d) OP rendering pipelined against the next scanline.** `OPProcessList` writes the *current*
  halfline's line buffer, which is then read by the scanline renderer later in the *same*
  `TOMExecHalfline` call (tom.c:1549 write, 1587-1593 read) — no double-buffering exists today.
  Complicating factor: `OPProcessList`'s `OBJECT_TYPE_GPU` case (op.c:654-698) runs the GPU inline
  and mutates shared GPU state, so pipelining "OP for halfline N+1" against "TOM render of halfline
  N" on separate threads would race on GPU state for any title using GPU objects (Primal Rage,
  per the comment at op.c:660-673, is a real example). Feasibility: only safe for the no-GPU-object
  common case, which would need runtime detection and a fallback to serial — MED-HIGH effort for
  LOW clearly-demonstrated payoff (the OP/TOM per-halfline work is already cheap per §4/§5). Not
  recommended.

**Overall threading conclusion:** the emulator's determinism/accuracy model depends on fine-grained
(sub-halfline) synchronous interleaving between 68K/GPU/DSP, which is the opposite of what's needed
for cheap thread parallelism. The one clean producer/consumer boundary that exists (audio resample)
doesn't have enough work in it to be worth the synchronization cost. I'd deprioritize threading
entirely for this subsystem area and focus perf effort on the sequential-cost findings above
(§1.1 LTO, §2 event scheduler) instead.

---

## Summary table

| # | Finding | File:line | Impact | Risk | Effort | Sonnet-OK |
|---|---|---|---|---|---|---|
| 1.1 | Bus dispatch fns non-static, no LTO on rpi*/unix targets | jaguar.c:500-1181; Makefile:219 | MED-HIGH (unsure) | LOW-MED | S(flag)/M(validate) | Yes, w/ human sign-off on acid results |
| 2.1 | Event scheduler O(32) linear scans ×4, ~1.5-2k dispatches/field | event.c:74-255; jaguar.c:1683-1752 | MED (unsure) | LOW-MED | S-M | Yes |
| 4.1 | BG line-buffer clear is byte-at-a-time | tom.c:1541-1547 | LOW-MED (unsure) | LOW | S | Yes |
| 1.2 | dram_timing per-access call | bus_arbiter.c:136-185 | LOW (opt-in, off by default) | n/a | n/a | n/a — no action |
| 5.1 | OP O(N²) is a myth for perf purposes — teardown-only | op.c:319-369; tom.c:1627 | NONE | n/a | n/a | n/a — no action, just a correction |
| 6.1-6.3 | vjtrace/crash_detect/perf_iface all already cheap by design | see §6 | NONE | n/a | n/a | n/a — no action |
| 7 | Threading: only (a) audio is safe, and it's too small to bother; (b)-(d) unsafe or too invasive | design-level | — | — | — | — |
