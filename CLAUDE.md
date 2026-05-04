# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project

Virtual Jaguar libretro core — Atari Jaguar emulator on the libretro API. C, GPLv3. Upstream: `http://shamusworld.gotdns.org/git/virtualjaguar`.

## Branching

GitFlow: branch new work off **`develop`** (the integration branch); `master` is release-only (tagged commits, hotfix merges, release-branch merges). PRs targeting `master` get auto-warned by `.github/workflows/warn-pr-base.yml` — retarget to `develop` unless the source branch is `hotfix/*` or `release/*`. Full flow in [`docs/release-process.md`](docs/release-process.md).

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
- Exempt (see `scripts/c89-lint.sh::skip_file`): `src/m68000/cpu*.c` and `src/m68000/read*.c` (UAE 68K), `src/bios/jag*bios*.c` and `src/bios/jagstub*bios.c` (bin2c hex tables), `src/tom/blitter_simd_{sse2,neon}.c` (platform intrinsics), `test/tools/test_rcheevos_e2e.c` (rcheevos-dependent), `test/tools/flicker_detect.c` (diagnostic).

## Hardware model

Four processors, unified memory map, big-endian. `GET16/GET32/SET16/SET32` macros byte-swap on LE hosts. Address-range map is documented in `src/core/vjag_memory.c` (header comment); the dispatch logic lives in `src/core/jaguar.c`. RAM 0x000000 (2 MB), cart 0x800000, TOM regs 0xF00000, JERRY regs 0xF10000.

**Authoritative hardware reference:** The Jaguar Technical Reference Manual (JTRM) is the ground-truth spec for all emulation decisions. If you have a local copy, place it in `docs/atari-jaguar-1999/` (gitignored — copyrighted, not distributed). Key sections: Software Reference (register maps, PIT formulas, ISA), Technical Reference (clock hierarchy, bus timing), Hardware Bugs & Warnings (silicon errata). **Always verify clock rates and register behavior against the JTRM** — do not trust comments in the source code, which have historically been wrong (e.g. PIT clock was incorrectly halved). System clock: 26.590906 MHz NTSC / 26.593900 MHz PAL. 68K runs at half (~13.3 MHz). GPU/DSP/PIT run at the full system clock rate.

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
- `test/regression_test.sh` — screenshot regression vs `test/baselines/` via miniretro (built from source on first run; `MINIRETRO_BIN` env to skip the build)
- `test/tools/test_memory_map.c` — asserts `SET_MEMORY_MAPS`, `SET_SUPPORT_ACHIEVEMENTS=true`, descriptor layout
- `test/tools/test_blitter_compare` — fast vs accurate blitter diff
- `test/test_dsp_mac40.c` — DSP 40-bit MAC accumulator (`dsp_acc40.h`)
- `test/test_cd_boot.c` — dlsym harness for 68K regs/RAM
- `test/sram_test.sh` — SRAM round-trip

### Performance / profiling

`make benchmark` runs `test/tools/test_benchmark` headlessly against a fixed ROM (default `test/roms/yarc.j64`, 600 frames) and prints FPS / ms-per-frame. Use as a same-host commit-to-commit delta — don't compare across machines. Full guide: [`docs/profiling.md`](docs/profiling.md) covers Instruments / `perf` / flame graphs and the SIMD A/B knob.

### Headless framebuffer caveat

The miniretro harness used by `test/regression_test.sh` doesn't expose the same composited framebuffer that RetroArch reads. Symptom: `jag_240p_test_suite` main menu shows ~1k non-black pixels via miniretro vs tens of thousands via RetroArch. Treat that as a **headless read-path / presentation bug** (OP+blitter output vs what the host reads), not a 240p timing or `__muldi3` performance bug. Verify against RetroArch before treating a regression as real.

## Distilled hardware reference

`docs/jtrm-*.md` — synthesized from the Jaguar Technical Reference Manual, optimized for LLM consumption:
- `jtrm-clocks-timing.md` — clock hierarchy, video timing, PIT formulas, memory map, bus priority
- `jtrm-register-map.md` — complete register addresses + bit fields (TOM, GPU, blitter, JERRY, DSP)
- `jtrm-gpu-dsp.md` — RISC ISA, pipeline, score-boarding, interrupts, MAC, wave table ROM
- `jtrm-blitter.md` — address generators, B_CMD, LFU truth table, modes of operation
- `jtrm-jerry.md` — PIT timers, JINTCTRL, I2S/DAC, UART, clock dividers, EEPROM
- `jtrm-object-processor.md` — object types, bit fields, display pipeline, colour space

Read these **before** making hardware-accuracy decisions. They supersede comments in source code.

## Sub-agent guidelines

When spawning agents for work in this repo, include these rules:

1. **C89 strict.** No mid-block declarations, no `for(int i…)`, no C99. All vars at top of block. Run `bash scripts/c89-lint.sh src/YOURFILE.c` before declaring done.
2. **Branch from develop.** Use `git worktree` or branch off develop. Never target main.
3. **Hardware reference.** For any emulation-accuracy work, read `docs/jtrm-*.md` first. Do NOT trust source-code comments for clock rates or register behavior.
4. **Test after changes.** Run `make -j$(getconf _NPROCESSORS_ONLN)` to verify build. Run `make test` for the full suite. For blitter changes, also run `test/tools/test_blitter_compare` if available.
5. **No unnecessary changes.** Don't refactor surrounding code, add abstractions, or clean up unrelated files. Surgical changes only.
6. **Commit message style.** Use conventional commits: `fix(component):`, `perf(component):`, `test(component):`, `docs:`.

## Known limitations

- Blitter not fully cycle-accurate (some games need fast mode).
- No bus contention modeling.
- VC register behavior not fully accurate.
