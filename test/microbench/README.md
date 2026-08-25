# Purpose-built microbenchmark ROMs (#536)

Small, single-engine Jaguar cartridges with a **fixed iteration count** and a
**harness-assertable completion marker**. They fill the gap #533 identified
after auditing the public ROM corpus (jagniccc/yarc): real titles never give
single-engine isolation or an exact, deterministic termination.

| ROM | engine | DONE magic | status |
|---|---|---|---|
| `bench68k.j64` | 68000 only | `$C0DE0068` | shipped (task 1) |
| `benchgpu_arith.j64` | GPU, arithmetic-heavy | `$C0DE0A01` | shipped (task 2) |
| `benchgpu_branch.j64` | GPU, branch-heavy | `$C0DE0B02` | shipped (task 3) |
| `benchdsp.j64` | DSP | `$C0DE0D53` | shipped (task 4) |
| `benchblit.j64` | blitter | `$C0DE0B17` | shipped (task 5) |

Run them:

```bash
make microbench          # builds the core (TEST_EXPORTS=1) + tools, runs all
```

Each tool prints one machine-parseable line; `done_frame` is the point of the
exercise:

```
MICROBENCH engine=68k done=1 done_frame=114 start_frame=1 count=1000000 expect_count=1000000 budget=600
```

The `.j64` images are **committed**, so CI never needs an assembler. Regenerate
them only after editing a `.s`:

```bash
make jaguar-toolchain-build
eval "$(tools/jaguar-toolchain/setup.sh env)"
test/microbench/build.sh
```

---

## The cart bootstrap

`cartboot.inc` holds it; every ROM includes it first in `.text`. It is the
authority — this section explains *why* those bytes.

### Header layout

```
$800000 .. $8003FF   zero fill        (retail carts put the encrypted
                                       BootIntro block here; unused by us)
$800400              04 04 04 04      universal marker / cart-type byte
$800404              00 80 20 00      68K entry vector -> $802000
$800408              00 00 00 00
$80040C .. $801FFF   $FF fill
$802000              first instruction
```

This is **byte-identical** to the two public ROMs already in the repo. Both
`test/roms/yarc.j64` and `test/roms/jagniccc.j64` read, at file offset `$400`:

```
00000400: 0404 0404 0080 2000  ...... .
```

That was the working reference; the layout was pattern-matched from it rather
than inferred from spec text.

### What the core actually does with those bytes

Three separate consumers, all in this repo — verified by reading them, not
assumed:

1. **`src/core/file.c` `ParseFileType()`** — a sub-1 MiB image is accepted as
   `JST_ROM` only if `GET32(buffer, $400) == $04040404`
   (`CART_UNIVERSAL_MARKER` at `CART_UNIVERSAL_MARKER_OFFSET`). Without the
   marker our 8 KB cart would fall through to the raw-binary / unrecognized
   arms. (An exact-megabyte file is accepted on size alone, but padding five
   ROMs to 1 MiB each just to skip a header is not a trade worth making.)
2. **`src/core/file.c` `DetectPrependedHeaderSize()`** — reads file `$600`
   (= `512 + $400`) looking for the same marker to decide whether a 512-byte
   copier header is glued to the front. Our `$FF` padding there is not the
   marker, so nothing is stripped. **Keep the `$40C..$1FFF` fill non-`$04040404`.**
3. **`src/core/jaguar.c` HLE BIOS state** — `cartTypeByte =
   jagMemSpace[CART_HEADER_BASE]` (`$800400`), and `MEMCON1 = MEMCON1_BASE |
   (cartTypeByte & MEMCON1_CART_MASK)` → `$1861 | $04` = `$1865`. The marker's
   first byte doubles as the cart-type byte, so we inherit exactly the MEMCON1
   a commercial cart gets.

The entry vector at `$800404` is read by `file.c`, range-checked (must be even
and land in main RAM / cart ROM / boot ROM), stored in `jaguarRunAddress`, and
copied by `JaguarReset()` to main RAM `$000004` — which is where the 68000
fetches its reset PC. `$00802000` passes the check.

### Real-BIOS mode does **not** work — measured, not assumed

The plan's architecture note ("no BootIntro / jagcrypt") is confirmed
empirically. All runs 300 frames, sentinels read from main RAM afterwards:

