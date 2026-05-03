# Acid-test ROM toolkit

Synthetic Jaguar ROMs that exercise specific hardware corners --
blitter modes, GPU/DSP cross-talk, beam chasing, OP scenarios, IRQ
delivery, HLE-vs-real-BIOS divergence -- and report PASS / FAIL to
the host via a fixed RAM signature.

## Why

1. **Reproducible perf benchmarks** that don't depend on commercial
   ROMs (which we cannot ship). Each acid test is small (typically
   <8 KB), open-source, and exercises a single feature so we can
   attribute regressions cleanly.
2. **Bug-finding under stress.** Commercial games hit wide
   combinations of features but only the combinations *they happen
   to use*. Acid tests exhaustively walk a feature axis (every
   pixsize, every phrase/non-phrase, every Z-mode) and catch
   divergence between fast and accurate blitters, between our
   implementation and the hardware reference, and between successive
   emulator versions.
3. **Documenting reality.** We *expect* many tests to fail today --
   the emulator is deliberately not cycle-accurate, the OP timing is
   loose, bus contention is unmodelled, and HLE BIOS doesn't match
   real BIOS in many places. Each failing test is a checked-in
   description of a known accuracy bug, which is more useful than
   prose in `docs/TODO`.

## Status

Early but live. Framework runs; per-test PASS/FAIL with diagnostic
codes; per-test perf-counter delta dumps when built with
`BENCH_PROFILE=1` (the default for `make acid`). vasm assembler is
optional -- if absent, the assemble step is skipped with a warning
and only the runner harness is built.

**52 / 72 tests PASSing across 13 categories.**  Failures and
NOT-RUN-YETs are intentional documentation of known emulator gaps.

| Category | Tests | Pass | Open issues surfaced |
|---|---:|---:|---|
| smoke      |  1 |  1 | — |
| memory     |  8 |  8 | — |
| timing     |  9 |  8 | jerry_pit_setup: PIT readback returns 0 |
| irq        |  9 |  6 | vblank_delivery + jerry_pit_irq + rapid_irq_pump NOT-RUN-YET (IRQ raises in TOM/JERRY per perf counters but never reaches 68K vec-64 handler) |
| blitter    | 17 |  4 | 13 SRC-reading tests fail identically; lfu_zero_fill / lfu_one_fill / lfu_invert_src PASS — narrows bug to LFU source-routing |
| gpu        |  2 |  2 | — (gpu_basic_run + gpu_reg_access) |
| dsp        |  3 |  3 | dsp_mac_accumulator is currently a NOP-loop placeholder; real 40-bit-MAC math is a follow-up |
| op         |  3 |  3 | — |
| bus        |  2 |  1 | blitter_back_to_back: same root cause as blitter category |
| hle        |  6 |  6 | — |
| quirks     |  7 |  6 | divl_zero_traps: DIVS.L #0 doesn't trap to vec 5 (path code looks correct per agent trace; needs investigation) |
| stress     |  3 |  2 | many_blits: same blitter root cause |
| perf       |  3 |  3 | — |

**Real bugs surfaced as failing tests** (each ready as a regression
gate for a focused fix-PR):

1. **Blitter source-data routing** — 13 of 14 SRC-reading tests
   fail identically (`observed=0`, perf counters confirm blit ran).
   PASS exceptions narrow the bug:
   - LFU=$0 (always 0), LFU=$F (always 1) PASS — output ignores SRC
   - LFU=$3 (~S) PASS — *anomaly*, suggests bug isn't a flat
     "SRC read = 0" but in how SRC routes through the LFU
2. **IRQ delivery to 68K vec 64** — TOM/JERRY raise IRQs (counters
   tick), 68K handler never fires.  `vector_64_writable` PASSES,
   so the vector-write path itself is fine; bug is in IPL ack /
   vector fetch.  Likely load-bearing for Doom #131.
3. **JERRY PIT register readback** returns 0 despite commit
   `1ca2fdc` claiming to fix it.
4. **DIVL zero-divide trap** doesn't fire — tracing in the agent
   report suggests the code path is correct but the trap doesn't
   reach the handler.

## Layout

