# Virtual Jaguar libretro — Atari Jaguar & Jaguar CD emulator

The actively maintained **Atari Jaguar and Jaguar CD emulator** for [libretro](https://www.libretro.com/) / RetroArch. This is the continuation of the Virtual Jaguar project, extensively rewritten and developed here: Jaguar CD support, an HLE BIOS, netplay link cable, RetroAchievements, savestates/run-ahead, and a hardware-accuracy programme measured against the Jaguar Technical Reference Manual.

[![C/C++ CI](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml)

## Features

- Emulates the Atari Jaguar's four processors: Motorola 68000, GPU, DSP, and Object Processor
- Supports NTSC and PAL video modes
- 2-player input with configurable numpad mapping
- Fast and legacy blitter modes (the legacy/accurate path is SIMD-accelerated on SSE2 and NEON)
- No BIOS files required: the Jaguar boot ROM and both Jaguar CD BIOSes are built into the core, with an HLE BIOS as the default boot path (see [BIOS](#bios))
- Save state, run-ahead (deterministic serialization), SRAM/EEPROM via the libretro SRAM interface, cheat codes, and a memory map for RetroAchievements
- Supported ROM formats: `.j64`, `.jag`, `.rom`, `.abs`, `.cof`, `.bin`, `.prg` (including inside ZIP archives), plus `.cue` and `.cdi` for Jaguar CD images, and conservative headerless raw homebrew loading
- Network link play (JagLink / CatBox emulation): Doom deathmatch, AirCars, BattleSphere Gold — via RetroArch netplay, or a direct TCP link between frontends on socket-capable platforms ([setup guide](docs/netlink-user-guide.md))

## Recent improvements (libretro fork)

This fork has diverged substantially from upstream Virtual Jaguar v2.1.0. See [docs/WHATSNEW](docs/WHATSNEW) for the full v2.2.0 changelog. Highlights:

- **HLE BIOS** now produces hardware-equivalent post-boot state — MEMCON1, clocks, GPU auth magic, OLP, exception vectors, TOM/JERRY timing — and the vast majority of commercial titles boot cleanly without any BIOS image. 200+ pin tests in `test_hle_bios` cover the contract.
- **Game-specific fixes**: Alien vs Predator red noise (M2 blitter `BKGWREN+BCOMPEN`), Doom resolution (proper `PWIDTH` pixel replication, replaces the legacy hack), and audio dropouts at frame edges across many titles (interleaved JERRY events). Jaguar CD support shipped in v3.0.0: CUE/BIN and CDI images boot through either an HLE CD BIOS or a real CD BIOS.
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

## BIOS

**No BIOS files are required.** The Jaguar console boot ROM and both Jaguar CD
BIOSes (retail and developer) are embedded in the core, so every boot mode
works out of the box:

- **Cartridges** — the `BIOS (Cartridges)` core option chooses between the
  HLE BIOS (default: the core performs the boot setup itself, skipping the
  boot animation) and the real boot ROM. The console boot ROM is always the
  embedded copy; it is never loaded from disk.
- **CD discs** — the `CD Boot Mode` core option chooses between the HLE CD
  BIOS (default, recommended) and a real CD BIOS (`Real BIOS`, or `Auto`,
  which currently also boots the real BIOS). In the real-BIOS modes the
  `CD BIOS Type` option selects the retail or developer image.

### Optional external CD BIOS override

In the real-BIOS CD modes only (`CD Boot Mode` set to `Real BIOS` or
`Auto`), a CD BIOS ROM file in the RetroArch `system`
directory takes precedence over the embedded images — useful if you want to
run a specific BIOS revision. The file must be exactly 256 KiB and can live
directly in `system/` or in an `Atari - Jaguar/`, `Atari - Jaguar CD/`,
`jaguar/`, or `jaguarcd/` sub-folder, under one of these names:

| Type | Accepted filenames |
| --- | --- |
| Retail | `[BIOS] Atari Jaguar CD (World).j64` / `.rom` / `.bin` |
| Developer | `[BIOS] Atari Jaguar Developer CD (World).j64` / `.rom` / `.bin` |
| Generic | `jaguarcd_bios.bin`, `jagcd_bios.bin`, `jaguarcd.bin`, `jagcd.bin`, `Jaguar CD BIOS.rom`, `Jaguar CD BIOS.bin` |

Selection is by filename only — the core does not verify which BIOS a file
actually contains, so a mislabelled or corrupt file will boot to a black
screen. If real-BIOS CD boots misbehave, remove or rename any CD BIOS files
in `system/` to fall back to the known-good embedded images.

## Documentation

- [Network play setup guide](docs/netlink-user-guide.md)
- [File format details](docs/README)
- [Source layout](docs/source-layout.md)
- [Changelog](docs/WHATSNEW)
- [Known issues & TODO](docs/TODO)
- [Security policy & binary verification](SECURITY.md)
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
- Current maintainer — Joseph Mattiello ([@JoeMatt](https://github.com/JoeMatt)). This repository is where Virtual Jaguar development continues today.

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