| image | mode | final 68K PC | sentinels |
|---|---|---|---|
| `bench68k.j64` | HLE (harness default) | `$80202C` (the `bra.s *` park) | all written |
| `bench68k.j64` | `--bios` | `$0050B6` | untouched |
| jagcrypt `-u` signed variant | `--bios` | `$0059A0` | untouched |
| jagcrypt `-u` signed variant | HLE | `$80202C` | all written |

The pinned `pc_jagcrypt` *will* run (`jagcrypt -u bench68k.rom` produces a
`.U1`, contrary to what the `-tursi` gap might suggest), but the RSA data it
generates is not what the emulated Series K BIOS validates against, so the
signed image is no more bootable under `--bios` than the plain one. It is also
worse for our purposes: the signing pass is not byte-reproducible.

**Conclusion: these ROMs are HLE-boot only.** That is fine — `test/harness/`
defaults to HLE (`--bios` is opt-in) and the microbench tools never pass it.

---

## Sentinel convention — all five ROMs

`$010000`–`$01000F` is the **reserved sentinel block**. Benchmark working
buffers (blitter destinations, GPU scratch) must live at **`$020000` or
above** so they can never reach it.

| address | meaning |
|---|---|
| `$010000` | START magic `$C0DE57A7` — written at the entry point by every ROM |
| `$010004` | DONE magic — per-engine, see the table at the top |
| `$010008` | iterations actually executed |
| `$01000C` | reserved |

Write the **count before the magic**, so a reader that sees DONE is guaranteed
to see a settled count.

Why `$010000` and not the plan's sketched `$001000`: `HLE_SSP_CART` is `$4000`
(`src/core/jaguar.c`), and the 68K stack grows down from there straight through
`$1000`. `$010000` sits above the stack, above the HLE vector table
(`$0`–`$3FF`) and its RTE stubs (`$400`/`$404`), and well inside the 2 MB of
main RAM. Main RAM reads back all-zero at reset in HLE mode, so "never ran" is
distinguishable from any magic value.

Having **both** a START and a DONE marker is what makes a failure legible: the
tool reports `start_frame=-1` ("ROM never booted") separately from
`done_frame=-1` with `start_frame=1` ("booted, didn't finish in budget").

---

## Frame budget

The harness runs a fixed frame count and then reads memory; the ROM never has
to signal exit (each parks in `bra.s *`). **The budget must exceed the loop's
completion frame**, and completion moves with the core's timing model.
Measured for `bench68k.j64` (1,000,000 iterations):

| configuration | done_frame |
|---|---|
| harness defaults | 114 |
| `gpu_pipeline_timing=enabled` | 114 |
| PAL | 114 |
| `dram_timing=enabled` | 128 |
| both + `VJ_DRAM_SCALE=8` | 228 |
| both + `VJ_DRAM_SCALE=12` | 288 |
| both + `VJ_DRAM_SCALE=16` | 350 |

`dram_timing` and `gpu_pipeline_timing` both default to `disabled`, so the
default path is 114. The tools use **600** — clears the slowest measured
configuration with ~1.7x margin, at about a second of wall time.

Sanity check on 114: the 3-instruction body costs a nominal 26 cycles on a
68000 (`addq.l` 8 + `subq.l` 8 + taken `bne.s` 10), so 26 M cycles at ~13.3 MHz
≈ 1.95 s ≈ 117 fields at 60.05 Hz.

**If you change an `ITERATIONS` value, re-measure and update both the `.s`
comment and the tool's budget block.**

Measured for `benchgpu_arith.j64` (2,000,000 iterations). **Correction
(fix round 1, 2026-08-24): the `gpu_pipeline_timing=enabled` row below was
previously published as `86` (flat, no response). That was never re-measured
after being copied from an earlier draft of this table -- a direct run shows
it moves to `172`, a ~100% jump, the largest relative move of any
configuration tested against this ROM:**

| configuration | done_frame |
|---|---|
| harness defaults | 86 |
| `gpu_pipeline_timing=enabled` | 172 |
| `dram_timing=enabled` | 86 |
| PAL | 86 |
| both + `VJ_DRAM_SCALE=8` | 172 |
| both + `VJ_DRAM_SCALE=12` | 172 |
| both + `VJ_DRAM_SCALE=16` | 172 |