```
test/acid/
   README.md            -- this file
   Makefile             -- assembles tests/*.s into .jag ROMs (vasm)
   run.c                -- harness: dlopen core, load ROM, read signature
   include/
      jaguar_header.s   -- minimal Jaguar cart header + entry vector
      acid_test.s       -- pass/fail signature macros
   tests/
      blitter/          -- blitter mode matrix
      gpu/              -- GPU coprocessor
      dsp/              -- DSP coprocessor
      op/               -- Object Processor
      timing/           -- VC/HC/PIT/halfline rate
      irq/              -- VBlank, JERRY PIT, GPU/DSP IRQ delivery
      bus/              -- 68K + GPU + Blitter concurrent access
      hle/              -- HLE BIOS vs real BIOS divergence
      memory/           -- RAM/ROM/mirror/endianness/access widths
      quirks/           -- documented hardware quirks + commercial hacks
      stress/           -- heavy concurrent workloads (AvP-style)
      perf/             -- predictable cycle-stress workloads
```

## How a test reports its result

Tests write a four-word "acid signature" block at fixed RAM offset
`$100000` (1 MB into main RAM, well clear of the 68K vector table at
`$0..$3FF`, BIOS workspace, cart-mode stack at `$4000`, and typical
RAM-loaded executable region).

```
$100000 ACID_RESULT     $12345678 = pass
                        $DEADBEEF = fail
                        $00000000 = not-run-yet (test crashed or never wrote)
$100004 ACID_DETAIL     test-specific code (sub-test ID)
$100008 ACID_OBSERVED   value the test actually got (on FAIL)
$10000C ACID_EXPECTED   value the test was looking for
```

The runner reads main-RAM via `retro_get_memory_data(SYSTEM_RAM)`
after running N frames and prints PASS / FAIL / NOT-RUN-YET with
diagnostics.

Exit code: 0 for pass, 1 for fail/not-run, 2 for harness error.

## Per-test perf summary

When the core is built with `BENCH_PROFILE=1` (the default for
`make acid`), the runner snapshots a set of perf counters before /
after each test and dumps the delta:

```
[PASS       ] tests/timing/vc_per_frame.jag
              perf: timing_jaguar_execute_calls=600 timing_halfline_callbacks=314400
```

That tells us at a glance:
- the test ran for 600 retro_run cycles (10 emulated seconds at 60 Hz)
- the halfline callback fired 314400 times = exactly 524 per frame
  (NTSC), which is what the hardware spec calls for

If a future change makes the halfline rate jump to 1048800 (1048
per frame), this number will catch it immediately even if no test
explicitly checks for it.

Counters surfaced in the per-test summary today:

| Counter | Source | Expected (NTSC default) |
|---|---|---|
| `timing_jaguar_execute_calls` | `JaguarExecuteNew` entry | 1 per `retro_run()` |
| `timing_halfline_callbacks` | `HalflineCallback` entry | 525 per frame |
| `timing_vblank_irqs` | TOM video-int raise | 1 per frame |
| `timing_jerry_irqs` | JERRY PIT IRQ raise | 0 unless game enables PIT |
| `timing_gpu_irqs_to_68k` | TOM PIT-→68K raise | 0 unless game enables TOM PIT |
| `blitter_calls` | `BlitterMidsummer2` entry | game-dependent |
| `blitter_outer` | blitter outer-loop iter | game-dependent |
| `blitter_inner` | blitter inner-loop iter | game-dependent |
| `blitter_phrase_reads` | source/dest phrase loads | game-dependent |
| `blitter_phrase_writes` | dest phrase stores | game-dependent |

Add new counters in the file that increments them (`PERF_COUNTER`
is file-scoped) and append the name to `kPerfCounters[]` in
`test/acid/run.c` to surface in the summary.

## Building & running

vasm (motorola syntax) is the assembler. Three options:

**Option A — local source build (current default).** Build from the
`prb28` GitHub mirror (the upstream `sun.hasenbraten.de` site is
sometimes unreachable):

```bash
git clone --depth 1 https://github.com/prb28/vasm.git /tmp/vasm
cd /tmp/vasm && make CPU=m68k SYNTAX=mot
sudo install vasmm68k_mot /usr/local/bin/
```

