# Virtual Jaguar libretro

Port of the [Virtual Jaguar](http://shamusworld.gotdns.org/git/virtualjaguar) Atari Jaguar emulator to the [libretro](https://www.libretro.com/) API.

[![C/C++ CI](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml)

## Features

- Emulates the Atari Jaguar's four processors: Motorola 68000, GPU, DSP, and Object Processor
- Supports NTSC and PAL video modes
- 2-player input with configurable numpad mapping
- Fast and legacy blitter modes
- Optional BIOS boot sequence
- Supported ROM formats: `.j64`, `.abs`, `.jag`, `.rom` (including inside ZIP archives)

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

Original Virtual Jaguar by David Raingeard (Potato Emulation), ported by SDLEMU (Niels Wagenaar & Carwin Jones).

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
