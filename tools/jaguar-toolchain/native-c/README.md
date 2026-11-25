# Native Jaguar C sample

Build a small 68000 program, draw a pixel-art badge, and break on the C function
that renders the asset. This uses GNU cross-tools and the Virtual Jaguar desktop
app. The sample can serve as a baseline for the JagStudio integration
tracked in [#1228](https://github.com/libretro/virtualjaguar-libretro/issues/1228).

Run these commands from this directory with Python 3, GNU Make and Virtual
Jaguar 3.13.1 or later available.

| Host | Install the cross-tools | Build |
| --- | --- | --- |
| macOS | `brew install m68k-elf-gcc` | `DEVELOPER_DIR=/Library/Developer/CommandLineTools make` |
| Debian or Ubuntu | `sudo apt install gcc-m68k-linux-gnu binutils-m68k-linux-gnu make python3` | `make CROSS=m68k-linux-gnu-` |

The output is `build/jaguar-native-c/badge.abs` from the repository root.
The ABS carries its own symbols, so the app can resolve `drawmark` without
a separate symbol file. The build also leaves the linked ELF for inspection
with the cross-toolchain.

```sh
# From the repository root:
virtualjaguar --reload-on-change build/jaguar-native-c/badge.abs
virtualjaguar --break drawmark build/jaguar-native-c/badge.abs
```

On macOS, use `"/Applications/Virtual Jaguar.app/Contents/MacOS/virtualjaguar"`
as the executable if it is outside your PATH. F9 continues from the breakpoint.
Edit `badge.inc` and rebuild to see the updated image with reload enabled.

From this directory, `make smoke` verifies the named breakpoint and the stock
badge's framebuffer hash. Add `CROSS=m68k-linux-gnu-` on Debian or Ubuntu, or
pass `VJ=/absolute/path/to/virtualjaguar` to select the app. The smoke run
writes `smoke.log` and `badge-frame.0.png` beside the executable.