`dram_timing` alone doesn't move this ROM: the 2M-iteration hot loop runs
entirely out of GPU local RAM and touches external memory only for its two
sentinel STOREs, and `VJ_DRAM_SCALE` only scales external-memory latency.
`gpu_pipeline_timing` is the mover, and it roughly *doubles* the completion
frame -- see the explanation after the branch-ROM table below, which covers
both ROMs together. The tools use the same **600** budget as `bench68k.j64`
for consistency across the five ROMs; it clears 172 with ~3.5x margin.

Measured for `benchgpu_branch.j64` (2,000,000 iterations, 21-instruction
body -- see the `.s` for the exact composition: 1 CMPQ + 8 not-taken
JR/NOP pairs + ADDQ + SUBQ + 1 JUMP/NOP loop closer, ~90% branch dispatch):

| configuration | done_frame |
|---|---|
| harness defaults | 91 |
| `dram_timing=enabled` | 91 |
| PAL | 91 |
| `gpu_pipeline_timing=enabled` | 113 |
| both + `VJ_DRAM_SCALE=8` | 113 |
| both + `VJ_DRAM_SCALE=12` | 113 |
| both + `VJ_DRAM_SCALE=16` | 113 |

### `gpu_pipeline_timing` response: arith moves MORE than branch, not less

An earlier draft of this document (and `task-3-report.md`) claimed
`benchgpu_arith.j64` stayed flat under every configuration including
`gpu_pipeline_timing`, and that only `benchgpu_branch.j64` responded to it.
**That was false and came from an unverified copy of the arith row above.**
Directly measured, both ROMs respond, and arith responds *more*, both in
absolute frames (+86 vs. +22) and relative terms (+100% vs. +24%):

| ROM | defaults | `gpu_pipeline_timing=enabled` | delta |
|---|---|---|---|
| `benchgpu_arith.j64` | 86 | 172 | +86 frames (+100%) |
| `benchgpu_branch.j64` | 91 | 113 | +22 frames (+24%) |

`dram_timing` alone still doesn't move either ROM (both loops run entirely
out of GPU local RAM), and `VJ_DRAM_SCALE` has no further effect once
`gpu_pipeline_timing` is already enabled for either ROM -- confirming the
extra cost in both cases comes from the pipeline model's internal-RISC-time
accounting, not from DRAM access latency.

The mechanism (read from `src/tom/gpu.c` `GPUPipeCheckUse()`, not guessed):
`gpu_pipeline_timing` implements a **write-back port conflict** rule off the
JTRM's "Register Write-Back" section -- the GPU register bank is dual-port,
so back-to-back register-writing instructions can only avoid a 1-cycle stall
when the second instruction's *sole* read target is the first instruction's
write-back register. Any instruction that reads **two** registers, neither of
which is the previous instruction's destination, pays the stall
unconditionally (`gpu.c` around the "Write-back port conflict" comment).

- **`benchgpu_arith.j64`'s** loop body is 16 back-to-back ALU ops
  (`add`/`sub`/`and`/`or`), each of which both reads two registers and writes
  a third (JTRM ISA classification 7 = reads-both-writes-dest in
  `gpu_pipe_flags[]`). Consecutive pairs almost never share the prior
  instruction's destination as an operand, so nearly every one of the 16 ALU
  ops eats the 1-cycle write-back stall -- roughly +16 cycles on a ~20-cycle
  nominal body, which is why the completion frame very nearly doubles (86 ->
  172, and 172/86 = 2.00).
- **`benchgpu_branch.j64`'s** loop body is dominated by JR and its
  mandatory delay-slot NOP (16 of 21 instructions across the 8 not-taken
  pairs). Both JR and NOP are classified as reading zero registers
  (`gpu_pipe_flags[]` index 0 for both), so they structurally cannot trigger
  the two-register-read write-back conflict -- only the loop's CMPQ, ADDQ,
  and SUBQ (each of which reads at most one register per the same table) are
  even eligible, and none of those pairings triggers the rule either. What
  branch-heavy code does pay for instead is the JTRM's documented taken-JUMP
  refill cost and the flags interlock between CMPQ and a conditional
  JR/JUMP -- smaller, less frequent charges, which is why the ROM moves by
  22 frames instead of 86.