**Option B — Docker image (recommended for CI).** A couple of
ready-made Jaguar-toolchain images vendor vasm + vlink + vbcc:

- `toarnold/jaguarvbcc` -- Docker Hub:
  https://hub.docker.com/r/toarnold/jaguarvbcc/
- `Leffmann/vasm` -- GitHub:
  https://github.com/Leffmann/vasm

Wire either into a CI job that volume-mounts the repo and runs
`make acid` inside the container; the image already has `vasmm68k_mot`
on `$PATH`.

**Option C — alternative assembler.** `rmac` (Reboot's modern fork
of Atari's `smac`) also assembles 68K motorola syntax for Jaguar,
though our test sources currently target vasm idioms.  See
https://www.commodore-news.com/news/item/13087/en/desktop for
context on the wider Jaguar/Atari toolchain landscape.

Then from the repo root:

```bash
make acid                                       # build core + tests + run
make -C test/acid clean                         # clear build artifacts
```

Or for one specific test:

```bash
make BENCH_PROFILE=1 TEST_EXPORTS=1             # build core
make -C test/acid acid_run                      # build harness
test/acid/acid_run \
   ./virtualjaguar_libretro.dylib \
   test/acid/tests/timing/vc_per_frame.jag \
   600                                           # 600 frames
```

## Writing a new test

Template:

```
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT
                ; ... your test code ...
                ; PASS:                          fall-through to ACID_PASS
                ; FAIL: ACID_FAIL detail,observed,expected
                ACID_PASS
```

Macros:

| Macro | Effect | Clobbers |
|---|---|---|
| `ACID_INIT` | clear signature block to NOT-RUN-YET | d0, a0 |
| `ACID_PASS` | write PASS magic, halt forever | d0 |
| `ACID_FAIL d,o,e` | write FAIL + 3 diagnostic words, halt | d0 |

`ACID_FAIL` accepts any operand `move.l` accepts (registers OR
`#imm`):

```
ACID_FAIL #5,#$DEAD,#$BEEF       ; all immediates
ACID_FAIL d3,d5,d4               ; all from registers
ACID_FAIL #1,d2,#0               ; mixed
```

The runner runs your test for 600 emulated frames by default
(10 seconds at 60 Hz NTSC). If your test needs longer, pass an
explicit count: `acid_run <core> <rom.jag> <num_frames>`.

## Test categories (planned)

This is the long-form roadmap. Tests land incrementally; each
landing PR fills in part of one category. `[OK]` = at least one
test landed, `[--]` = none yet.

### `smoke/` `[OK]`
The tests every test depends on. If anything here fails, the rest
of the suite is meaningless until smoke passes again.

- `zzz_smoke.s` `[PASS]` -- ACID_INIT + ACID_PASS, no real work

Future:
- "M68K reset PC matches cart entry vector" (verifies HLE init)
- "Vector table is filled (no PRNG garbage at $100)"

### `timing/` `[OK]`
Frame-pacing and counter-rate tests. **High priority** -- the Doom
1.5-2x speed regression (issue #131) lives in this category.

- `vc_advance.s` `[PASS]` -- VC must change at all
- `vc_per_frame.s` `[PASS]` -- VC sweeps once per frame, ~60 frames/sec

Future:
- HC advance rate within a scanline (matches HP halfline period)
- VBlank rate matches NTSC 60 Hz / PAL 50 Hz exactly
- VC field-bit (#11) toggles between fields
- JERRY PIT divider rate
- TOM PIT divider rate
- Halfline IRQ delivery jitter (target: <1 halfline)
- Frame-tear test: VC poll-loop catches the right cycle to update
  palette mid-frame

Original `docs/TODO` items relevant here: _"Fix VC behavior to
match what a real Jaguar does"_ (still open per Shamus' notes),
_"Cycle accuracy for GPU/DSP/OP/Blitter"_.

### `irq/` `[OK]`
Interrupt delivery from each subsystem. `irq_ack_handler()` returns
vector 64 for ALL hardware IRQs, so we patch vector 64 and watch a
shared flag.

- `vblank_delivery.s` `[NOT-RUN-YET]` -- VBlank IRQ should bump a
  counter; currently the IRQ raises in TOM (`timing_vblank_irqs`
  counter ticks) but the 68K handler at vector 64 doesn't fire.
  Real bug surface -- either the IPL ack path or our vector-64
  patch is wrong.

Future:
- JERRY PIT timer 1 / timer 2 IRQ delivery
- TOM PIT IRQ delivery
- DSP IRQ -> 68K via JERRY external
- GPU IRQ -> 68K
- IRQ priority cascade (higher takes over lower)
- Nested IRQs
- IRQ ack timing after the handler RTEs

Original `docs/TODO` items: _"DSP code needs to be rewritten"_
(historical; some of that flowed through), _"Need to emulate bus
contention"_ (affects IRQ ack timing).

### `blitter/` `[OK]`
Blitter mode matrix. The biggest accuracy axis we have -- two paths
(fast `blitter_generic` and accurate `BlitterMidsummer2`) that
*should* produce bit-identical output but often don't.

- `zzz_smoke.s` `[PASS]` -- placeholder; no blitter touched
- `copy_simple.s` `[NOT-RUN-YET]` -- 8-phrase round-trip copy;
  partially executes (`blitter_calls=1, inner=2`) then crashes

Future:
- One copy test per pixsize (1, 2, 4, 8, 16, 32 bpp)
- Phrase mode vs pixel mode at each pixsize
- Z-buffer modes (zmode 0..7)
- Gouraud shading (GOURD)
- Z-interpolation (GOURZ)
- SRCSHADE
- BCOMPEN bit pattern compositing (used for font rendering)
- DCOMPEN data compare (transparent color)
- BKGWREN (write background color)
- LFU functions (16 source/dest combos)
- Wide blits (multi-phrase rows)
- Tall blits (multi-line)
- Clipping (CLIPA1)
- Step modes (XADDPHR, XADDPIX, XADD0, XADDINC)
- A1 vs A2 source/dest swap
- **Fast vs accurate blitter divergence**: run each test twice,
  compare results bit-for-bit

Original `docs/TODO`: _"Blitter needs fixing"_, _"Need to propagate
blitter fixes in the A1 <- A2 direction to the A1 -> A2 direction"_.

### `op/` `[--]`
Object Processor scenarios.

Future:
- STOP object terminates list correctly
- Bitmap object render at every pixsize
- Scaled bitmap (HSCALE, VSCALE)
- Branch object (conditional, on YPOS / VC)
- GPU-interrupt object
- OP-list cycle detection
- REFLECT / RMW / TRANS modifiers
- Palette indexing (CRY vs RGB)
- OP timing budget per halfline

Original `docs/TODO`: _"Need to fix timing in the OP. As it is now,
it gives a false impression of how much it's capable of."_

### `gpu/` `[--]`
GPU RISC instruction coverage + 68K-side register access.

Future:
- One test per GPU opcode (~64 of them)
- Register file access from 68K via $F02100..
- GPU IRQ to 68K
- GPU stop / restart
- GPU-Blitter handshake (program GPU to issue blits, poll BUSY)
- DIVQ semantics
- IMACN accumulator
- Branch conditions

### `dsp/` `[--]`
Same shape as GPU but DSP-specific.

Future:
- All DSP opcodes
- 40-bit MAC accumulator (we have `src/jerry/dsp_acc40.h`; needs
  cycle-accurate test)
- DSP IRQ delivery
- I2S sample-clock (SCLK) rate matches configured divider
- Audio sample buffer fill rate (catches buffer over/underrun
  symptoms before they reach the user)
- DSP <-> 68K mailbox
- DSP <-> GPU memory access through TOM bus

Original `docs/TODO`: _"DSP code needs to be rewritten"_.

### `bus/` `[--]`
Bus contention / arbitration. We don't model bus contention today,
so these tests will mostly **fail by design** until we do -- which
is exactly the point.

Future:
- 68K + GPU concurrent main-RAM read race
- Blitter + 68K concurrent main-RAM access
- Memory bandwidth ceiling (sum of throughput across masters)
- Refresh cycles stealing bus time

Original `docs/TODO`: _"Need to emulate bus contention"_ (literally
listed by Shamus as still-open).

### `hle/` `[--]`
HLE BIOS vs real BIOS divergence. Each test runs once with
`virtualjaguar_bios=disabled` (HLE) and once with `enabled` (real
BIOS); both must produce the same observable result for accuracy.

Future:
- 68K register state immediately after reset
- GPU register state
- DSP register state
- JERRY clock dividers (CLK2, CLK3)
- I2S setup (SCLK, SMODE)
- TOM border colour (BORD1/2)
- Vector table contents
- HLE_BIOS_WORK_FLAG_ADDR ($0804) value
- Cart authentication GPU magic at $F03000

### `memory/` `[--]`
Address-space behaviour.

Future:
- Main RAM read/write at every width (8/16/32/64-bit)
- Cart ROM read at every width
- GPU local RAM ($F03000..)
- DSP local RAM ($F1B000..)
- Mirror addresses (Jaguar has several)
- Endianness consistency (big-endian Jaguar on LE host)
- Open-bus reads
- Write-only and read-only register correctness

### `quirks/` `[--]`
Documented Jaguar 1 hardware quirks and known commercial hacks.
A test here is a contract: "the emulator must reproduce this
quirk because game X depends on it."

Future:
- A2 yadd tied to A1 yadd (Jaguar 1 bug)
- BSR.L $61FF (Atari `aln` linker absolute address quirk)
- 68020 MULL/DIVL trap (Removers Library / m68k-atari-mint-gcc)
- DSP MAC pipelining quirks
- OP scaling underflow / wrap behaviour
- Doom pwidth=8 pixel-replication (now in scanline renderers)

### `stress/` `[--]`
AvP-style heavy concurrent workloads. These won't fit the 16.6 ms
frame budget on slow hosts -- the goal is to detect *regressions*
in our own throughput.

Future:
- 2000+ small blits per frame (mimics AvP gameplay)
- Concurrent GPU + Blitter + DSP at max sustained rate
- 68K AI-style logic + heavy blitter
- Pathological ADDARRAY input (every daddasel/daddbsel combo)

### `perf/` `[--]`
Predictable cycle-stress workloads we can measure across emulator
versions to characterise throughput change.

Future:
- N-iteration GPU loop (predictable instruction count)
- N-iteration DSP loop
- N-byte memcpy via 68K
- N-byte blitter copy
- Fixed-rate audio sample budget

## Caveats

- The boot stub assumes the **HLE BIOS** path is in use
  (`virtualjaguar_bios=disabled`); the runner sets that variable
  unconditionally. Real-BIOS testing is a separate axis (see `hle/`).

- Tests halt by `bra.s .` at end -- they don't return to a host
  scheduler. The runner runs N frames and reads the signature; if
  the test crashed before writing, you get NOT-RUN-YET.

- `vasm` license is "free for non-commercial use" with conditions.
  We use it as a build-time tool only; nothing assembled by vasm
  ships in the libretro core. See `prb28/vasm` for the source we
  build from.

- Nothing in here yet runs in CI. Once the test set stabilises and
  vasm install is documented in CI, we'll add a job that runs `make
  acid` and gates merges on it.

## See also

- [`docs/TODO`](../../docs/TODO) -- original devs' (Shamus, CJ,
  nwagenaar) outstanding accuracy / feature TODO list. Several
  items there map directly onto categories above (cycle accuracy,
  VC behaviour, OP timing, bus contention, blitter A1/A2
  propagation).
- [`docs/profiling.md`](../../docs/profiling.md) -- general profiling
  guide; covers `BENCH_PROFILE=1`, `xctrace` wrapper, and the perf
  counter system this toolkit uses.
- [`docs/emulation-bug-hunt-todos.md`](../../docs/emulation-bug-hunt-todos.md)
  -- our active bug-hunt notes; converging with acid coverage over
  time.
- Issue #131 -- Doom game logic / demos run 1.5-2x too fast. Will
  be reproduced + bisected once `timing/` and `irq/` tests cover the
  surface.
