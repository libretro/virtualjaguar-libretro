# Test Infrastructure

## headless.py - Python Libretro Test Harness

Primary headless test script using [libretro.py](https://github.com/JesseTG/libretro.py).

### Setup
```bash
python3.12 -m venv .venv-libretropy
source .venv-libretropy/bin/activate
pip install 'libretro.py[cli]'
```

### Usage
```bash
python test/headless.py <content.cue|.j64> [--frames N] [--cd-bios retail|dev] [--screenshot output.ppm]
```

### Capabilities
- Runs core completely headless (no GUI)
- Configurable frame count (default 600)
- Screenshots as PPM files
- Platform auto-detection (darwin/linux/win32)
- Stderr/stdout capture for debug logging

## regression_test.sh - Screenshot Regression Testing

Uses [miniretro](https://github.com/davidgfnet/miniretro) for automated
screenshot comparison against baselines.

### Usage
```bash
./test/regression_test.sh ./virtualjaguar_libretro.dylib
```

### Features
- ImageMagick `compare` for pixel-diff measurement
- Baseline PNGs in `test/baselines/`
- Visual diff generation on failures
- Determinism verification (runs each ROM twice)
- Frameskip invariance testing
- Save state round-trip validation

## test_cd_boot.c - Low-Level C Harness

Direct libretro API testing with hardware-level diagnostics via dlsym access
to internal functions.

### Build & Run
```bash
cc -o test/test_cd_boot test/test_cd_boot.c -ldl
./test/test_cd_boot roms/private/game.cue 600
```

### Capabilities
- `m68k_get_reg()` -- read 68K registers (D0-D7, A0-A7, PC, SR, SP)
- `TOMReadWord()` / `JERRYReadWord()` / `CDROMReadWord()` -- hardware registers
- `GetRamPtr()` -- direct RAM access
- Frame hashing, PC sampling, vector inspection

## sram_test.sh - SRAM Interface Testing

Tests libretro SRAM interface for save game handling.

```bash
./test/sram_test.sh ./virtualjaguar_libretro.dylib
```

## Unit Test Suites (make test)

Six test suites run via `make test`, covering CPU emulation, interrupt handling,
HLE BIOS, and blitter SIMD correctness.

### test_gpu_instructions.c — GPU RISC ISA (51 tests)
Arithmetic, logic, shift/rotate, compare, move/MOVEI, STORE/LOAD, saturation,
register bank switching. Tests run GPU programs in GPU RAM via dlsym.

### test_dsp_instructions.c — DSP RISC ISA (28 tests)
Same ISA as GPU with DSP-specific differences: signed saturation (sat16s/sat32s
instead of SAT8/SAT16), 8KB RAM at $F1B000.

### test_m68k_instructions.c — Motorola 68000 (39 tests)
MOVEQ, ADD/SUB/NEG/CLR, MULU/MULS/DIVU, SWAP, EXT, AND/OR/EOR/NOT,
LSL/LSR/ASR/ROL/ROR, CMP/TST, memory addressing modes (direct, pre-dec,
post-inc), LEA, ADDA/SUBA, BTST/BSET/BCLR.

### test_irq.c — Interrupt Handling (18 tests)
TOM IRQ enable/disable/latch/pending, JERRY IRQ enable, GPU IRQ assert/clear/IMASK,
TOM video mode register, JERRY timer prescaler, BUTCH interrupt control.

### test_hle_bios.c — HLE CD BIOS (15 tests)
Jump table, CD_poll A1=0 / A0=end conventions, CD_wait_response, ISR setup
handlers, TOC format, no-op entry safety, GPU auth magic, RAM byte order.

### test_cd_hle_boot.c — HLE CD Boot Smoke (1 dynamic test)
Recursively scans `test/roms/private/` (or `VJ_TEST_CD_ROOT`) for
`*.cue` / `*.iso` / `*.cdi` images. For each disc, forks a child that:

1. Loads the core fresh, forces `cd_boot_mode=hle`, calls `retro_load_game`.
2. Runs N frames (default 300, override with `VJ_TEST_CD_FRAMES`).
3. Records 68K PC after every frame; computes per-disc metrics:
   - `pc_in_ram`         — PC stays in valid RAM/BIOS ranges
   - `not_self_looping`  — PC moved at least once in the last 64 frames
   - `not_thrashing`     — visited > 8 distinct PCs (catches CD_read retry loops)
   - `ram_has_payload`   — at least 1KB of non-zero data in main RAM

Per-disc execution is wrapped in `fork()` so a `SIGSEGV` in one disc cannot
take down the suite. Crashes are reported as `[CRASH]` with the signal name.

Filters:
- `VJ_TEST_CD_FOCUS=substring`  run only matching discs
- `VJ_TEST_CD_FRAMES=N`         per-disc frame count

Current baseline (in `test/cd_hle_boot_baseline.log`, gitignored) is 0/14
discs PASS. The three failure modes the harness distinguishes:

| Mode      | Trigger                                            | Example discs |
|-----------|----------------------------------------------------|---------------|
| `[FAIL]`  | PC OOB, tight self-loop, or thrashing on <8 PCs    | All 14 discs |
| `[CRASH]` | Child died with SIGSEGV during `retro_load_game`   | `baldies.cdi` |
| `[SKIP]`  | No disc images discovered under the configured root | (empty corpus) |

### test_blitter_simd.c — Blitter SIMD (40,067 tests)
Exhaustive bit-exact comparison of LFU, DCOMP, ZCOMP, byte_merge against
reference implementations.

### Build & Run
```bash
make -j4 DEBUG=1    # Build core
make test           # Build & run all test suites
make test-build     # Build tests only
make clean-test     # Remove test binaries
```

### Framework (test_framework.h)
Minimal single-header test framework with dlsym-based core loading.
Provides GPU/DSP instruction encoding helpers, assert macros, and
function pointers to all hardware subsystem functions.

## CI Integration

GitHub Actions workflow (`.github/workflows/regression-test.yml`) runs
`regression_test.sh` and `sram_test.sh` on Linux x64, Linux ARM64, macOS ARM64.
Uploads diff artifacts on failure and comments on PRs.

## Directory Layout

```
test/
  headless.py              # Python libretro.py harness
  regression_test.sh       # Screenshot regression suite
  sram_test.sh             # SRAM interface test
  test_cd_boot.c           # CD boot diagnostics (C)
  test_framework.h         # Unit test framework header
  test_gpu_instructions.c  # GPU RISC ISA tests (51)
  test_dsp_instructions.c  # DSP RISC ISA tests (28)
  test_m68k_instructions.c # 68K CPU tests (39)
  test_irq.c               # Interrupt handling tests (18)
  test_hle_bios.c          # HLE CD BIOS tests (15)
  test_cd_hle_boot.c       # HLE CD boot smoke tests (dynamic discovery)
  cd_assertions.h          # Shared discovery + assertion helpers
  cd_hle_boot_baseline.log # Last captured per-disc baseline (gitignored)
  test_blitter_simd.c      # SIMD blitter tests (40067)
  baselines/               # Reference PNG screenshots
  roms/                    # Test ROMs (private/ is git-ignored)
  tools/                   # Test ROM generators, SRAM test harness
```