In short: `gpu_pipeline_timing`'s dominant cost in this model is a per-ALU-op
register-bank contention charge, not a branch-misprediction charge -- so a
tightly-chained ALU body (arith) is the *worse* case for it, and a
control-flow-heavy body built from register-free JR/NOP pairs (branch) is
comparatively cheap. This is the opposite of the "branch-heavy code is what
pipeline timing punishes" intuition #536 set out to test, and is worth
knowing before using either ROM as a stand-in for real branch-misprediction
cost. The tools use the same **600** budget as the other ROMs for both
ROMs; it clears the highest measured frame (172) with ~3.5x margin.

Measured for `benchdsp.j64` (2,000,000 iterations, identical 20-instruction
body to `benchgpu_arith.j64` for a direct GPU-vs-DSP interpreter comparison
at the same clock):

| configuration | done_frame |
|---|---|
| harness defaults | 86 |
| `gpu_pipeline_timing=enabled` | 86 |
| `dram_timing=enabled` | 86 |
| both | 86 |

Same default completion frame as `benchgpu_arith.j64` (both 86, both engines
run this identical body at the same ~26.6 MHz full-system clock), **but the
DSP does not respond to `gpu_pipeline_timing` the way the GPU does** -- all
four configurations above were re-run directly against this ROM (not
inferred from the GPU table). This isn't a coincidence of this workload:
`src/jerry/dsp.c`'s `DSPExec()` comment block states outright that "the DSP
has no pipeline-timing mode of its own (`DSPExecP`/`DSPExecP2` are declared
in `dsp.h` but never called)" -- `gpu_pipeline_timing` only gates the DSP's
*idle-skip fast-forward* path (irrelevant here, since this loop never idles)
and adds no per-instruction cost model for the DSP the way `GPUPipeCheckUse()`
does for the GPU. `dram_timing` doesn't move it either, for the same reason
as the GPU ROM: the hot loop runs entirely out of DSP local RAM and touches
external memory only for its two sentinel STOREs. The tool uses the same
**600** budget as the other four ROMs; it clears 86 with ~7x margin.

Measured for `benchblit.j64` (150,000 launches of a 32x32 8bpp PATDSEL solid
fill; see "The blitter ROM is 68K-only" below for why this one has no
companion GPU/DSP source and no per-loop table like the previous three).
Unlike the other four ROMs, its fill destination (`$020000`, the benchmark
work buffer) is **main RAM**, so — checked directly, not assumed by analogy
with the GPU/DSP ROMs whose hot loops never leave local RAM — it responds to
`dram_timing` on its own, `virtualjaguar_blitter_timing` dominates, and the
combination can exceed the other ROMs' 600-frame budget:

| configuration | done_frame |
|---|---|
| harness defaults | 87 |
| `gpu_pipeline_timing=enabled` | 87 |
| PAL | 87 |
| `dram_timing=enabled` | 101 |
| `blitter_timing=enabled` | 434 |
| `dram_timing` + `blitter_timing` + `gpu_pipeline_timing` | 458 |
| all three + `VJ_DRAM_SCALE=8` | 564 |
| all three + `VJ_DRAM_SCALE=12` | 607 |
| all three + `VJ_DRAM_SCALE=16` (worst measured) | 631 |

`virtualjaguar_blitter_timing` (default disabled, off in `make microbench`'s
run) is the mover by a wide margin — it is the option that charges the
blit's own bus-occupancy cost (`BlitDurationSysclks()` in
`src/tom/blitter_mmio.c`; the "Fixed setup" comment in `benchblit.s` covers
where that cost lands). `gpu_pipeline_timing` never moves this ROM at all
(it gates GPU/DSP RISC-instruction pipeline accounting; nothing here runs on
either processor). Because 631 exceeds the other ROMs' 600-frame budget,
`test_microbench_blit`'s `MB_FRAME_BUDGET` is **1200**, not 600 — a
deliberate, documented difference, not an oversight (see the tool's own
comment).

### The blitter ROM is 68K-only

