# Purpose-built microbenchmark ROMs (#536)

Small, single-engine Jaguar cartridges with a **fixed iteration count** and a
**harness-assertable completion marker**. They fill the gap #533 identified
after auditing the public ROM corpus (jagniccc/yarc): real titles never give
single-engine isolation or an exact, deterministic termination.

| ROM | engine | DONE magic | status |
|---|---|---|---|
| `bench68k.j64` | 68000 only | `$C0DE0068` | shipped (task 1) |
| `benchgpu_arith.j64` | GPU, arithmetic-heavy | `$C0DE0A01` | task 2 |
| `benchgpu_branch.j64` | GPU, branch-heavy | `$C0DE0B02` | task 3 |
| `benchdsp.j64` | DSP | `$C0DE0D53` | task 4 |
| `benchblit.j64` | blitter | `$C0DE0B17` | task 5 |

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
