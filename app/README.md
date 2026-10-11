# Virtual Jaguar 4.0 (standalone app)

A desktop front end for the Virtual Jaguar core: SDL3 for window, audio and
input, Dear ImGui for the UI, OpenGL 3.3 core for presentation, on macOS, Linux
and Windows. The core stays C89 and is linked **statically**; the app drives it
only through the standard `retro_*` API, the same one RetroArch uses, so every
option and save state behaves exactly as it does there.

Everything lives under `app/`. The core's own `make` build is untouched.

## Build

You need CMake 3.20+, a C99/C++17 toolchain and, on Linux, the SDL3 desktop dev
packages (see below). SDL3 (3.4.16) and Dear ImGui (1.92.8) are downloaded at
configure time, pinned by SHA256. Nothing is vendored in the repo.

```sh
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j
ctest --test-dir app/build -C Release --output-on-failure   # headless smoke tests
```

The binary is `app/build/virtualjaguar` (`app/build/Release/virtualjaguar.exe`
with the Visual Studio generator).

Use an already installed SDL3 instead of fetching one: `-DVJ_APP_SYSTEM_SDL3=ON`
(Homebrew `sdl3`, distro `libsdl3-dev`). For offline builds point
`-DFETCHCONTENT_SOURCE_DIR_SDL3=<dir>` and `-DFETCHCONTENT_SOURCE_DIR_IMGUI=<dir>`
at unpacked sources.

### macOS

Xcode command line tools and CMake (`brew install cmake`). No extra libraries.
The host `make` build of the core needs `DEVELOPER_DIR=/Library/Developer/CommandLineTools`
to avoid App Management prompts; the CMake/C++ build of the app does not, and
setting it there can hide the C++ standard library headers.

### Linux

```sh
sudo apt-get install cmake pkg-config libasound2-dev libpulse-dev libx11-dev \
  libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
  libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
  libegl1-mesa-dev libdbus-1-dev libudev-dev libwayland-dev wayland-protocols \
  libdecor-0-dev
```

### Windows

MinGW-w64 (MSYS2 `mingw-w64-x86_64-gcc`, `cmake`, `ninja`) is the supported
toolchain: it is what the core's own Windows release build uses.

```sh
cmake -S app -B app/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build app/build
```

MSVC (`cmake -S app -B app/build` then `cmake --build app/build --config Release`)
is wired up and runs in CI as an experimental, non-gating job: the repo's
`msvc-check` job has never compiled about 31 of the core's sources with `cl.exe`
(`scripts/check-msvc-sources.sh`, issue #679), so full-core MSVC is unproven.

## How the core is built here

`app/cmake/VJCore.cmake` **parses the source list out of `Makefile.common`**
(the same regexes as `scripts/check-package-sources.sh`), so adding a `.c` there
needs no change in `app/`. The flags mirror what `make -n` passes: `-D__LIBRETRO__`,
`-DINLINE=inline`, the warning set, `-fno-strict-overflow`, libchdr's own C99
translation unit. The SIMD blitter variant is chosen by the compiler: all three
`blitter_simd_<arch>.c` are built with `-DBLITTER_SIMD_AUTODETECT`, exactly as
`Package.swift` does (CI checks that exactly one `blitter_simd_ops` exists).
`src/core/version.h` is generated at configure time when `sh` is available.

No `-std=gnu89` is forced: `make` passes no `-std` either, and gnu89 changes
`inline` linkage. C89 conformance is gated by `scripts/c89-lint.sh`.

## Running

```sh
virtualjaguar game.j64            # cartridge (HLE boot, no BIOS needed)
virtualjaguar disc.cue            # or .chd / .cdi
virtualjaguar --option virtualjaguar_usefastblitter=enabled game.j64
virtualjaguar --help
```

Open a game with **File > Open** (Ctrl/Cmd+O), by **drag and drop** onto the
window, or on the command line. **File > Open recent** keeps the last ten.
Loading another game while one is running does a full unload, `retro_deinit`,
`retro_init`, `retro_load_game` cycle.

| Key | Action |
| --- | --- |
| Arrows, Z, X, C, Enter, Right Shift | D-pad, B, A, C, Option, Pause |
| number row / numpad | Jaguar keypad (the core reads the keyboard device) |
| F5 / F7 / F6 | save state / load state / next slot (Shift+F6 previous) |
| F8, Ctrl/Cmd+R | pause, reset |
| Tab (hold) | fast-forward |
| F11, Alt+Enter, Esc | fullscreen, leave fullscreen |
| F1 | pin or hide the menu bar in fullscreen |

Gamepads use SDL's standard layout (South/East/West = B/A/C, Back = Pause,
Start = Option); the left stick also drives the D-pad. Controller types per
Jaguar port (Team Tap, rotary, ...) are under **Settings > Input**.

