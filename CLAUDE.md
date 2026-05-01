# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project

Virtual Jaguar libretro core — Atari Jaguar emulator on the libretro API. C, GPLv3. Upstream: `http://shamusworld.gotdns.org/git/virtualjaguar`.

## Build

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # Build (auto-detects platform)
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # Debug (-O0 -g)
make clean
make platform=ios-arm64                       # Cross-compile target
```

Output: `virtualjaguar_libretro.{dylib,so,dll}`. CI: `make -j4` on Ubuntu (GCC) and macOS (Clang) plus `test/regression_test.sh` screenshots.

## C89 / GNU89 — strict

The libretro buildbot uses MSVC on Windows. CI has a `c89-lint` job. Run `bash scripts/c89-lint.sh src/YOURFILE.c` before pushing.

- **No mid-block declarations.** All vars at top of block, before any statement. Most common violation.
- `//` comments allowed (GNU89), but prefer `/* */` for new code.
- No C99: no `for (int i…)`, no compound literals, no designated initializers, no VLAs.
- Exempt: `src/tom/blitter_simd_{sse2,neon}.c` (need platform headers), `src/m68000/*` (machine-generated).

## Hardware model

Four processors, unified memory map, big-endian. `GET16/GET32/SET16/SET32` macros byte-swap on LE hosts. Map in `src/core/vjag_memory.h`: RAM 0x000000 (2 MB), cart 0x800000, TOM regs 0xF00000, JERRY regs 0xF10000.

- **68000** (13.3 MHz, `src/m68000/`) — main CPU. UAE-derived. `cpuemu.c` is **machine-generated, ~1.8 MB** — never read whole; grep first, then `Read` with offset/limit only on matched ranges.
- **GPU** (26.6 MHz RISC, `src/tom/gpu.c`) — graphics coprocessor.
- **DSP** (`src/jerry/dsp.c`) — same ISA as GPU; audio.
- **Object Processor** (`src/tom/op.c`) — sprite/bitmap rendering.
- **TOM** (`src/tom/tom.c`) — video, GPU, OP, Blitter (`src/tom/blitter.c`).
- **JERRY** (`src/jerry/jerry.c`) — audio DAC, DSP, timers, EEPROM.

Frame loop is event-driven (not cycle-accurate): `JaguarExecuteNew()` in `src/core/jaguar.c` runs 68K to next event, then GPU, then fires callbacks (half-line render, timers).

## Libretro layer

`libretro.c` (top-level) implements the API. Video XRGB8888 dynamic res (320×240 NTSC / 320×256 PAL). Audio 48 kHz 16-bit stereo. Core options in `libretro_core_options.h` (blitter mode, BIOS, NTSC/PAL, DSP, input).

## Layout

- `src/core/` — orchestration, memory map, events, settings, files, cheats
- `src/tom/` — video, GPU, OP, blitter (+ SIMD)
- `src/jerry/` — audio, DSP, DAC, EEPROM, input, wavetable
- `src/cd/` — Jaguar CD: BUTCH/FIFO/DSA in `cdrom.c`, image loading (CUE/BIN, CHD, CDI) in `cdintf.c`; BIOS auth bypass + boot stub in `src/core/jaguar.c`
- `src/bios/` — embedded BIOS / boot stubs
- `src/m68000/` — UAE 68K (machine-generated; treat as opaque)
- `libretro-common/` — shared utility lib
- `test/tools/` — test harnesses; `test/roms/private/` — commercial ROMs/BIOSes (gitignored)

## Build system

`Makefile` covers 30+ targets, auto-detected via `uname` or `platform=`. `Makefile.common` lists sources. Flags: `-D__LIBRETRO__`, `-DMSB_FIRST` for big-endian.

## Testing

Local-only RetroAchievements validation — no RA account/API/server. `test/tools/test_rcheevos_e2e.sh` downloads pinned `RCHEEVOS_REF` and verifies `rc_libretro` mapping (`RC_CONSOLE_ATARI_JAGUAR`) matches host RAM.

Key harnesses:
- `test/regression_test.sh` — screenshot regression vs `test/baselines/`
- `test/headless.py` — libretro.py runner (frames, screenshots)
- `test/tools/test_memory_map.c` — asserts `SET_MEMORY_MAPS`, `SET_SUPPORT_ACHIEVEMENTS=true`, descriptor layout
- `test/tools/test_blitter_compare` — fast vs accurate blitter diff
- `test/test_dsp_mac40.c` — DSP 40-bit MAC accumulator (`dsp_acc40.h`)
- `test/test_cd_boot.c` — dlsym harness for 68K regs/RAM
- `test/sram_test.sh` — SRAM round-trip

### Headless framebuffer caveat

Some non-RetroArch headless harnesses (libretro.py, miniretro) don't expose the same composited framebuffer that RetroArch reads. Symptom: `jag_240p_test_suite` main menu shows ~1k non-black pixels via headless vs tens of thousands via RetroArch. Treat that as a **headless read-path / presentation bug** (OP+blitter output vs what the host reads), not a 240p timing or `__muldi3` performance bug. Gate via `make screenshots-preflight`.

## Known limitations

- Blitter not fully cycle-accurate (some games need fast mode).
- No bus contention modeling.
- VC register behavior not fully accurate.
