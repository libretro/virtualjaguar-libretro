# Virtual Jaguar libretro

[![C/C++ CI](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml)
[![Latest release](https://img.shields.io/github/v/release/libretro/virtualjaguar-libretro)](https://github.com/libretro/virtualjaguar-libretro/releases)
[![License](https://img.shields.io/github/license/libretro/virtualjaguar-libretro)](LICENSE)

An Atari Jaguar and Jaguar CD emulator packaged as a [libretro](https://www.libretro.com/) core, so it runs in RetroArch and other libretro frontends. It emulates the 68000, GPU, DSP and Object Processor, plus the CD drive controller, link port, Memory Track and GameDrive. It continues the original Virtual Jaguar project (see [Credits](#credits-and-license)).

Website, downloads, compatibility and enhancement write-ups: **[jaguar.provenance-emu.com](https://jaguar.provenance-emu.com)**.

## Install

- **RetroArch:** *Online Updater > Core Downloader > Atari - Jaguar (Virtual Jaguar)*.
- **Releases:** tagged builds are on [GitHub Releases](https://github.com/libretro/virtualjaguar-libretro/releases). Put the file in RetroArch's `cores` folder.
- **Nightly:** the rolling [`nightly` prerelease](https://github.com/libretro/virtualjaguar-libretro/releases/tag/nightly) is rebuilt on every push to `develop`. It is gated on compiling, not on the test suite.

Release builds cover Linux (x86_64, aarch64, i686, Raspberry Pi 1-5), macOS (Apple Silicon, Intel), Windows (x64, x86), Android (arm64-v8a, armeabi-v7a, x86_64, x86), iOS, tvOS, WebAssembly, PS Vita and Nintendo Switch. The matrix is in [`release.yml`](.github/workflows/release.yml). Other platforms get the core through the libretro buildbot.

### BIOS

None required. The Jaguar boot ROMs and CD BIOSes are embedded, and cartridges boot through a high-level BIOS by default. Optional external boot ROM and CD BIOS overrides, and exactly how they are found and checked, are in [`docs/bios.md`](docs/bios.md).

### Content

| Type | Extensions | Notes |
| --- | --- | --- |
| Cartridge | `j64`, `jag`, `rom`, `abs`, `cof`, `bin`, `prg` | Headerless raw homebrew is detected conservatively. Soft patching is covered in [`docs/rom-patches.md`](docs/rom-patches.md). |
| Jaguar CD | `cue` (with `bin`), `cdi`, `chd` | `.iso` is not supported. CHD needs session metadata from a recent `chdman`; see [`docs/jagcd-chd.md`](docs/jagcd-chd.md). Each release attaches a suitable `chdman` as `jagcd-tools-*.zip`. |

The core can also start with no content and take a disc through RetroArch's disk control. Cartridge EEPROM/SRAM and the CD Memory Track are saved as `<game>.srm` in RetroArch's `saves` folder; save states go in `states`.

## Features

- **Jaguar CD:** boots through a high-level CD BIOS (default) or the real CD BIOS on emulated BUTCH hardware; audio CDs and the Virtual Light Machine work. Memory Track saves are emulated. Load-time tuning: [`docs/cd-read-speed.md`](docs/cd-read-speed.md).
- **Enhancements, all core options:** true-color Gouraud shading, 2x internal resolution, widescreen, per-title defaults, and texture dump/replacement for HD packs. Overview and A/B images on the [website](https://jaguar.provenance-emu.com/enhancements.html); texture packs in [`docs/texture-dump.md`](docs/texture-dump.md).
- **Speed:** an accurate SIMD blitter (SSE2/NEON), idle-loop fast-forward for the GPU and DSP (on by default), and M68K and RISC clock-scale options. Which options help and which cancel each other: [`docs/settings-and-performance-guide.md`](docs/settings-and-performance-guide.md).
- **Save states, run-ahead, rewind, cheats, RetroAchievements:** supported. States from older versions still load ([`docs/savestate-compat.md`](docs/savestate-compat.md)).
- **Controllers:** joypad, Pro Controller, Team Tap, ST/Amiga mouse, Tempest rotary, analog and driving controllers, 6D stick and light gun, with per-axis tuning. See [`docs/input-devices-user-guide.md`](docs/input-devices-user-guide.md).
- **Link play:** JagLink/CatBox serial over TCP or RetroArch netplay, and the Voice Modem ([`docs/netlink-user-guide.md`](docs/netlink-user-guide.md), [`docs/voice-modem.md`](docs/voice-modem.md)).
- **Jaguar GameDrive:** detection and bank switching for GD-locked homebrew ([`docs/jgd-interface-notes.md`](docs/jgd-interface-notes.md)).
- **Debugging:** a crash watchdog that writes the failure signature to the frontend log, and an optional GDB/LLDB stub ([`docs/gdb-stub-guide.md`](docs/gdb-stub-guide.md)).

Every option is documented in the core options menu and in the [libretro docs](https://docs.libretro.com/library/virtual_jaguar/#core-options), which can lag the newest release.

## Compatibility

Nearly every cartridge and disc image in the maintainer's test corpus reaches game code, but some games are still broken, freeze, or run at the wrong speed. The website's [compatibility page](https://jaguar.provenance-emu.com/compatibility.html) is the player-facing summary.

The committed boot matrices are headless regression gates, not completion certificates: [`docs/cart-boot-matrix.md`](docs/cart-boot-matrix.md) and [`docs/cd-boot-matrix.md`](docs/cd-boot-matrix.md). The cartridge matrix runs only the Fast blitter, so it misses freezes that appear at default settings ([#800](https://github.com/libretro/virtualjaguar-libretro/issues/800)). A reaching-game-code row does not mean a game is playable. Known problems are tracked in the [v3.7.1 milestone](https://github.com/libretro/virtualjaguar-libretro/milestone/11), [`docs/cd-known-issues.md`](docs/cd-known-issues.md) and [`docs/cart-issue-triage.md`](docs/cart-issue-triage.md). libretro also keeps a [community compatibility list](https://docs.libretro.com/library/compatibility/jaguar/).

If a game works or fails for you, tell us in [Discussions](https://github.com/libretro/virtualjaguar-libretro/discussions) and attach the frontend log.

## Building

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # build for the host platform
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # -O0 -g
make platform=ios-arm64                      # cross-compile
make test                                    # test suite (builds with TEST_EXPORTS=1)
```

The result is `virtualjaguar_libretro.{so,dylib,dll}` in the repository root. The source is strict C89/GNU89 because the libretro buildbot uses MSVC; run `bash scripts/c89-lint.sh` before pushing. Platforms, cross-compiles and test details are in [`docs/agent/build.md`](docs/agent/build.md) and [`docs/agent/testing.md`](docs/agent/testing.md). Other docs: [source layout](docs/source-layout.md), [profiling](docs/profiling.md), [release process](docs/release-process.md).

## Contributing and support

- Pull requests target `develop`, not `master`. Branching, lint gates and commit style are in [CONTRIBUTING.md](CONTRIBUTING.md).
- Bugs: [Issues](https://github.com/libretro/virtualjaguar-libretro/issues); questions and compatibility reports: [Discussions](https://github.com/libretro/virtualjaguar-libretro/discussions).
- Security reports: [SECURITY.md](SECURITY.md). Do not file these as public issues.
- Release history: [`docs/RELEASE_NOTES_v3.7.0.md`](docs/RELEASE_NOTES_v3.7.0.md) and the earlier `docs/RELEASE_NOTES_*.md`.

## Credits and license

- Original Virtual Jaguar by David Raingeard (Potato Emulation).
- SDL/Linux/Win32 port by Niels Wagenaar and Carwin Jones (SDLEMU).
- Cleanups, the Qt GUI and upstream maintenance by James Hammons (Shamus). The upstream repository was `shamusworld.gotdns.org/git/virtualjaguar`; a [GitHub mirror](https://github.com/mirror/virtualjaguar) exists.
- libretro port by libretro/RetroArch contributors; current maintainer Joseph Mattiello ([@JoeMatt](https://github.com/JoeMatt)). Many Jaguar CD fixes in v3.7.0 are by Adrien Beudin ([@beudbeud](https://github.com/beudbeud)). Full list: [contributors](https://github.com/libretro/virtualjaguar-libretro/graphs/contributors).

Licensed under the [GNU General Public License v3.0](LICENSE).
