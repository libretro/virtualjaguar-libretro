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
  test_blitter_simd.c      # SIMD blitter test (C)
  baselines/               # Reference PNG screenshots
  roms/                    # Test ROMs (private/ is git-ignored)
  tools/                   # Test ROM generators, SRAM test harness
  cd_trace_*.log           # Debug logs from CD boot tests
```
