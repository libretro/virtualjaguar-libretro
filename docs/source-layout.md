# Source Layout

The core source tree is organized by Jaguar hardware ownership. Keep mechanical
file moves separate from behavior changes so emulator fixes stay easy to review
and bisect.

## Top-Level Files

- `libretro.c` owns the libretro API entry points, core options, input polling,
  serialization glue, cheats, and per-frame execution handoff.
- `Makefile.common` is the source list used by the main Makefile and Android
  NDK build. Update it whenever a C file is added, moved, or removed.

## Subsystems

- `src/core/` contains system orchestration, the memory map, event scheduling,
  settings, file helpers, cheats, memory tracking, MMU helpers, and save-state
  support shared by multiple chips.
- `src/tom/` contains TOM-side hardware: video timing, Object Processor, GPU,
  Blitter, and Blitter SIMD implementations.
- `src/jerry/` contains JERRY-side hardware: DSP, DAC/audio pipeline, EEPROM,
  input/joystick handling, and wavetable data.
- `src/cd/` contains Jaguar CD/BUTCH register emulation and the disc-interface
  abstraction.
- `src/bios/` contains embedded BIOS and boot stub arrays. These are data-heavy
  generated/static inputs, not regular refactor targets.
- `src/m68000/` contains the UAE-derived 68K CPU core. Most files here are
  generated or upstream-derived and should remain isolated.

## Refactor Guidelines

- Preserve C89/GNU89 compatibility: declarations stay at the top of each block.
- Keep public interfaces in subsystem headers and private cross-file state in
  small `*_internal.h` headers scoped to one subsystem.
- Prefer one concern per commit: source moves, build updates, and logic changes
  should not be mixed unless the build cannot remain green otherwise.
- When adding a new C file, update `Makefile.common` and the manual MSVC source
  list in `.github/workflows/c-cpp.yml`.
- After source layout changes, run `make lint`, a clean native build, and the
  focused unit tests for the touched subsystem.

## Blitter Files

- `src/tom/blitter.c` contains the active Blitter implementation and
  Oberon-derived hardware logic.
- `src/tom/blitter_mmio.c` contains Blitter lifecycle, register reads/writes,
  and command dispatch.
- `src/tom/blitter_compare.c` contains comparison-mode diagnostics used when
  validating fast Blitter behavior against the accurate implementation.
- `src/tom/blitter_simd_*.c` contains SIMD acceleration for selected data-path
  operations.
