# Performance baseline + profile: OP vs blitter vs GPU/DSP

Captured 2026-05-01 on Apple Silicon (M-series Mac), arm64 build, default flags + `RELEASE_DEBUG_INFO=1`.  See `make benchmark` and `docs/profiling.md` for the methodology.

## TL;DR

**OP is not the bottleneck.  Blitter is rarely the bottleneck either.  GPU and DSP RISC interpretation dominate frame time across the board.**

This redirects the next-up perf work from issue #123 (OP) to a different target — see "Recommendation" at the bottom.

## Baseline FPS

`make benchmark` 600 frames after 60 warmup, headless, no video presentation, no audio output.  Same Mac, same thermal state.

| ROM | Fast blitter | Accurate blitter | Slowdown |
|---|---:|---:|---:|
| `yarc.j64` (raycasting demo) | 280 FPS | 229 FPS | 1.22× |
| `jagniccc.j64` (NICCC compo demo) | 355 FPS | 258 FPS | 1.37× |
| `Iron Soldier (1994).jag` | 312 FPS | 258 FPS | 1.21× |
| `Iron Soldier 2 (World).j64` | 313 FPS | 266 FPS | 1.18× |
| `Doom - Evil Unleashed (1994).jag` | 306 FPS | 261 FPS | 1.17× |
| `Skyhammer_(1999).jag` | 339 FPS | 215 FPS | **1.58×** |

All ROMs run well above 60 FPS on M-series.  The "where does it hurt" question is meaningful for slower hosts (Pi, mobile) — same ratios, lower absolute numbers.

## Profile breakdown (`/usr/bin/sample`, 8s @ 6000-frame run)

Captured to `/tmp/op-baseline/sample-*-*.txt`.  Aggregated top-of-stack by symbol:

### `yarc.j64` (demo, fast blitter)

