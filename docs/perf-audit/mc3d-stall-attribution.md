# Missile Command 3D — RISC stall attribution (2026-08-27)

Where MC3D's interpreter budget actually goes, and what the >16 ms spike frames do
differently (answer: nothing — see §3).  Follows up the host profile that showed GPU
interpreter ~33% / DSP interpreter ~28% of samples with `jr` the top single opcode in both.
Shorthand, LLM-oriented; companion to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md).

**Tools introduced with this audit** (both need `make TEST_EXPORTS=1`; build lines in each
file's header):

- `test/tools/risc_backtrace_histogram.c` — drains the vjtrace GPU/DSP PC-history rings
  (`vjtrace_backtrace()`, last 1024 PCs/processor) after every frame and histograms them, so
  the profile covers **in-frame** execution, not just the parked frame-end PC that the
  existing `risc_pc_histogram` samples.  Disassembles the loop body around each top PC from a
  **first-sighting snapshot** (MC3D overlays GPU kernels, so code read at exit can be a
  different program — the tool flags that), and records frame-end parked-PC **register
  snapshots**, which is what resolved the polled addresses below.
- `test/tools/frame_cost_probe.c` — per-frame CSV of wall ms + work counts
  (GPU/DSP interpreted-opcode deltas, DSP idle-skip savings, IRQ/OP/blitter event counters
  from `vjtrace_counters`), plus a `--top N` most-expensive-frames-vs-median report.

Setup: worktree at `libretro/develop` (baf682d), macOS arm64 (M-series),
`make TEST_EXPORTS=1`, MC3D attract mode (no input — fixed-frame scripted input is invalid
for timing, per the vj-debug trap list), 900-frame warmup, 6000 measured frames.
Caveat that applies to every number here: **arming vjtrace force-disables the DSP idle-loop
fast-forward** (`DSPExec` gate, issue #569) and adds per-instruction ring-write overhead, so
the armed runs measure the fully-interpreted stream.  That matches the shipping default
(`virtualjaguar_risc_idle_skip` ships disabled) but not a config where the user enabled it.
Counts are load-insensitive; the ms columns are same-run-relative only.

## 1. GPU: 66.8% of interpreted instructions are ONE 3-instruction semaphore poll

Backtrace-ring histogram, 6,144,000 sampled instructions (1024/frame × 6000 frames):

| rank | PC | share | what |
| --- | --- | ---: | --- |
| 1-3 | `$F03134/36/38` | **66.75%** | scene-ready semaphore poll (below) |
| 4-20 | `$F03924-48` | ~8.2% (0.48% each, flat) | blitter-feeding kernel — straight-line real work |

The dominant loop (all three PCs are the same loop):

```
$F03134: A402  load   (r0),r2      ; r0 = $00009704  (main RAM, from parked-reg snapshot)
$F03136: 7822  cmp    r1,r2        ; r1 = 3
$F03138: D7B8  jr     N,-3 -> $F03134
$F0313A: 8C04  moveq  #0,r4        ; delay slot
```

It polls **main-RAM word `$9704` until it reaches 3**.  A vjtrace watch
(`--watch 0x9704:4:rw`) names both sides: the **68K writes 1→2→3 from PC `$9A18`** (~1.4
writes/frame) and the GPU itself clears it at `$F03144` after the wait exits.  So this is a
GPU-waits-for-68K scene semaphore: the 68K prepares the next scene stage while the GPU burns
its entire remaining slice budget interpreting a 3-instruction wait.  `gpu_exec_opcode_count`
is ~442,700/frame, dead flat — deterministic slice budgets mean the GPU consumes its full
26.6 MHz budget every frame whether it works or waits.

The flat 0.48%-per-PC block at `$F03924-48` is a per-strip blit submission kernel (parked
r14=`$F02200` A1_BASE, r15=`$F02224` A1_PIXEL, r16=`$F02238` B_CMD, r18=`$F0223C` B_COUNT).
Its `load (r16) ; btst #0 ; jr Z,-3` head looks like a blitter-busy wait but is NOT a spin in
this core: blits execute synchronously on the B_CMD write, so the poll exits after one
iteration.  Equal hit counts across all ~17 PCs confirm straight-line execution — this ~8% is
real work, not elidable.

MC3D swaps GPU overlays (the RAM at `$F03134` holds an MMULT transform kernel at other
times), which is why the first-sighting code snapshot in the tool matters.

## 2. DSP: 57.4% is the Atari sound driver's I2S tick poll

| rank | PC | share | what |
| --- | --- | ---: | --- |
| 1-3 | `$F1B128/2A/2C` | **57.4%** | I2S frame-counter poll (below) |
| 4-9 | `$F1B0E8-F4` | ~4.6% | command/jump-table dispatch — real work |
| 10+ | `$F1B2B8...` | ~0.29% each, flat | mixer straight-line code — real work |

```
$F1B128: 7B82  cmp     r28,r2       ; r28 = local tick counter
$F1B12A: 96A2  movefa  r21,r2       ; r2 = ALTERNATE-bank r21 (written by the I2S ISR)
$F1B12C: D7A2  jr      Z,-3 -> $F1B128
$F1B12E: 0C20  addqt   r1,r0        ; delay slot: monotonic accumulator
```

Same driver family as Iron Soldier / AvP (`perf-audit-2026-08.md` measured 74-99.7% there):
wait until the ISR-advanced alternate-bank counter differs from the local copy.
`dsp_exec_opcode_count` ~473,700/frame — the DSP too consumes its whole budget every frame.

**The shipped DSP idle-loop fast-forward already catches this loop.**  With
`virtualjaguar_risc_idle_skip=enabled` (`test/tools/dsp_idle_ab`, 300 measured frames):
interpreted DSP opcodes drop 474,867 → 182,637/frame (**−61.5%**), fires=995/frame,
opcodes_skipped=87.7M/300 frames.  It ships **disabled by default** while the compatibility
corpus grows, and is also suppressed by blit-memo / non-stock clock scale / dram_timing /
gpu_pipeline_timing / armed vjtrace.  There is **no GPU equivalent** yet (P1 "port to GPU" is
unimplemented).

## 3. Spike frames: the expensive frames do the SAME emulated work

`frame_cost_probe`, 6000 frames.  Clean (unarmed) baseline from `risc_pc_histogram` on the
same host/session: p50 8.7 ms, p99 47.8 ms, p999 114.6 ms, max 199 ms, 15.45% over the 16.67 ms
budget (this host was noisier than the earlier 3.0/30.6 ms capture; the bimodal shape
reproduces regardless).

Per-frame work counts vs frame time, 6000 frames:

| column | correlation with ms |
| --- | ---: |
| gpu_ops | 0.004 |
| dsp_ops | 0.041 |
| blit_cmd | 0.281 |
| op_obj / op_list / irq_assert | constant (1007 / 256 / 1 per frame) |

The worst frame (124.6 ms) executed 442,707 GPU ops, 440,502 DSP ops, 326 blits, 1007 OP
objects — indistinguishable from the 12 ms median frame (442,703 / 456,065 / 324 / 1007).  A
60 ms frame ran **zero** blits; the 6,589-blit frame took 32.6 ms.  Slow frames cluster in
adjacent runs (297 of 579 over-budget frames are adjacent to another; e.g. 3321-3323,
6206-6207) — the signature of transient host interference, not workload.

**Conclusion: the spikes are host-side (scheduler/QoS/thermal), not emulated-work bursts.**
The emulated workload is nearly constant per frame *because* both RISC interpreters always
burn their full slice budgets — mostly spinning.  The actionable lever is therefore not
"find the expensive frame" but "lower the constant baseline" so host-noise spikes stay under
budget: on the A12 the same interference lands on a much higher baseline and drops frames.
(yarc cross-check reproduces this: frames 62-70 hit 12-14 ms vs a 5.95 ms median with
byte-flat counts.)

## 4. Cross-checks (300 frames each)

- **yarc.j64** — GPU: **100%** of sampled instructions at `$F03192` `jr Z,-1` self-spin
  (delay slot `cmpq #0,r22` re-arms Z; exits via interrupt).  DSP: 0 instructions.  Confirms
  the #533 "yarc is an amplifier" caveat with the loop now named.
- **jagniccc.j64** — GPU: **100%** split across `$F03042/44`:
  `load (r15),r0 (ds) ; cmpq #0,r0 ; jr NZ,-2` with parked r15=`$F03FF0` — polls a 68K
  mailbox in GPU **local** RAM (matches `perf-audit-2026-08.md`'s finding, now with the
  address resolved from registers).  DSP: healthy spread (top PC 7.6%, mixer/dispatch code) —
  niccc's DSP does real work; its wait share is small.  Frame times tight (p99 6.5 ms armed).

## 5. Ranked optimization candidates

1. **GPU port of the idle-loop fast-forward** (P1 phase 3, issue #569 framework) — MC3D's
   `$F03134` poll is 66.8% of GPU interpretation (≈22% of total frame at the profiled 33%
   GPU share); jagniccc's mailbox poll is ~100% of its GPU samples; yarc's self-`jr` is 100%.
   All three shapes are admissible under the existing DSP admission rules (plain-RAM/local-RAM
   loads + ALU + the loop-closing `jr`; register-space loads stay excluded).
   *Determinism risk: must stay byte-identical* — same contract the DSP skip already meets
   (cycles, registers, flags, `gpu_exec_opcode_count` advanced exactly; gate off under
   dram_timing / pipeline timing / clock scale / armed vjtrace; A/B with
   `test/tools/dsp_idle_ab`-style framebuffer+audio hash sweep and `ir_ab.sh`).
2. **Ship `virtualjaguar_risc_idle_skip` wider** (per-title titledb rows or default-on after
   corpus validation) — zero new code; measured −61.5% DSP interpretation on MC3D, and the
   DSP is ~28% of frame → ≈16% of total frame recovered on the default config that currently
   leaves it off.  *Determinism risk: bit-exact by construction; the open question is corpus
   coverage, which is a validation task, not a design one.*
3. **Semaphore wait-elision beyond affine loops** (future, only if 1 underdelivers): MC3D's
   GPU poll exits via a 68K write that can only land between RISC slices, so a slice whose
   fixed-point probe proves "polled RAM word unchanged ⇒ loop is a fixed point" could skip to
   slice end in O(1) even where the affine-delta test fails.  *Determinism risk: same
   byte-identical contract; needs the same probe framework.*
4. **Host scheduling headroom on Apple TV / A12** (frontend-side, not core): the spikes are
   host interference amplified by a high constant baseline.  After 1+2 cut the baseline
   ~35-40%, re-measure on-device with the perf-counter route (`docs/profiling.md`) before
   inventing further core work.  *No determinism risk.*
5. **Not candidates**: the `$F03924` blitter-feed kernel (~8% GPU) and the DSP dispatch/mixer
   code (~5%) are straight-line real work; the B_CMD `btst` poll exits after one iteration
   because blits are synchronous in this core.

## Follow-ups (tooling gaps found while measuring)

- vjtrace has no per-frame **blitter pixel volume** counter (only BLIT_CMD count), so a
  same-command-count/large-area blit storm would be invisible to `frame_cost_probe`; the
  BENCH_PROFILE `blitter_inner` counter covers it but needs a separate build.  Noted rather
  than added — out of scope for a tools-only PR.
- `vjtrace_backtrace`'s ring records interpreter-loop PCs only (inlined `jr`/`jump` delay
  slots and idle-skipped iterations don't push), so histogram shares slightly undercount
  delay-slot instructions.  Documented in the tool header; harmless for spin attribution.
