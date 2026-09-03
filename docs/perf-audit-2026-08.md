# Performance audit — 2026-08-22

Why RPi 4 / Apple A10X run this core badly while they run N64/Saturn/PSX interpreters fine,
and the prioritized, delegatable task list that falls out of it. Shorthand, LLM-oriented.
Raw per-subsystem sub-agent reports (line-level detail, rejected ideas) live in
[`perf-audit/`](perf-audit/); only items **verified by a second read or a measurement** are
promoted here.

## TL;DR

1. **The DSP (and GPU) interpreters spend most of their budget interpreting idle loops.**
   Frame-end PC sampling shows the DSP parked in a 3-5 instruction wait loop 90-99.7% of the
   time on Iron Soldier / AvP / Doom, while executing its **full** 26.6 MHz cycle budget
   (416k-590k opcodes/frame, every title). Nothing else can run inside a RISC slice and IRQs
   are only delivered at slice entry, so these loops are fixed points for the rest of the
   slice → a **cycle-exact fast-forward** is possible. DSP is 50-67% of frame time on
   commercial titles (Iron Soldier 67%, AvP 63%); this is the single biggest lever and
   needs no JIT. → **P1**
2. **Linux/Android/RPi builds pay GOT/PLT for every global in the hot loops.** ELF targets
   are `-fPIC` with only a link-time version script — no `-fvisibility=hidden`, no
   `-fno-semantic-interposition`, no LTO (LTO exists only for `classic_armv7_a7`). GCC must
   assume interposition, so `gpu_reg[..]`, the flags, `dsp_pc`, counters… are GOT-indirect
   loads per emulated instruction and every `JaguarRead*` is a PLT call. `RETRO_API` already
   carries `visibility("default")`, so hiding everything else needs no source change.
   `jni/Android.mk` additionally inherits **none** of `Makefile`'s opt flags (no `-O3`; NDK
   default `-O2 -fstack-protector-strong`). → **P2**
3. A handful of genuinely dumb per-pixel / per-instruction costs that are mechanical to fix:
   fast blitter does 3-6 full `JaguarRead*/Write*` dispatch calls per pixel although inline
   RAM helpers already exist in the same file; `GPUReadLong` splits every DRAM long into two
   `JaguarReadWord` calls; DSP `sh`/`sha` are 32-iteration loops (GPU versions are single
   shifts); event scheduler does 4×32-slot double scans per event at ~2,000 events/frame;
   68K runs a 3-call hook chain per instruction that costs 3× the 68K interpreter itself.
   → **P3–P8**
4. **AvP's titledb defaults (2x + true colour) cost ~30% of its frame on the host** — on an
   A10X that is the difference between playable and not. → **P9**
5. Threading: not worth it. 68K/GPU/DSP sync at sub-halfline granularity; the one clean
   producer/consumer boundary (audio resample) is too small to pay for a thread. Don't.