**Settings > Core options** is generated from the core's options v2 definitions,
categories and `SET_CORE_OPTIONS_DISPLAY` visibility included, so a new core
option shows up with no change to the app. Changes are saved; options that say
"Restart required" apply on **Restart game to apply**.

Pacing follows the core's reported field rate (60.05445 Hz NTSC, 50.08013 Hz
PAL). Audio is opened at the core's rate and resampled by SDL, with a small
playback-ratio correction against the buffered amount so it tracks the video.

## Where files go

| | |
| --- | --- |
| normal | the OS per-user data folder (`SDL_GetPrefPath`): `~/Library/Application Support/VirtualJaguar/`, `%APPDATA%\VirtualJaguar\`, `~/.local/share/VirtualJaguar/` |
| portable | `data/` next to the executable, when a file named `portable.txt` sits beside it |
| override | `--data-dir DIR` |

Inside: `config.ini` (settings, core option overrides, recents), `saves/`
(`<game>.srm`: EEPROM, SRAM, Memory Track), `states/` (`<game>.slotN.state` plus a
`.png` thumbnail), `system/` (BIOS files, only if you want real ones), `imgui.ini`.

## Smoke tests

`virtualjaguar --headless` runs with SDL's dummy video and audio drivers: no
window, no GL. Each game is delivered as a drop event (the same handler the
window uses), run for `--frames` frames, and reported as an FNV-1a frame hash
(the same hash `test/harness/harness.c` uses). CTest runs:

- `smoke_yarc`: boot `test/roms/yarc.j64` under HLE with no BIOS, check the hash.
- `smoke_reload`: yarc, jagniccc, yarc in one process; the two yarc runs must
  match, which proves a different game in between left no static state behind.
- `smoke_state`: save state and thumbnail, run on, load, run on; both runs match.
- `smoke_cd`: load the synthetic CD image by path (no BIOS).

To regenerate the expected hash after an intentional emulation change:
`virtualjaguar --headless --frames 300 test/roms/yarc.j64` and update
`VJ_SMOKE_YARC_HASH` in `app/cmake/VJSmokeTests.cmake`.

## Layout

| file | job |
| --- | --- |
| `cmake/VJCore.cmake` | core + libchdr static libraries from `Makefile.common` |
| `cmake/VJSmokeTests.cmake` | CTest smoke tests |
| `src/core_host.*` | libretro host: environment calls, options, SRAM, states, discs |
| `src/app.*` | window, GL, main loop, pacing, events, save-state slots, headless runner |
| `src/video.*` | XRGB8888 texture and aspect-correct / integer-scaled presentation |
| `src/audio.*` | SDL3 audio stream with dynamic rate control |
| `src/input.*` | keyboard and gamepad to libretro input |
| `src/ui.*` | ImGui: menu bar, settings, options menu, state manager, OSD |
| `src/paths.*`, `src/config.*` | data folders (portable mode) and the settings file |

## Not in this first piece

Shaders (#834), cheats (#835), RetroAchievements (#836), input remapping (#837),
the game library (#838), packaging and signing (#839), the installer (#840).
Microphone and netpacket are reported unsupported.
