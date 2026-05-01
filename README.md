# Virtual Jaguar libretro

Port of the [Virtual Jaguar](http://shamusworld.gotdns.org/git/virtualjaguar) Atari Jaguar emulator to the [libretro](https://www.libretro.com/) API.

[![C/C++ CI](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml)

## Features

- Emulates the Atari Jaguar's four processors: Motorola 68000, GPU, DSP, and Object Processor
- Supports NTSC and PAL video modes
- 2-player input with configurable numpad mapping
- Fast and legacy blitter modes (the legacy/accurate path is SIMD-accelerated on SSE2 and NEON)
- Optional BIOS boot sequence, plus an HLE BIOS so games can boot without a BIOS image
- Save state, run-ahead (deterministic serialization), SRAM/EEPROM via the libretro SRAM interface, cheat codes, and a memory map for RetroAchievements
- Supported ROM formats: `.j64`, `.abs`, `.jag`, `.rom` (including inside ZIP archives), plus conservative headerless raw homebrew loading

## Recent improvements (libretro fork)

This fork has diverged substantially from upstream Virtual Jaguar v2.1.0. See [docs/WHATSNEW](docs/WHATSNEW) for the full v2.2.0 changelog. Highlights:

- **HLE BIOS** now produces hardware-equivalent post-boot state — MEMCON1, clocks, GPU auth magic, OLP, exception vectors, TOM/JERRY timing — and the vast majority of commercial titles boot cleanly without any BIOS image. 200+ pin tests in `test_hle_bios` cover the contract.
- **Game-specific fixes**: Alien vs Predator red noise (M2 blitter `BKGWREN+BCOMPEN`), Doom resolution (proper `PWIDTH` pixel replication, replaces the legacy hack), and audio dropouts at frame edges across many titles (interleaved JERRY events). Jaguar CD support is in flight on a separate branch (PR forthcoming).
- **CPU accuracy**: DSP 40-bit MAC accumulator semantics, FLAGS-write dispatch, GPU/DSP IMASK preservation and ADDC carry overflow, DIVL exception PC.
- **Accurate-blitter** accuracy fixes (`daddmode` NAND tree, `daddbsel` bit 3, `ADDARRAY` cinsel carry, `SRCSHADE` color).
- **Object Processor**: scaled and fixed-bitmap `firstPix` handling, left/right/reflected edge clipping for scaled bitmaps, `firstPix` for 2/4/16/24 BPP fixed bitmaps.
- **TOM IRQs**: pending status now latches even when CPU enables are clear; `IPL2` reasserts on enable via the unified `TOMAssertEnabledIRQs` path. Selective clear works correctly when multiple sources are pending.
- **Headless test surface**: `make test` runs HLE-BIOS pin tests, event queue tests, blitter SIMD bit-exactness, DSP MAC40 semantics, save-state round-trip / rewind, cheat decoders, libretro memory-map / RetroAchievements wiring, plus a screenshot regression diff via `miniretro` on push.
- **Performance**: `~2x` speedup on DSP/GPU/memory hot paths, audio refactored to drop per-sample events.

## Building

```bash
make -j$(getconf _NPROCESSORS_ONLN)            # Auto-detects platform
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1    # Debug build
make platform=ios-arm64                         # Cross-compile (ios-arm64, osx, unix, win, android, switch, vita, etc.)
```

Output varies by platform: `.so` (Linux), `.dylib` (macOS), `.dll` (Windows).

## Documentation

- [File format details](docs/README)
- [Source layout](docs/source-layout.md)
- [Changelog](docs/WHATSNEW)
- [Known issues & TODO](docs/TODO)
- [libretro documentation](https://docs.libretro.com/)

## Links

- Upstream: `git clone http://shamusworld.gotdns.org/git/virtualjaguar`
- Unofficial GitHub mirror: https://github.com/mirror/virtualjaguar

## Contributors

This project is built on the work of many contributors. See the [full list on GitHub](https://github.com/libretro/virtualjaguar-libretro/graphs/contributors).

- Original Virtual Jaguar by David Raingeard (Potato Emulation).
- SDL/Linux/Win32 port by Niels Wagenaar & Carwin Jones (SDLEMU).
- Cleanups, GUI/Qt port, and ongoing upstream maintenance by James Hammons (Shamus).
- libretro core port by libretro/RetroArch contributors.
- libretro fork maintenance — Joseph Mattiello ([@JoeMatt](https://github.com/JoeMatt)).

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