6. Already fine (don't re-propose): computed-goto dispatch (both RISCs), inlined delay
   slots, frontend glue (zero frame copies, one audio batch, geometry only on change),
   vjtrace compiled out of release, crash_detect 256-sample hash/frame, perf probes at slice
   granularity, OP `O(N²)` (teardown-only, 0% of frame), TOM LUT-driven scanline render.

**Related:** ARM64/NEON SIMD tasks — [`perf-audit/simd-neon-arm64.md`](perf-audit/simd-neon-arm64.md); per-chip build tuning —
[`perf-audit/arm-chip-tuning.md`](perf-audit/arm-chip-tuning.md); non-SIMD hot-path followups — [`perf-audit/hotpath-followups-2026-08.md`](perf-audit/hotpath-followups-2026-08.md).

## Evidence

### Host profiles (macOS arm64, `sample`, 12 s each, release `-O3 -g`, fast blitter)

Top-of-stack samples. States: `test/roms/private/states/iron_soldier_v104_f2400.state`,
`Alien vs Predator (1994) corridor.state`.

| symbol | Iron Soldier | AvP | jagniccc |
| --- | ---: | ---: | ---: |
| `DSPExec` + `dsp_executeOpcode` (loop + inlined handlers) | 5278 | 2684 | 4332 |
| `dsp_opcode_jr` | **1303** | 539 | 920 |
| `blitter_generic` | 653 | 706 | 505 |
| `JaguarReadLong` / `JaguarWriteLong` / `JaguarWriteWord` | 584 / 275 / – | 181 / – / 80 | 353 / – / 158 |
| `TOMExecHalfline` (hires render inlined) + `Shadow*` | 36 | **1655 + ~2000** | 87 |
| `OPProcessFixedBitmap` / `OPProcessScaledBitmap` | 278 | 613 | 511 |
| `GetTimeToNextEvent`+`SubtractEventTimes`+`HandleNextEvent` | 331 | 212 | 345 |
| `NVMBiosHook`+`M68KInstructionHook`+`JaguarCDHLEHook` | **310** | – | 310 |
| `m68k_execute` | 103 | – | 150 |
| `GPUExec` + `executeOpcode` | 30 | 366 | 403 |

### Frame-end PC histogram (TEST_EXPORTS build, 1200 frames, `dsp_pc`/`gpu_pc` read after each `retro_run`)

| title | top DSP PC | share | loop (decoded from DSP RAM) | shape |
| --- | --- | ---: | --- | --- |
| Iron Soldier | `$F1B128-12C` | 99.7% | `cmp r28,r2 ; movefa r21,r2 ; jr EQ,-3 ; (ds) addqt #1,r0` | poll alt-bank reg written by I2S ISR |
| AvP | `$F1B130-134` | 74% | same driver, same loop | same |
| Doom | `$F1B092-09C` | 90% | `movei #a,r2 ; load (r2),r1 ; or r1,r1 ; jr EQ,-6 ; (ds) nop` | poll RAM word |
| jagniccc (GPU) | `$F03042/44` | 73% | `cmpq #0,r0 ; jr NE,-2 ; (ds) load (r15),r0` | poll 68K mailbox |
| AvP (GPU) | `$F03118` | 80% | `jr $ ; nop` | pure spin (GPU mostly halted here, cheap) |

`dsp_exec_opcode_count`/frame: IS 590k, AvP 532k, Doom 461k, niccc 417k — i.e. the DSP
always consumes its whole slice budget (≈443k cycles/frame; delay-slot instructions are not
charged by the outer loop, which is why opcodes > cycles).

Slice structure (`src/core/jaguar.c:1683-1752`): one scheduler step per event; EVENT_JERRY
has `JERRYI2SCallback` (~735/field) + `DSPSampleCallback` (~800/field), EVENT_MAIN has
`HalflineCallback` (524/field) → ~2,000 slices/frame of ~550 RISC cycles each. IRQs are
dispatched in `DSPExec`/`GPUExec` at slice entry only (`dsp.c:1070-1100`, `gpu.c:1466`;
plus the in-loop `IMASKCleared` re-check, which only fires after the ISR's own `store`).

## Task list

Columns: impact on frame time (measured where stated, else estimate), risk, effort, who.
"sonnet" = mechanical with the spec below + existing tests; "opus" = needs design.
Every task: branch off `libretro/develop`, C89, `make TEST_EXPORTS=1 test`, acid gate,
`test/tools/ir_ab.sh --ref libretro/develop HEAD` for the Ir delta (load-insensitive), and
a RetroArch smoke. Audio/DSP tasks: BOTH `test_audio_clipping` AND `test_audio_presence`.

### P1 — RISC idle-loop fast-forward (DSP first, then GPU) — HIGH (est. 30-50% of frame on commercial titles) — risk MED — effort L — **opus (design), sonnet (port to GPU)**

Files: `src/jerry/dsp.c` (`DSPExec` ~1070-1160, `dsp_opcode_jr` 1397, `dsp_opcode_jump`),
`src/tom/gpu.c` (`GPUExec` 1429-1570, `gpu_opcode_jr/jump` 1794-1880).

Mechanism (must stay bit-exact — same cycles charged, same register/flag end state,
`dsp_exec_opcode_count` advanced by the skipped instruction count so `crash_detect`'s wedge
test (`crash_detect.c:452-482`) still sees progress):

- Detect a *taken backward* `jr` (offset in [-8, -1]) — optionally `jump` to a nearby PC.
- Candidate body = instructions from target to the `jr` inclusive + its delay slot. Admit
  only: ALU/cmp/btst/move/moveq/movei/movefa/moveta/nop, `addq/addqt/subq/subqt`, and
  **loads whose effective address is plain RAM** (DSP/GPU local RAM or `<0x200000` DRAM),
  never a register-space address (`$F00000-$F1FFFF` minus local RAM — reads there have side
  effects / change without an instruction). Reject any store, any `jump`/`jr` other than the
  loop branch, `load_r14/15`-relative with a register-space base, `mmult/imacn/div` etc.
- Fixed-point test: snapshot (active bank regs, flags) at the loop head; run one
  iteration; snapshot again; run another; if delta(iter2) == delta(iter1) for every register
  and flags identical and the branch was taken both times → affine extrapolation: with
  `n = (cycles_remaining / loop_cost) - 1`, apply `reg += n*delta` for the delta registers,
  `cycles -= n*loop_cost`, `opcode_count += n*len`, then fall through and let the last partial
  iteration run normally. (The Iron Soldier loop has `addqt #1,r0` in the delay slot — a
  monotonic counter — which is why "identical state" alone is not enough.)
- `loop_cost` must be exactly what the interpreter charges per iteration today: sum of
  `*_opcode_cycles[]` for the charged instructions + any `bus_stall`; the inlined delay slot
  is *not* charged by the outer loop (see `gpu.c:1815-1822` comment). Safest: measure it from
  the two probe iterations (`cycles_before - cycles_after`) rather than recomputing.
- Gate OFF when `vjs.gpuPipelineTiming`/DSP pipeline timing is on, when `busArbiter.enabled`
  (`dram_timing`), when `riscClockScalePct != 100`, and under `VJ_TRACE` PCHIST — those
  paths have per-instruction state the extrapolation would have to model. Stock settings
  are the ones the Pi/A10X users run.
- Exit conditions are automatic: the ISR (delivered at the next slice entry) changes the
  polled register/memory, so the next fixed-point probe fails and normal execution resumes.
  A 68K/GPU/OP write to the polled RAM happens between slices for the same reason.
- Savestates: no new state (detection is re-derived every slice). Determinism: identical
  by construction; verify with `test/tools/ir_ab.sh` (Ir will DROP) plus framebuffer+audio
  A/B over ≥8,000 frames on Iron Soldier / AvP / Doom / Wolf3D / Tempest 2000 / Skyhammer
  using the existing A/B sweep tooling (`docs/profiling.md`, memory
  `project_fb_ab_sweep_tooling`). Acceptance = byte-identical framebuffer + audio streams
  with the feature on vs off.
- Measure with `test/tools/test_benchmark` on the three states above; expected DSP time to
  drop by the idle fraction (ISR is ~100-300 cycles of a ~550-cycle slice).
- Prototype order: (1) `jr $ ; nop` and register-only loops (covers IS/AvP and the whole
  Atari sound-driver family), (2) RAM-poll loops (Doom, niccc GPU mailbox), (3) port to GPU.

Also note: `dsp_releaseTimeSlice_flag` / `DSPReleaseTimeslice` are write-only dead code
(`dsp.c:792/811/824`) — not a hook to build on.

### P2 — ELF build flags: visibility / interposition / LTO / Android opt — HIGH for Pi+Android (unmeasured; A/B it) — risk LOW-MED — effort S (flags) + M (validate) — **sonnet**

Files: `Makefile` (unix block ~193, rpi blocks ~320-410, `Makefile.common:158-165`),
`jni/Android.mk`, `link.T`, `link-test.T`, `exports.list`.

- Add `-fno-semantic-interposition` (GCC; clang no-op) to all ELF `-fPIC` targets — safe
  first cut, zero source change.
- Add `-fvisibility=hidden` for `!TEST_EXPORTS` builds (the white-box test ABI dlsyms
  internals via `link-test.T`, so keep test builds default-visibility, or introduce a
  `VJ_TEST_EXPORT` attribute later). `retro_*` stay exported via `RETRO_API`.
  `scripts/build-id.sh` / `test/tools/simd_matrix_check.sh` already assert build identity —
  extend the matrix with a `readelf`/`nm -D` row so a hidden-visibility regression is
  caught like #516 was.
- LTO (`-flto`) on `unix`/`rpi*`: flip, run acid + `make TEST_EXPORTS=1 test` + device
  smoke; LTO can surface latent C89 UB. Measure with `ir_ab.sh 'OPT…' 'OPT… LTO=1'` — Ir
  may rise while wall time drops; wall-clock A/B/B/A on the Pi is the verdict.
- `jni/Android.mk`: `COREFLAGS` currently `-DINLINE=inline -D__LIBRETRO__ $(INCFLAGS)
  $(BLITTER_SIMD_DEFINE)` — add `-O3 -DNDEBUG -fvisibility=hidden -fno-stack-protector
  -fomit-frame-pointer` (LOCAL_CFLAGS win over NDK defaults; verify with `ndk-build V=1`).
- Darwin targets are unaffected (two-level namespace → `dso_local`), which is why the
  A10X needs P1/P3+ rather than this.
- Optional later: PGO (`-fprofile-generate/-use`) — `make benchmark` is a deterministic
  training workload; needs a corpus + staleness guard; design first.

### P3 — Fast blitter per-pixel dispatch — MED-HIGH of blitter time (blitter = 5-17% of frame) — risk LOW — effort S-M — **sonnet**

`src/tom/blitter.c`: `READ_PIXEL_*`/`WRITE_PIXEL_*`/`READ_ZDATA_16`/`WRITE_ZDATA_16`/
`READ_RDATA_*` macros (264-345) call `JaguarRead*/Write*` (5-7 range compares, cross-TU call)
3-6× per pixel inside `blitter_generic` (553-1158). `blitter_read_byte/word/long` /
`blitter_write_*` (60-140, `BLITTER_ALWAYS_INLINE`, `<0x200000` fast path + identical
fallback) already exist and are used only by the accurate engine (≥2167). Route the macros
through them. Gate: `test/tools/test_blitter_compare` (fast vs accurate, 0 divergent) +
framebuffer A/B. Bundle: cache `REG(A1_FLAGS)`/`REG(A2_FLAGS)` in locals before the pixel
loop (re-read 30+ times per pixel, loop-invariant: no `blitter_ram` write until 1152-1154);
make `blitter_generic` `static`. Later (needs design, L): hoist the ~20 loop-invariant
command-bit branches out of the per-pixel loop by specialising the common shapes (the
accurate engine's "COLLAPSED INNER LOOP — PATTERN FILL" at ~2500 is the template); add
`PERF_COUNTER`s to the fast engine first so shapes are chosen by measurement.

### P4 — `GPUReadLong` DRAM fallback — MED of GPU LOAD cost — risk ~0 — effort S — **sonnet**

`src/tom/gpu.c:885`: `return (JaguarReadWord(offset, who) << 16) | JaguarReadWord(offset + 2, who);`
→ `return JaguarReadLong(offset, who);` (`jaguar.c:1122` has the one-check `GET32` fast path
and an identical two-word fallback; `DSPReadLong` and `GPUWriteLong` already do this).
`loadp` pays it twice. Byte-identical. While there: delete the dead `gpu_opcode[64]`
function-pointer table (`gpu.c:524`, only use is `#if 0` at 1509 — keeps 64 out-of-line
handler copies alive), make `gpu_opcode_cycles[64]`/`gpu_convert_zero[32]` `static const`
(`gpu.c:512/653`), and consider turning the malloc'd `branch_condition_table`
(`gpu.c:656`, shared with dsp.c) into a `static const` baked table (verify byte-identical).

### P5 — DSP `sh`/`sha` 32-iteration loops — MED (depends on SH frequency in sound drivers) — risk ~0 — effort S — **sonnet**

`src/jerry/dsp.c:1999-2030` (`sha`), `2039-2073` (`sh`): `while (shift) { _Rn <<= 1; … }`.
Port the GPU versions verbatim (`gpu.c` `gpu_opcode_sh` 2668, `gpu_opcode_sha` 2641: single
native shift with the `>=32` guard). Flags/carry semantics must match the loop exactly
(C = bit shifted out first). Audio test pair required.

### P6 — Event scheduler O(32) double scans — MED (~3-4% measured on host) — risk LOW-MED — effort S-M — **sonnet**

`src/core/event.c`: two 32-slot lists, `GetTimeToNextEvent` ×2 + `SubtractEventTimes` +
`HandleNextEvent` each scan all 32 per scheduler step, ~2,000 steps/frame, `double` math.
(a) keep live events packed (swap-remove) so scans are O(live≈5-8); (b) cache
next-event time per list and only rescan on insert/remove; (c) `SubtractEventTimes` →
a per-list time base (subtract once, not per slot). Savestate format (`EventStateSave/Load`
307-393) serialises by position + callback id — keep it stable or bump with the next
version. Also consider merging `JERRYI2SCallback` and `DSPSampleCallback` cadences if they
are the same period (halves JERRY steps) — check `dac.c:329/371` vs `jerry.c:383` first.

### P7 — 68K per-instruction hook chain — LOW-MED (~3% measured on Iron Soldier / niccc) — risk LOW — effort S — **sonnet**

`src/core/jaguar.c:364-410` `M68KInstructionHook` runs per 68K instruction
(`m68kinterface.c:203`): traceback ring store + `JaguarCDHLEHook()` + `NVMBiosHook()` +
strategy hook — three cross-TU calls that each immediately reject. Measured: the three hooks
(310 samples) cost 3× `m68k_execute` (103). Fix: one precomputed `m68kHookPCs` fast
reject — e.g. a global `uint32_t hookMask` / small sorted range table rebuilt whenever
`hle_active`, `jaguarMemTrackInserted`, or `bootConfig.strategy` change, and check it inline
before any call; keep the traceback ring store (cheap, needed by crash reports) or make it
`startM68KTracing`-gated too if the PC ring is only read on crash.

### P8 — Minor mechanical — LOW each — **sonnet**, bundle with neighbours

- `tom.c:1541-1547` BG line-buffer clear: 1440 byte stores → 720 16-bit stores / `memset`
  when hi==lo (only when BGEN set; check codegen first — may already vectorise).
- `libretro.c:2879-2901, 2951-2973`: 24 unconditional `input_state_cb(KEYBOARD)` numpad
  fallbacks per frame — gate on a keyboard being present.
- `jlink.c:666-682`: `clock_gettime` per frame with netlink off — skip when disabled.
- `dsp.c:2140-3097`: ~960 lines of dead second interpreter (`DSP_add`…`DSPOpcode[]`,
  `DSPExecP/P2/Comp` prototypes without bodies) — zero runtime cost; cleanup PR only,
  touches savestate layout (`pipeline[]`/`scoreboard[]` still serialised) → version bump.

### P9 — Enhancement defaults vs. host capability — HIGH for AvP/Doom on slow hosts — risk LOW — effort S-M — **sonnet** (policy decision: user)

`src/core/titledb.c:122-135` turns on `internal_resolution=2x` + `true_color` for AvP
(Doom likewise per memory). Host profile: `TOMExecHalfline` (hires render inlined) +
`Shadow*` ≈ 30% of AvP's frame. Options: (a) a core option "enhancement profile:
auto/quality/performance" where `auto` disables titledb enhancements on platforms with
`HAVE_NEON` 32-bit / `rpi*` / when `retro_run` overruns the frame budget for N frames;
(b) make the hires path cheaper (`tom.c:916-1021` per-subpixel shadow-tag lookups). Do (a)
first — it is a few lines and recovers ~30% on the exact titles people test.

### Not recommended / rejected (so nobody re-spends the tokens)

- Threading (DSP thread, OP pipelining, framebuffer-convert thread, audio thread) —
  sub-halfline sync; `GPUSyncToM68K` fires on ordinary mailbox polls; conversion *is* the
  render; audio resample is too small. See `perf-audit/bus-frame.md` §7.
- 68K JIT / core swap — 0.7-2.6% of frame; licence issues (prior decision).
- `-Ofast`/`-ffast-math` — #517 (determinism).
- Flag packing / lazy flags in the RISC interpreters — deliberate, commented rationale; not
  the bottleneck (idle loops are).
- Caching `gpu_reg` bank pointer in a local — it legitimately changes mid-slice (STORE to
  G_FLAGS).
- DSP/GPU `div` 32-iteration loops — likely load-bearing for bit-exact 16.16 semantics
  (SCPCD comment); JTRM check before touching.
- OP `OPObjectExists` O(N²) — teardown-only; the #123 "~1%" was wrong.

## Measurement recipes

- Host hot-function profile (attribution only, load-insensitive enough):
  `make RELEASE_DEBUG_INFO=1` → `test/tools/test_benchmark <dylib> <rom> 6000 --warmup 60
  --blitter fast --load-state <state> & sample $! 12 -file out.txt` → read "Sort by top of
  stack". Per-commit deltas: `test/tools/ir_ab.sh --ref libretro/develop HEAD` (cachegrind
  Ir, zero-variance; Ir can rise while wall time falls for inlining/unrolling changes).
- Frame-end RISC PC histogram (idle-loop finder): build `TEST_EXPORTS=1`, dlsym
  `dsp_pc`/`gpu_pc`/`JaguarReadWord` and sample after each `retro_run` (the harness used
  for this audit was a 40-line patch of `test/tools/test_benchmark.c`; worth landing as
  `test/tools/risc_pc_histogram.c` next to `m68k_pc_histogram.c`).
- On-device: the `RETRO_ENVIRONMENT_GET_PERF_INTERFACE` counters (`device_perf.sh`,
  `rpi_perf.sh`) give the **subsystem** split only (8 brackets, no stacks) — run first to
  confirm the ordering on the A10X (#509's unmet exit criterion; iPad Pro 2 = A10X). For
  **function/line** attribution use Instruments Time Profiler: `make platform=ios-arm64
  RELEASE_DEBUG_INFO=1` keeps `-O3 -g`; nothing in `code-sign-cores.sh` or the RetroArch
  Xcode project strips (`COPY_PHASE_STRIP=NO`, no `STRIP_INSTALLED_PRODUCT`); the
  `RetroArch iOS Release` scheme's ProfileAction already builds Release; `dsymutil` the dylib
  so Instruments matches by UUID; Product ▸ Profile ▸ Time Profiler, Invert Call Tree +
  Flatten Recursion. Scriptable via `xctrace record --template 'Time Profiler' --device
  <udid> --attach RetroArch --time-limit 30s`. Full recipe: `perf-audit/device-profiling.md`.
  Gap worth filling: a `device_perf.sh profile` subcommand wrapping `xctrace`.