`benchblit.s` has no `benchblit_gpu.s`/`benchblit_dsp.s` companion and no
lyxass pass. The blitter is a DMA engine addressed entirely through 68K
writes to its MMIO register file (`$F02200-$F0229F`); there is no separate
program to load onto another processor, so this ROM reuses Task 1's
cart-boot shape directly (`bench68k.s`'s `.include "cartboot.inc"` followed
by 68K code) rather than Tasks 2-4's 68K-bootstrap-plus-RISC-payload split.

### IDLE poll is a no-op in this emulator — verified, not inferred

`benchblit.s` polls `B_CMD`'s bit 0 between blit launches, matching the
hardware-programming idiom in `docs/jtrm-blitter.md`'s "B_CMD Status"
section. In **this core specifically**, that poll never actually waits:
`src/tom/blitter_mmio.c`'s `BlitterReadByte` hard-codes the status byte at
`COMMAND+3` to `$05` ("always idle/never stopped"), and `BlitterWriteWord`
dispatches the fill synchronously (calls straight into `blitter_blit()` /
`BlitterMidsummer2()`) before the write instruction that triggered it even
retires — there is no modeled "blitter running in the background" state for
a poll to observe. The poll is included because it is what real Jaguar code
does and because the ROM should look like a plausible blit-launch loop, not
because it changes this ROM's timing on this core.

---

## Assembling: what actually worked

```bash
rmac -fb -o bench68k.o bench68k.s
rln  -n -a 800000 x x -o bench68k.j64 bench68k.o
```

Gotchas, all learned from the tools' own errors:

- **No `.org` in a 68K section.** rmac: *"Error: .org permitted only in
  GPU/DSP/OP, 56001, 6502 and 68k (with -fr switch) sections"*. The load address
  goes on `rln -a` instead, which is how a real cart is built anyway — hence
  the header being laid out with `dcb.b` fill from offset 0 and linked at
  `$800000`.
- **`-fb`** selects BSD object format, which rmac's own usage text labels *"use
  this for Jaguar"* and is what rln consumes.
- **`rln -n`** suppresses rln's file header, so the output is the raw
  big-endian cart image. rln pads the tail out to an 8-byte phrase, which is
  why `bench68k.j64` is 8240 bytes rather than the 8238 the source emits.
- **`rln` needs the two positional `x x` placeholders** (data/bss segment
  addresses) after `-a <text>`.
- `.include` and `.incbin` both work in rmac (verified). `build.sh` `cd`s into
  `test/microbench/` so `.include "cartboot.inc"` resolves without an `-i` flag.
- **Output is byte-reproducible.** Two consecutive builds of an unchanged
  source `cmp` identical, so a regenerated `.j64` diffs only when the source
  really changed.

### For tasks 2–5: embedding a lyxass GPU/DSP blob

`lyxass` output is **not** raw code. It is a **12-byte BS94 header** followed by
the code (see `tools/jaguar-toolchain/smoke/hellogpu.s`, which documents the
exact layout):

```
4253 3934    "BS94" magic
00F0 3000    run address
0000 0004    code length
D7E0 E400    <-- the actual code starts here, at offset 12
```

A naive `.incbin` of a lyxass `.o` therefore embeds the header, and the RISC
will happily execute `BS94` + address + length as instructions. **Skip the
first 12 bytes** — either strip them when generating the blob, or have the 68K
loader copy from `blob+12`.

The other lyxass trap from that same file: it emits **nothing** for code placed
before its `.run` directive, while still exiting 0 and still writing a
12-byte-header file. Put `.run` after `.org` and before the first instruction.

---

## Honesty notes

- The 68K loop **executes from cart ROM at `$802000`**, so the measurement
  includes 68K instruction-fetch cost through the cart window — it is not pure
  register-op throughput. Relevant when comparing numbers across the five ROMs,
  since the GPU/DSP benches will run from local RISC RAM instead.
- Interrupts are masked (`move.w #$2700,sr`) before the loop. The HLE BIOS
  leaves video/timer IRQs live with RTE stubs installed; letting them fire
  would put BIOS-path 68K cycles inside the measured window and make the loop's
  cost depend on video timing.
- TOM still renders a (blank) display throughout. Video cost is not isolated
  away and cannot be, short of blanking the screen.
