# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Virtual Jaguar libretro core — an Atari Jaguar emulator ported to the libretro API. Written in C, licensed under GPLv3. Upstream: `http://shamusworld.gotdns.org/git/virtualjaguar`.

## Build Commands

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # Build (auto-detects platform)
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # Debug build (-O0 -g)
make clean                                    # Clean build artifacts
make platform=ios-arm64                       # Cross-compile for specific platform
```

Output binary name varies by platform:
- macOS: `virtualjaguar_libretro.dylib`
- Linux: `virtualjaguar_libretro.so`
- Windows: `virtualjaguar_libretro.dll`

CI runs `make -j4` on Ubuntu (GCC) and macOS (Clang), plus screenshot regression tests via `test/regression_test.sh`. See `docs/test-infrastructure.md` for the full test harness inventory.

## Architecture

### C Language Standard — C89/GNU89

This codebase **must** compile as C89 (GNU89 dialect). The libretro buildbot uses MSVC on Windows, which enforces C89 strictly. CI includes a `c89-lint` job that catches violations.

**Rules:**
- **No mid-block variable declarations.** All variables must be declared at the top of their enclosing block (function or `{}`), before any statements. This is the most common violation.
- `//` comments are allowed (GNU89 extension), but `/* */` is preferred for new code.
- No C99 features: no `for (int i = ...)`, no compound literals, no designated initializers, no VLAs.
- SIMD files (`src/blitter_simd_sse2.c`, `src/blitter_simd_neon.c`) are exempt from the lint check since they require platform-specific headers.
- Machine-generated files (`src/m68000/*`) are also exempt.

**Local check before pushing:**
```bash
gcc -fsyntax-only -std=gnu89 -Werror=declaration-after-statement \
    -I. -Isrc -Isrc/m68000 -Ilibretro-common/include \
    -D__LIBRETRO__ -DINLINE="inline" src/YOURFILE.c
```

### Atari Jaguar Hardware Emulation

The Jaguar has four processors sharing a unified memory-mapped address space:

- **Motorola 68000** (13.3 MHz) — main CPU for game logic. Emulated via UAE-derived core in `src/m68000/`. The `cpuemu.c` file is machine-generated and very large (~1.8 MB).
- **GPU** (26.6 MHz RISC) — graphics coprocessor in `src/gpu.c`
- **DSP** (26.6 MHz RISC) — audio coprocessor in `src/dsp.c`, same instruction set as GPU
- **Object Processor** — sprite/bitmap rendering in `src/op.c`

Two custom chips contain these processors:
- **TOM** (`src/tom.c`) — video output, GPU, Object Processor, Blitter (`src/blitter.c`)
- **JERRY** (`src/jerry.c`) — audio DAC (`src/dac.c`), DSP, timers, EEPROM (`src/eeprom.c`)

### Execution Model

Frame execution is event-driven, not cycle-accurate. `JaguarExecuteNew()` in `src/jaguar.c` runs the main loop: the 68K executes until the next timed event, then GPU runs for the same timeslice, then event callbacks fire (half-line rendering, timer interrupts, etc.).

### Memory

Memory map defined in `src/vjag_memory.h`. The Jaguar is big-endian; `GET16/GET32/SET16/SET32` macros handle byte-swapping on little-endian hosts. Main RAM is 2 MB at 0x000000, cart ROM at 0x800000, TOM registers at 0xF00000, JERRY registers at 0xF10000.

### Libretro Integration

`libretro.c` (top-level) implements the libretro API — initialization, per-frame execution, input polling, video/audio output. Video is XRGB8888 at dynamic resolution (typically 320x240 NTSC / 320x256 PAL). Audio is 48 kHz 16-bit stereo.

Core options defined in `libretro_core_options.h` control blitter mode, BIOS usage, NTSC/PAL, DSP execution, and input mapping.

### Key Directories

- `src/` — emulator core (hardware chips, CPU, I/O, BIOS ROMs as C arrays)
- `src/m68000/` — UAE-derived 68K CPU emulation
- `libretro-common/` — shared libretro utility library (string, file, VFS)
- `docs/` — documentation: changelog, known issues, BUTCH register map, CD data flow, test infrastructure
- `test/tools` — test scripts and headless front-ends
- `test/roms` — test ROMs; `private/` subdirectory has commercial ROMs and BIOSes