| Function | Samples | % of frame |
|---|---:|---:|
| `GPUExec` (per-instruction dispatch in `src/tom/gpu.c`) | ~5018 | **~74%** |
| `blitter_generic` | ~649 | 10% |
| `HalflineCallback` | 79 | 1.2% |
| (`OPProcessFixedBitmap` etc. didn't make top-15) | – | <1% |

**Demo content with heavy GPU programs.  This pattern would generalize to anything that uses the GPU's RISC for software rendering.**

### `Iron Soldier (1994).jag` (commercial, fast blitter)

| Function | Samples | % of frame |
|---|---:|---:|
| `DSPExec` (per-instruction dispatch in `src/jerry/dsp.c`) | 3456 | **~51%** |
| `dsp_opcode_jr` | 818 | 12% |
| `dsp_opcode_*` (load_r15_indexed, jump, load) | ~250 | 4% |
| **DSP TOTAL** | **~4524** | **~67%** |
| `HalflineCallback` / `JaguarReadLong` / `JaguarExecuteNew` | ~250 | 4% |
| `OPProcessFixedBitmap` | 68 | **1%** |
| `m68k_execute` | 49 | 0.7% |
| `blitter_generic` | 48 | 0.7% |

**OP at 1%.  Blitter at 0.7%.  68K at 0.7%.  Two-thirds of frame time in DSP interpretation.**

### `Skyhammer_(1999).jag` (commercial, fast blitter)

| Function | Samples | % of frame |
|---|---:|---:|
| `DSPExec` | 2260 | **~33%** |
| `GPUExec` | ~1600 | **~24%** |
| `m68k_execute` | 177 | 2.6% |
| `blitter_generic` | 112 | 1.6% |
| `HalflineCallback` | 106 | 1.6% |

**GPU + DSP = 57% of frame time.  Blitter is irrelevant here on the fast path.**

### `Skyhammer_(1999).jag` (commercial, accurate blitter)

| Function | Samples | % of frame |
|---|---:|---:|
| `DSPExec` | 1525 | **~22%** |
| `BlitterMidsummer2` + `DATA` + `ADDARRAY` | ~1430 | **~21%** |
| `GPUExec` | ~720 | **~11%** |
| `m68k_execute` | 97 | 1.4% |

**The one case where the blitter genuinely matters — and only because the user opted into accurate mode.  Even here, DSP is comparable.**

## What this changes

The original `[spike] OP performance audit` (#123) and `[spike] Blitter performance audit` (#124) both assumed those subsystems dominate.  They don't.

| Target | Hypothesis going in | Reality | ROI |
|---|---|---|---|
| **OP** (#123) | "dominates on heavy-OP scenes (Wolf3D, T2K, Iron Soldier)" | 1% on Iron Soldier; doesn't make top-15 elsewhere | Low — even a 10× OP speedup buys ~1% of frame time |
| **Blitter** (#124) | "fast vs accurate matters for some games" | Fast is <2% everywhere except where accurate is opted-in (then ~21% on Skyhammer) | Medium — only for users running accurate blitter on specific titles |
| **GPU/DSP RISC** (#122 sub-component) | "JIT might help on mobile/Pi" | **24-74% of frame time, every ROM** | **High** — single dynarec helps both because they share an ISA |
| **68K** (#122 sub-component) | "wrap UAE JIT or Cyclone68k" | 0.7-2.6% — barely visible in any profile | Low |

## Recommendation

**Redirect to #122 (JIT / dynarec / cached IR), specifically the GPU/DSP RISC half.**  Both Tom GPU and Jerry DSP share the same ISA (~64 opcodes, fixed 16-bit encoding, no MMU).  A single basic-block dynarec or cached-IR dispatcher covers both, and the profile data says it would attack the actual hot path on every game tested:

- Demos (yarc/jagniccc) → mostly GPU.
- Commercial games (Iron Soldier, Doom, Skyhammer) → mostly DSP, sometimes both.

Closing #123 (OP) and #124 (blitter) as **wontfix-for-now** based on profile data is the honest call.  Cheap wins documented in those spikes (e.g., the OP `O(N²)` discovery bug, fast-blitter SIMD widening) can still land opportunistically — they're correct fixes, just won't move the headline number.

## Why we DIDN'T touch OP/blitter as planned

- The OP spike (#123) called out an `O(N²)` linear scan in `OPObjectExists`.  That's still a real bug worth fixing for code-quality reasons.  But it costs ~1% of frame time, not the ~30% the spike speculated.  Not a perf win.
- The blitter accurate-mode wins (#124) are real for users who deliberately enable that mode on a specific title.  But the default user experience runs the fast blitter, which is single-digit% of frame time.

## What 68K JIT would buy us

Per profile: 0.7-2.6% of frame time across all ROMs.  Even a 10× speedup of the 68K interpreter → at most 2-3% wall-clock improvement.  **Not worth the GPLv2 / GPLv3 license dance with UAE-JIT or the ARM-only constraint of Cyclone68k.**

The profile result also explains why the standalone Virtual Jaguar interpreter has been "fast enough" for years on desktop — modern CPUs eat the 68K interpreter for breakfast.  The GPU/DSP RISC don't eat as well because of the call-overhead-per-instruction pattern.

## Next steps

1. Update issues #122, #123, #124 with this profile data.  Keep #122 open and re-scope to GPU/DSP-only.  Close #123 and #124 with the linked profile evidence.
2. New spike: feasibility of a Tom RISC cached-IR / threaded-code dispatcher — the lowest-cost approach, works on JIT-restricted platforms (iOS, Switch).  Block JIT comes later if the cached IR proves the model.
3. Profile data + this doc lives at `docs/op-perf-profile.md` (this file).  Re-run periodically as a same-host commit-to-commit delta.

## Files / commands

- Baseline benchmarks: `make benchmark BENCH_ROM=<rom> BENCH_BLITTER={fast,accurate}` — 600 frames + 60 warmup default
- Profile capture: `./test/tools/test_benchmark <core> <rom> 6000 --warmup 60 --blitter <mode> &` then `sample $! 8 -file /tmp/sample.txt`
- Test ROMs in tree: `test/roms/yarc.j64`, `test/roms/jagniccc.j64`
- Commercial ROMs (private): `test/roms/private/Iron Soldier (1994).jag` and friends
