# Acid-test ROM toolkit

Synthetic Jaguar ROMs that exercise specific hardware corners --
blitter modes, GPU/DSP cross-talk, beam chasing, OP scenarios -- and
report pass/fail to the host via a fixed RAM signature.

The motivation is two-fold:

1. **Reproducible perf benchmarks** that don't depend on commercial ROMs
   (which we can't ship). Each acid test is small (typically <8 KB),
   open-source, and exercises a single feature so we can attribute
   regressions cleanly.
2. **Bug-finding under stress.** Commercial games hit wide combinations
   of features, but only the combinations *they happen to use*. Acid
   tests exhaustively walk a feature axis (every pixsize, every
   phrase/non-phrase, every Z-mode) and catch divergence between fast
   and accurate blitters, between our implementation and the hardware
   reference, and between successive emulator versions.

Status: **early scaffolding.** Runner + build infrastructure landed,
first source-form test landed, vasm dependency documented but optional
(CI builds skip the assemble step when vasm is absent).

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
      timing/           -- VC/VP, halfline, beam chasing
```

## How a test reports its result

Tests write a four-word "acid signature" block at fixed RAM offset
`0x100` (low main-RAM, well below the cart base and any normal use).

```
0x100: ACID_RESULT     -- 0x12345678 PASS, 0xDEADBEEF FAIL,
                          0x00000000 NOT-RUN-YET
0x104: ACID_DETAIL     -- test-specific error / sub-test code
0x108: ACID_OBSERVED   -- value the test actually got (on FAIL)
0x10C: ACID_EXPECTED   -- value the test was looking for
```

The runner reads main-RAM via `retro_get_memory_data(SYSTEM_RAM)` after
running N frames and prints PASS / FAIL with diagnostics.

## Building

The toolchain is **vasm** (motorola syntax + Jaguar GPU/DSP backends),
with **vlink** for linking. Both are open source from
http://sun.hasenbraten.de/vasm/.

```bash
# macOS (build from source -- not in Homebrew):
git clone http://sun.hasenbraten.de/vasm/release/vasm.tar.gz   # or: curl -O
cd vasm && make CPU=m68k SYNTAX=mot
sudo install vasmm68k_mot /usr/local/bin/

git clone http://sun.hasenbraten.de/vlink/release/vlink.tar.gz
cd vlink && make
sudo install vlink /usr/local/bin/
```

Linux: same source build, no package manager wrapper.

Then:

```bash
cd test/acid && make           # assembles all tests/*.s into *.jag
make acid                       # from repo root: build core + tests + run
```

If `vasmm68k_mot` is not on `$PATH`, the Makefile prints a one-line
warning and skips the assemble step. Pre-built `.jag` ROMs are checked
into `tests/<category>/prebuilt/` for the cases where we want CI to
test against a known-good binary without depending on the assembler.

## Writing a new test

1. Pick a category (`blitter/`, `gpu/`, etc.) or add a new one.
2. Drop a `<name>.s` file. Start from
   `tests/blitter/copy_simple.s` as a template.
3. Include the acid header + the signature macros:
   ```
   include "jaguar_header.s"
   include "acid_test.s"
   ```
4. Write your test. End with `ACID_PASS` or `ACID_FAIL detail,
   observed, expected`.
5. Run `make` in `test/acid/`; the new test's `.jag` appears alongside.
6. Run `./run <core> <name>.jag` to verify.

## Running

```bash
# From repo root:
make acid                                          # build + run all tests
test/acid/run ./virtualjaguar_libretro.dylib \
              test/acid/tests/blitter/copy_simple.jag    # one test
```

The runner exits 0 if all PASS, non-zero if any FAIL or NOT-RUN-YET.

## Future categories (not yet shipped)

- **Blitter mode matrix** -- every (pixsize, phrase_mode, gourd, gourz,
  bcompen, dcompen) combination, fast vs accurate divergence checks.
- **GPU<->Blitter sync** -- GPU programs that issue a blit, poll BUSY,
  and verify dest data.
- **DSP<->68K I2S** -- DSP fills SOR/SOL, 68K observes IRQ timing,
  measure jitter.
- **OP edge cases** -- scaled bitmaps with ZP, branch objects, GPU-int
  objects, OP-list cycles.
- **Beam chasing** -- VC/VP register reads at known scanline offsets,
  programmatic palette swaps mid-frame.
- **Cycle stress** -- fixed-iteration GPU/DSP loops with predictable
  cycle counts, used to characterise our event-scheduler timing
  accuracy.

Each will land as its own focused test or test family.
