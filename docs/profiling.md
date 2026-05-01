# Profiling guide

How to measure where the Virtual Jaguar libretro core actually spends time.

## TL;DR — `make benchmark`

Wall-clock baseline you can run on every commit:

```bash
make benchmark                              # default: yarc.j64, 600 frames, fast blitter
make benchmark BENCH_FRAMES=3000            # longer run (smoother numbers)
make benchmark BENCH_BLITTER=accurate       # A/B against the slow path
make benchmark BENCH_ROM=test/roms/private/Atari\ Karts.jag
```

Reports `Frames/sec`, `Time/frame`, total wall time.  Boots the core via `dlopen`, runs N frames headless (no video presentation, no audio output), so you measure pure emulator work.

**Use it as a delta**: capture baseline before your change, run again after.  Don't trust absolute numbers across hosts (CPU, thermals, scheduler, big.LITTLE pinning).  Do trust same-host commit-to-commit deltas.

> The harness lives at `test/tools/test_benchmark.c`.  Read it if you want to measure something specific (per-subsystem timing, only-DSP, etc.) — it's <400 lines.

## macOS — Instruments / `sample`

**Instruments (Time Profiler)** is the easiest way to get a flame graph on macOS.

```bash
make benchmark BENCH_FRAMES=6000 BENCH_WARMUP=120 &
BENCH_PID=$!

# Sample for 30 seconds, output to .trace bundle
xcrun xctrace record --template "Time Profiler" --attach $BENCH_PID --output bench.trace --time-limit 30s
open bench.trace
```

The default symbolication is good — you'll see `OPProcessFixedBitmap`, `BlitterMidsummer2`, `DSPExec` etc. as top hot frames if they're slow.

For a quick text dump without the GUI:

```bash
sample $BENCH_PID 5 -file /tmp/sample.txt
# 5-second sample.  Read /tmp/sample.txt for collapsed call stacks.
```

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
| Blitter | `src/tom/blitter.c`, `blitter_mmio.c`, `blitter_simd_*.c` | Two paths: fast (`BlitterFast`, ~equiv to upstream's old fast blitter) and accurate (`BlitterMidsummer2`).  SIMD only kicks in on the fast path's pixel loops. |
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