### Build System

`Makefile` handles 30+ platform targets with auto-detection. `Makefile.common` lists all source files. Platform is selected via `platform=` variable or auto-detected from `uname`. Key flags: `-D__LIBRETRO__`, `-DMSB_FIRST` for big-endian platforms.

### Jaguar CD Emulation

CD support is implemented across `src/cdrom.c` (BUTCH chip / FIFO / DSA commands), `src/cdintf.c` (disc image loading: CUE/BIN, CHD, CDI), and hooks in `src/jaguar.c` (BIOS auth bypass, boot stub injection).

Key docs:
- `docs/butch-registers.md` — full BUTCH register map ($DFFF00-$DFFF2F) with bit definitions
- `docs/cd-data-flow.md` — how CD data moves from disc to RAM (I2S -> FIFO -> GPU ISR -> RAM), BIOS code map, boot stub layout

### Testing

RetroAchievements-related — **no RetroAchievements account, API, or gameplay server**; local validation only. The E2E harness still **fetches the pinned rcheevos source tarball from GitHub** when `build/rcheevos-static` is missing (CI may cache that directory); that is unrelated to contacting RetroAchievements services.

- `test/tools/test_memory_map.c` — asserts `SET_MEMORY_MAPS`, `SET_SUPPORT_ACHIEVEMENTS` with **`true`**, and descriptor layout vs `retro_get_memory_data(SYSTEM_RAM)`.
- `test/tools/test_rcheevos_e2e.sh` — downloads pinned **rcheevos** (`RCHEEVOS_REF`) when needed, builds `librcheevos.a`, then runs `test_rcheevos_e2e` to verify **rc_libretro** memory resolution (`RC_CONSOLE_ATARI_JAGUAR`) matches host RAM — the same mapping stack RetroArch uses before any RA cloud call.

See `docs/test-infrastructure.md` for all test harnesses:
- `test/test_dsp_mac40.c` — Jaguar DSP **40-bit MAC** accumulator semantics (`dsp_acc40.h`), run in CI with SIMD tests; relevant for long IIR chains (e.g. pink-noise generators on DSP).
- `test/headless.py` — Python headless runner via libretro.py (screenshots, frame control)
- `test/regression_test.sh` — screenshot regression suite with baseline comparison
- `test/test_cd_boot.c` — low-level C harness with dlsym access to 68K registers and RAM
- `test/sram_test.sh` — SRAM interface round-trip testing

#### Headless framebuffer / 240p suite — how to report issues

Profiling symbols like **`__muldi3`** (64-bit multiply helpers on some 32-bit ABIs) are a **compiler/performance** concern, **not** evidence that the Jaguar 240p test ROM’s DSP pink-noise path or NTSC timing is wrong. Do **not** frame “240p fails” primarily as a **`__muldi3`** bug unless you are optimizing a 32-bit build for speed.

The **useful** regression story for automated screenshot / libretro.py / SessionBuilder runs is:

- **Symptom:** On some cores or builds, a **non-RetroArch headless session** does not expose the **same composited framebuffer** via the libretro API (`video_screenshot`, etc.) as **RetroArch with the same core binary** — e.g. main menu of **jag_240p_test_suite v1.0.0** shows only a **thin band** (~order of **1k** non-black RGB pixels) vs **tens of thousands** on a known-good path.
- **Interpretation:** Suspect **presentation / pixel source** — Object Processor and blitter output vs **what headless clients actually read** — until disproven with hardware or reference captures. This is **not** “prove 240p timing is wrong first.”
- **Checks:** Use in-repo gates (e.g. **screenshots-preflight** / main-menu sanity, non-black pixel floor on the **~2000+** scale). Passing preflight ⇒ the **headless read-path** issue is resolved for that artifact; failing preflight ⇒ file a **framebuffer/compositing** bug for **headless libretro** consumption (logs, **two artifacts**: broken vs good), not a long **`__muldi3`** narrative.

### Known Limitations

- Blitter not fully cycle-accurate (some games need fast blitter mode)
- Bus contention between processors not emulated
- Vertical count (VC) register behavior not fully accurate
