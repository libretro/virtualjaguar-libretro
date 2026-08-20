# Profiling guide

How to measure where the Virtual Jaguar libretro core actually spends time.

> **Investigating a report from tvOS, Android or a console port? Start with
> [On-device profiling](#on-device-profiling--retroarchs-performance-counters).**
> Everything else in this guide is host-side (macOS/Linux,
> Instruments/`perf`/`sample`) and cannot reach a device you can't attach a
> profiler to. Context for the wider effort: #509.

## On-device profiling — RetroArch's performance counters

The core registers eight timing counters through
`RETRO_ENVIRONMENT_GET_PERF_INTERFACE`, and RetroArch renders them in its own
UI on every platform including tvOS. This is the only route that measures the
device that is actually slow.

Nothing to build: it ships in release builds and is inert until a frontend
offers the interface.

### Capturing

1. In RetroArch, **Settings → Frontend → Performance Counters → On**
   (older builds: *Settings → Logging → Performance Counters*).
2. Load content, play the part that is slow, then quit the core — totals are
   also written to the RetroArch log at unload, which is usually easier to
   retrieve off a locked-down device than reading them off the TV.
3. **Settings → Logging → Log Verbosity → On** if you want the log copy.

### The counters

| Counter | What it brackets |
| --- | --- |
| `vj_m68k_slice` | One 68K slice from the event loop |
| `vj_gpu_exec` | All GPU RISC execution, every entry path |
| `vj_dsp_exec` | All DSP RISC execution, every entry path |
| `vj_gpu_sync` | The part of `vj_gpu_exec` pulled in by a 68K bus access |
| `vj_dsp_sync` | The part of `vj_dsp_exec` pulled in by a 68K bus access |
| `vj_op_halfline` | One Object Processor halfline render |
| `vj_blitter` | One blit, whichever engine is selected |
| `vj_dac_mix` | One frame of audio mixing |

### Reading them — they deliberately overlap

**These do not sum to frame time and are not a pie chart.** The Jaguar's
processors do not run in disjoint phases, and two nestings are real:

- A 68K access into GPU/DSP local RAM runs RISC cycles inline via
  `GPUSyncToM68K()` / `DSPSyncToM68K()`, so `vj_m68k_slice` **contains** RISC
  time.
- The Object Processor runs the GPU inline from a halfline callback, so
  `vj_op_halfline` **contains** GPU time.

`vj_gpu_sync` / `vj_dsp_sync` exist to make the first one measurable rather
than merely disclosed. To decompose:

```
real 68K work     ~=  vj_m68k_slice - vj_gpu_sync - vj_dsp_sync
slice-driven GPU  ~=  vj_gpu_exec   - vj_gpu_sync
```

`vj_gpu_exec` and `vj_dsp_exec` need no correction — every path that runs RISC
cycles goes through them.

### Two caveats worth stating plainly

**Enabling the counters costs time.** `vj_gpu_exec` alone brackets ~1,700
calls per frame, each taking two clock reads. Use the numbers for *relative
attribution* between subsystems, not as an absolute frame budget, and don't
compare a counters-on session against a counters-off one for total speed.

**It cannot perturb the emulation.** The instrumentation touches no emulated
state — `test/tools/perf_iface_witness` asserts that the framebuffer is
byte-identical with counters on and off, so a capture is measuring the same
machine you were playing.

### Sample shape

From `perf_iface_witness` on `yarc.j64`, 120 frames — the ordering is the
point, not the absolute numbers:

```
vj_gpu_exec      calls=201045  total=475217000   <- dominates
vj_blitter       calls=1433    total=100466000   <- few calls, expensive each
vj_op_halfline   calls=31440   total=26878000
vj_m68k_slice    calls=200390  total=6772000     <- small
vj_dsp_exec      calls=200390  total=4333000
vj_dac_mix       calls=120     total=14000
vj_gpu_sync      calls=0       total=0           <- this title never syncs
vj_dsp_sync      calls=0       total=0
```

### Adding a counter

Add a slot to the enum in [`src/core/perf_iface.h`](../src/core/perf_iface.h),
an identifier to `vjperf_idents[]`, and a `VJP_ENTER`/`VJP_LEAVE` pair at the
site. **Read the placement rule in that header first** — the probe goes after
a function's top-of-function guards, and there must be no `return` between
ENTER and LEAVE, or the slot leaks its nesting depth and silently measures
nothing for the rest of the session. `perf_iface_witness` gates that.

## TL;DR — `make benchmark`

Wall-clock baseline you can run on every commit:

```bash
make benchmark                              # default: yarc.j64, 600 frames, fast blitter
make benchmark BENCH_FRAMES=3000            # longer run (smoother numbers)
make benchmark BENCH_BLITTER=accurate       # A/B against the "accurate" path -- do not
                                             # assume "fast" is faster; it is scalar-only
                                             # while accurate is SIMD-accelerated (#511)
make benchmark BENCH_ROM=test/roms/private/Atari\ Karts.jag
```

Reports `Frames/sec`, `Time/frame`, total wall time.  Boots the core via `dlopen`, runs N frames headless (no video presentation, no audio output), so you measure pure emulator work.

**Use it as a delta**: capture baseline before your change, run again after.  Don't trust absolute numbers across hosts (CPU, thermals, scheduler, big.LITTLE pinning).  Do trust same-host commit-to-commit deltas.

> The harness lives at `test/tools/test_benchmark.c`.  Read it if you want to measure something specific (per-subsystem timing, only-DSP, etc.) — it's <400 lines.

## macOS — Instruments / `sample`

**Instruments (Time Profiler)** is the easiest way to get a flame graph on macOS.

The wrapper at `scripts/profile-mac.sh` builds the core, runs the benchmark
under `xctrace`, and writes a `.trace` bundle you can open in Instruments:

```bash
scripts/profile-mac.sh                                    # default: Time Profiler, accurate blitter
scripts/profile-mac.sh --template "CPU Counters"          # PMU: cycles, instructions, branch misses
scripts/profile-mac.sh --rom test/roms/yarc.j64 --open    # auto-open the trace
```

Manual invocation if you'd rather attach to a running process:

```bash
make benchmark BENCH_FRAMES=6000 BENCH_WARMUP=120 &
BENCH_PID=$!

xcrun xctrace record --template "Time Profiler" --attach $BENCH_PID --output bench.trace --time-limit 30s
open bench.trace
```

The default symbolication is good — you'll see `OPProcessFixedBitmap`, `BlitterMidsummer2`, `DSPExec` etc. as top hot frames if they're slow.

For a quick text dump without the GUI:

```bash
sample $BENCH_PID 5 -file /tmp/sample.txt
# 5-second sample.  Read /tmp/sample.txt for collapsed call stacks.
```

## Bespoke counters — `BENCH_PROFILE=1`

Sampling profilers tell you *where* time goes; counters tell you *how often*
something happens.  When you want exact iteration counts (e.g., "did my
fast-path actually skip the inner loop?"), use the `perf_counters` system in
`src/core/perf_counters.h`.

```bash
make benchmark BENCH_PROFILE=1 BENCH_BLITTER=accurate BENCH_FRAMES=300
# ...
# [perf] counter dump:
# [perf]   blitter_phrase_writes                    3034993
# [perf]   blitter_phrase_reads                     931821
# [perf]   blitter_inner_io                         3966814
# [perf]   blitter_inner                            4131151
# [perf]   blitter_outer                            337722
# [perf]   blitter_calls                            131628
```

The macros are zero-overhead when `BENCH_PROFILE` is undefined (default
build) — every `PERF_INC` becomes `((void)0)`, every `PERF_COUNTER`
becomes a typedef.  Use them freely in hot paths to instrument
hypotheses.

Adding a counter:

```c
#include "perf_counters.h"

PERF_COUNTER(my_event);             /* file scope */

void hot(void) {
    PERF_INC(my_event);             /* in-loop */
    PERF_ADD(my_event, n);          /* batch */
}
```

The harness (`test/tools/test_benchmark.c`) calls
`perf_counters_dump(stderr)` at exit; counter values appear right
before the `BENCHMARK RESULTS` block.

When to reach for this vs. Time Profiler:

| Question | Tool |
|---|---|
| "Where are we spending cycles?" | `xctrace` Time Profiler |
| "How many times does the inner loop run per frame?" | `BENCH_PROFILE=1` |
| "What fraction of inner iterations are no-ops?" | `BENCH_PROFILE=1` |
| "Are we hitting L1 / branch-mispredicting?" | `xctrace` CPU Counters |
| "Did this optimization change behavior, not just timing?" | `BENCH_PROFILE=1` (deltas in counts) |

## Linux — `perf` + flamegraph

```bash
sudo apt install -y linux-tools-common linux-tools-generic
git clone https://github.com/brendangregg/FlameGraph /tmp/flamegraph

make benchmark BENCH_FRAMES=6000 BENCH_WARMUP=120 &
BENCH_PID=$!

perf record -F 999 -g -p $BENCH_PID -- sleep 30
perf script | /tmp/flamegraph/stackcollapse-perf.pl | /tmp/flamegraph/flamegraph.pl > flame.svg
open flame.svg
```

`-F 999` = 999 Hz sample rate (avoid 1000 Hz lockstep aliasing with display refresh).  `-g` = capture call graphs.

## Hot paths to know

Suspicious-by-default places when something gets slow:

| Subsystem | File | Notes |
|---|---|---|
| Object Processor (sprites, bitmaps) | `src/tom/op.c` | `OPProcessFixedBitmap`, `OPProcessScaledBitmap`, `OPDiscoverObjects`. Dominant on heavy-OP scenes (Wolf3D, Tempest 2000). |
| Blitter | `src/tom/blitter.c`, `blitter_mmio.c`, `blitter_simd_*.c` | Two paths: fast (`blitter_generic`, the upstream-derived path) and accurate (`BlitterMidsummer2`). SIMD (`blitter_simd_{sse2,neon,scalar}.c`) is currently wired only into the **accurate** blitter's pixel kernel — see [issue #124](https://github.com/libretro/virtualjaguar-libretro/issues/124) for the plan to widen SIMD coverage. |
| 68K | `src/m68000/cpuemu.c` | Machine-generated UAE.  ~1.8 MB of source.  If this is hot, there's not much to do beyond JIT (out of scope). |
| GPU (RISC, 26.6 MHz) | `src/tom/gpu.c` | `GPUExec` per-instruction. Hot when game uses GPU heavily (most do). |
| DSP (RISC, audio) | `src/jerry/dsp.c` | `DSPExec` per-instruction.  See `src/jerry/dsp_acc40.h` for the 40-bit MAC. |
| Frame loop | `src/core/jaguar.c` | `JaguarExecuteNew` is the event-driven driver.  If the event queue is hot, look at `src/core/event.c`. |

## SIMD A/B testing

The blitter has three implementations, selected at build time via `BLITTER_SIMD`:

```bash
make BLITTER_SIMD=neon -j4 && make benchmark   # ARM
make BLITTER_SIMD=sse2 -j4 && make benchmark   # x86_64
make BLITTER_SIMD=scalar -j4 && make benchmark # portable fallback
```

Auto-detection picks NEON on aarch64 / SSE2 on x86_64 / scalar elsewhere (see `Makefile.common`).  Force the scalar build to verify SIMD is actually winning — when the gap closes, your bottleneck moved elsewhere.

## Build flavors

| Goal | Flags |
|---|---|
| Production perf | `make` (default; `-O2 -DNDEBUG -ffast-math -fomit-frame-pointer`) |
| Profiling (good symbols, near-prod perf) | `make RELEASE_DEBUG_INFO=1` (`-O2 -g`).  Strips later if shipping. |
| Sanitizers | `make CC="clang -fsanitize=address,undefined -O1 -g"` (catches bugs, halves perf) |
| Coverage | `make COVERAGE=1` (`-O0 -g --coverage`).  Don't profile this — coverage instrumentation overhead is ~3× and not representative. |
| Debug stepping | `make DEBUG=1` (`-O0 -g`) |

## Cycle / instruction counts

The core is **event-driven, not cycle-accurate** (`docs/source-layout.md` covers the rationale).  `JaguarExecuteNew` runs the 68K to the next event, then GPU, then fires callbacks.  Don't expect cycle-counter results to match real hardware — measure wall-clock instead.

If you do need cycle-level inspection:

```bash
# Linux: cycles + instructions per loop iteration
perf stat -e cycles,instructions,branches,branch-misses ./test/tools/test_benchmark ./virtualjaguar_libretro.so test/roms/yarc.j64 600

# macOS: Instruments has a "CPU Counters" template
xcrun xctrace record --template "CPU Counters" --launch -- ./test/tools/test_benchmark ...
```

## Regression triage

If `make benchmark` shows a regression after your change:

1. **Run twice** — check for noise.  Same-host runs typically vary <2%.  >5% delta is real signal.
2. **Bisect by commit**: `git bisect start HEAD <known-good-commit>`, mark good/bad with `make benchmark` results until you isolate the offender.
3. **Check both blitters** (`BENCH_BLITTER=fast` vs `accurate`) — sometimes a regression only shows on one path.
4. **Profile, don't guess** — the bottleneck is rarely where you'd expect on this codebase.

## See also

- [`CLAUDE.md`](../CLAUDE.md) — hardware model + repo layout
- [`docs/source-layout.md`](source-layout.md) — file-by-file source tour
- [`docs/emulation-bug-hunt-todos.md`](emulation-bug-hunt-todos.md) — known performance / accuracy follow-ups
- [`test/tools/test_benchmark.c`](../test/tools/test_benchmark.c) — the harness itself
