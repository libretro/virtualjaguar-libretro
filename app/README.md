# Virtual Jaguar 4.0 (standalone app)

A desktop front end for the Virtual Jaguar core: SDL3 for window, audio and
input, Dear ImGui for the UI, OpenGL 3.3 core for presentation, on macOS, Linux
and Windows. The core stays C89 and is linked **statically**; the app drives it
only through the standard `retro_*` API, the same one RetroArch uses, so every
option and save state behaves exactly as it does there.

Everything lives under `app/`. The core's own `make` build is untouched.

## Build

You need CMake 3.20+, a C99/C++17 toolchain and, on Linux, the SDL3 desktop dev
packages (see below). SDL3 (3.4.16), Dear ImGui (1.92.8) and rcheevos (12.5.0) are downloaded at
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
  libdecor-0-dev libcurl4-openssl-dev
```

`libcurl4-openssl-dev` is only for the game library's box art and database downloads
(see [Game library](#game-library)); without it the app still builds and runs, it just never
downloads anything.

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
| number row / numpad, `-` `=` | Jaguar keypad 0-9, `*`, `#` |
| A S D, Q E | Jaguar Pro Controller X Y Z and left / right shoulder (keypad 9 8 7, 4 6) |
| F2 | keypad overlay |
| F3 | controls overlay: a cheat sheet of your current bindings (also View menu) |
| Esc / F10 | release the captured mouse; F10 or a click on the picture captures it again (see below) |
| F5 / F7 / F6 | save state / load state / next slot (Shift+F6 previous) |
| F8, Ctrl/Cmd+R | pause, reset |
| Tab (hold) | fast-forward |
| F11, Alt+Enter, Esc | fullscreen, leave fullscreen |
| F1 | pin or hide the menu bar in fullscreen |

Gamepads use SDL's standard layout (South/East/West = B/A/C, Back = Pause,
Start = Option); the left stick also drives the D-pad. Controller types per
Jaguar port (Team Tap, rotary, ...) are under **Settings > Input > Devices & players**.

### Controls and the keypad

**Settings > Input** edits the bindings: pick a device (the keyboard, every
gamepad, or one connected controller model), pick the scope (everywhere, or
this game only), then **Set** / **Add** a binding and press the key, button or
stick direction ("press a button to bind"; Esc cancels), **Unbind**, or go back
to **Default**. Per control, the first of these that defines it wins: this game
and this controller, this game, this controller, everywhere, built-in default.
Games are identified by the CRC32 of the cartridge image (the key the core's
title database uses; copier headers stripped); discs fall back to the file name.
Everything is stored in `config.ini` as `input.*` keys. Controllers SDL does not
know can be added with a `gamecontrollerdb.txt` (SDL's format) in the data folder.

The default gamepad layout reaches all twelve keypad keys without the overlay:

| pad | keypad |
| --- | --- |
| North, L1, R1, L2, R2, L3, R3 | 0, 1, 2, 3, 4, 5, 6 |
| right stick left / up / right / down | 7, 8, 9, `*` (dominant axis only, never two keys) |
| Guide | `#` |
| Share / Capture, or F2, or View > Keypad overlay | on-screen keypad (D-pad moves, A/B presses, C closes) |

How it reaches the core: pad buttons ride the RetroPad bitmask; the RetroPad has
no button for keypad 7, 8, 9, `*`, `#`, so keypad keys are delivered as the
RETRO_DEVICE_KEYBOARD keys `libretro.c` polls (player 1: `0-9 - =`; player 2:
`p q w e r t y u i o [ ]`). The app announces a keyboard after every load, which
the core needs before it polls them. This is the core's default input path, so the
core's own remapping options (`virtualjaguar_alt_inputs`, `*_numpad_to_kb`, `*_retropad_*`
and the per-port device options) are forced to their defaults and hidden: this tab
and **Devices & players** do all of that. Team Tap sockets read only the RetroPad, so
there keypad 0-6 ride X, L, R, L2, R2, L3, R3 and keypad 7-9, `*`, `#` are unreachable
(a limit of the core).

### Devices, players and the mouse

**Settings > Input > Devices & players** (everywhere, or this game only; stored as
`input.port.N` / `input.game.<id>.port.N`) picks the device in each Jaguar port from the
core's own list and calls `retro_set_controller_port_device`:

| device | driven by (defaults, all rebindable under *Key / button bindings*) |
| --- | --- |
| Standard Joypad, Pro Controller | the pad and keypad bindings above |
| ST / Amiga mouse (port 2) | the mouse or trackpad (captured); or the right stick / arrows; Z X or South East are the buttons |
| Rotary (either port) | mouse left / right, or two keys (Left / Right), or the right stick; speed in *Tuning* |
| Analog, driving, paddle | left stick or `A D W S`; driving: Right trigger / `E` accelerator, Left trigger / `Q` brake; D = North or `V` |
| 6D | both sticks (`A D W S`, `J L I K`), triggers (`Q E`), shoulders (`U O`), A-F, Option = G, Pause = Rezero |
| Light gun (port 1) | the mouse over the picture, left click fires, right click (or `R`) fires off-screen to reload; a stick or arrows can aim too |
| Team Tap | one controller (or the keyboard) per socket, chosen under *Players* |

The *Players* section lists the active player slots (two, plus three more behind each
Team Tap) and which controller plays each: *Automatic* hands controllers out in plug-in
order (the first two play players 1 and 2, as before), or pick one by model and number.
The light gun aims with the rectangle the picture is drawn in, so scaling, aspect
overrides, integer scaling and HiDPI all land the shot on the pixel under the pointer.

The mouse is captured (SDL relative mode) only while a mouse or rotary is in a port,
the game window has focus, the game is running and no menu, popup or Settings window is
open. **Esc** or **F10** releases it (F10 may need Fn on a Mac keyboard); click the picture or press F10 to capture again.

The **controls overlay** (F3, the Touchpad button on a gamepad, or View > Controls overlay)
shows the bindings in effect for Player 1's device - the keyboard's or the gamepad's,
whichever you last used - and never takes input. It appears by itself for a few seconds
when a game starts with no gamepad on Player 1, and once per controller model for
gamepads; **Settings > Input > Devices & players** turns that off.

**Settings > Core options** is generated from the core's options v2 definitions,
categories and `SET_CORE_OPTIONS_DISPLAY` visibility included, so a new core
option shows up with no change to the app. Changes are saved; options that say
"Restart required" apply on **Restart game to apply**.

Pacing follows the core's reported field rate (60.05445 Hz NTSC, 50.08013 Hz
PAL). Audio is opened at the core's rate and resampled by SDL, with a small
playback-ratio correction against the buffered amount so it tracks the video.

## Shaders

**Settings > Video > Shader** (or **View > Shader**) runs the picture through a
RetroArch `.slangp` preset, on the GL 3.3 presenter, unmodified. Presets come
from [librashader](https://github.com/SnowflakePowered/librashader) (MPL-2.0),
which the app loads **at run time**: the build needs no Rust and links nothing
extra. If the library is missing, the wrong version, or a preset does not
compile, the picture is drawn plain and a one-line message says why; it is never
a failure.

- **Library.** At configure time CMake fetches librashader's two MIT-licensed C
  headers (tag `librashader-v0.12.0`, pinned by SHA256; `-DVJ_LIBRASHADER_INCLUDE_DIR=<dir>`
  for offline builds, `-DVJ_APP_SHADERS=OFF` to leave it out). At run time the
  app looks for `librashader.dylib` / `.so` / `.dll` in `$VJ_LIBRASHADER` (a file
  or folder; a path that does not exist means "missing"), next to the executable
  (and `../Frameworks` in a macOS bundle), in `<data folder>/librashader/`, and
  then the system library path (Homebrew's `/opt/homebrew/lib` included).
  `-DVJ_APP_FETCH_LIBRASHADER=ON` downloads the pinned prebuilt runtime for macOS
  and Windows (arm64/x86_64) and copies it next to the executable. Upstream
  publishes no Linux binary: install the distro/OBS package or build it, and it
  is picked up from the system path.
- **Presets.** `shaders/` beside the executable holds two small first-party presets
  (`vj-sharp-bilinear`, `vj-scanlines`, also the offline test fixtures) and, by
  default, a subset of [libretro/slang-shaders](https://github.com/libretro/slang-shaders)
  under `shaders/slang-shaders/`: the CRT (CRT-Royale, CRT-Geom, CRT-EasyMode, ...),
  scanline and interpolation families plus every directory they reference (~23 MB,
  ~900 files). CMake downloads the tarball for one pinned commit at configure time
  (SHA256-verified; nothing is committed to this repo), extracts those directories
  untouched and copies them beside the executable. **Licensing:** slang-shaders has no
  single licence; each shader states its own (GPL, MIT, public domain, ...) in its
  header and some directories carry LICENSE/README/THANKS files, all of which travel
  with the copy together with a `NOTICE-virtualjaguar.txt`. No network, or a hash
  mismatch, only means no bundled presets (a warning, never a build failure);
  `-DVJ_APP_BUNDLE_SLANG_SHADERS=OFF` skips it for distro/offline builds and
  `-DVJ_SLANG_SHADERS_ARCHIVE=<tarball>` supplies a local copy. For the rest of
  slang-shaders, or a newer copy, set **Preset folder** to a full checkout; the list
  is searched recursively and filterable, and preset parameters get sliders.
- **Scaling.** Integer scaling and the aspect modes are independent of shaders
  (`computePresentRect` in `present_util.cpp`): the chain renders into a texture
  of exactly the rectangle they pick, so a CRT preset at 3x integer scale looks
  the same as the plain picture at 3x. Both paths share that one function.
- **Settings keys.** `shader.enabled`, `shader.preset`, `shader.folder`.

`vj_shader_tests` (CTest `shaders_unit`, `shaders_bundled`, `shaders_library`, `shaders_gl`) covers
the pure logic and the missing-library fallback anywhere; the other two need the
library (and a display, for the GL one) and report **skipped**, not passed,
without them. Point `VJ_LIBRASHADER` at the library, and optionally
`VJ_TEST_PRESETS="a.slangp;b.slangp"` / `VJ_TEST_SLANG_DIR=<checkout>`, to run them.

## Game library

**File > Game library** (Ctrl/Cmd+L) is an optional grid of your games. It never replaces the
file picker, drag and drop or the command line, which all keep working.

1. **Add folder...** one or more folders. They are walked recursively (8 levels, symlinked
   folders inside them are not followed) for the formats the core reads: cartridges
   `.j64 .jag .rom .abs .cof .prg .bin` and discs `.cue .chd .cdi`. A `.bin` that a `.cue`
   references is a disc track, not a cartridge, and so is any `.bin` over 8 MiB. Zip is not
   scanned: the core cannot load it.
2. **Identification** by CRC32 against libretro-database (see licences below):
   - cartridges against the No-Intro "Atari - Jaguar" dat, using the whole file first and then
     the file minus a 512-byte copier header (the same rule as the core's
     `DetectPrependedHeaderSize`, so a headered dump of a known game still matches);
   - CD images against the Redump "Atari - Jaguar CD" dat. Redump hashes each track, so a
     `.cue` is identified by the CRC32 of the first file it references (Track 1). `.chd` and
     `.cdi` carry no cheap track hash and are matched by file name only.
   A name-only match is badged `?` (titled and given art from the dat entry), an unidentified
   file `??` (titled from its file name, no art). Hover a tile for the path, CRC32 and how it
   was matched.
3. **Box art** comes from [libretro-thumbnails](https://github.com/libretro-thumbnails/Atari_-_Jaguar)
   (`thumbnails.libretro.com/Atari - Jaguar/Named_Boxarts/<dat title>.png`; the libretro rule
   replaces `& * / : ` " < > ? \ |` with `_`). It is fetched only for tiles you can see, one title
   at a time, on a background thread, and cached under `<data>/thumbnails/` forever: art you
   have seen works offline. When there is no box art the title screen, then a snapshot, is used;
   when upstream has none the tile shows a coloured placeholder and the miss is remembered for
   30 days. Upstream coverage is the limit here: of the 209 titles in the cartridge dat, 70 have
   art under their exact dat name (64 box art); of the 26 Jaguar CD titles, 12 have a snapshot
   and none a box art. (Measured against libretro-thumbnails at the pinned dat's date.)
   **Download box art** turns all downloading off.
4. **Launch** is a click on the tile; it posts the same "open this path" event as the file
   dialog. **Close on launch** hides the window afterwards.

Files: `<data>/library/scan.tsv` (the hash cache: a rescan only stats files whose size and
modification time are unchanged), `<data>/library/db/` (databases fetched by **Update
database**), `<data>/thumbnails/`. Settings are the `library.*` keys in `config.ini`.

The two dat files are fetched at configure time, pinned to a libretro-database commit and
SHA256 (`VJ_DB_COMMIT` in `CMakeLists.txt`), and copied next to the executable in
`libretro-db/`. **Update database** downloads the current files into `<data>/library/db/`; the
newer of the two copies (by the dat's own `version`) is the one used. Offline build:
`-DVJ_APP_LIBRETRO_DB_DIR=<folder holding 'Atari - Jaguar.dat' and 'Atari - Jaguar CD.dat'>`;
`-DVJ_APP_BUNDLE_DB=OFF` skips them (the library then titles games from file names until
**Update database**).

HTTP is the only network code. SDL3 has no client, so `src/http.cpp` is a blocking GET with
three compile-time backends: **libcurl** (macOS SDK, `libcurl4-openssl-dev` on Linux, MinGW
if installed), **WinHTTP** on Windows (part of the OS: no dependency), or none
(`-DVJ_APP_HTTP=OFF`, or libcurl not found), in which case downloads are simply unavailable.
It is only ever called from worker threads, never from the UI or emulation loop.

**Licences.** The dats are CC BY-SA 4.0 (libretro-database, derived from No-Intro and Redump
data). They are not committed to this repository, only downloaded and shipped unmodified
beside the binary, with attribution here. The box art is the property of its owners; the app
downloads it to the user's own machine on demand, as RetroArch does, and never redistributes
it. PNGs are decoded with SDL3's own loader (`SDL_LoadPNG_IO`): no image library was added.

## Where files go

| | |
| --- | --- |
| normal | the OS per-user data folder (`SDL_GetPrefPath`): `~/Library/Application Support/VirtualJaguar/`, `%APPDATA%\VirtualJaguar\`, `~/.local/share/VirtualJaguar/` |
| portable | `data/` next to the executable, when a file named `portable.txt` sits beside it |
| override | `--data-dir DIR` |

Inside: `config.ini` (settings, core option overrides, recents), `saves/`
(`<game>.srm`: EEPROM, SRAM, Memory Track), `states/` (`<game>.slotN.state` plus a
`.png` thumbnail), `system/` (BIOS files, only if you want real ones), `imgui.ini`.

## Cheats

Emulation > Cheats... lists the cheats of the libretro cheat database for the running
game, with a toggle each, plus manual codes (hex `ADDRESS:VALUE`, `+` to join
several). What is on is remembered per game (`<data>/cheats/games/<key>.ini`, keyed
by the cartridge CRC32; discs by the core's title row or file name) and sent to the
core again after every load and state restore.

The cheat files are not committed to this repository (the libretro database is
CC BY-SA 4.0), but they are **bundled with builds**: configure downloads the 41
`cht/Atari - Jaguar` files of one pinned libretro-database commit (SHA-256 per file)
to `<build>/cheats/`, next to the program, and `cmake --install` puts them in
`<prefix>/bin/cheats/` with the database `LICENSE` and a credit README beside them, so
the attribution travels with the files (a packaging step takes them from the install
tree). The download is fail-soft: with no network the configure still succeeds, the
cheats are then simply not bundled (never without their license). For offline or
distro builds: `-DVJ_APP_FETCH_CHEATS=OFF`. You can always add your own `.cht` files
to `<data>/cheats/`, or point the window's "Cheat library" box (or `--cheats-dir`) at
a folder of them. A set is picked by name from the core's own title (or the file
name); with several matches, or none, you choose. Jaguar cheats are credited to Gent
of Emucheats.

Database entries are RetroArch memory cheats (decimal address/value, width in
`memory_search_size`); they become the core's hex codes, value little-endian in
memory as RetroArch writes it. `cheatN_enable` is ignored: nothing starts on.

## RetroAchievements

**Achievements > Sign in...** takes your RetroAchievements user name and password
once, gets a login token from the server and keeps **only the token** (`ra.user` and
`ra.token` in `config.ini`; the password is wiped from memory as soon as the request
is built). The token is a credential: treat `config.ini` like a password file. Sign out
deletes it. The menu also has the hardcore toggle, "include unofficial achievements"
and the **Achievement list** (progress, rich presence). Unlocks, game completion,
leaderboard attempts / submissions, progress indicators and server problems show as
toasts.

It is built on rcheevos' `rc_client` (v12.5.0, MIT, fetched at configure time like SDL3):

- **Game identification.** rc_client hashes the file itself: the whole cartridge file for
  Atari Jaguar (console 17), the second-session boot executable for a `.cue` disc (Atari
  Jaguar CD, console 77, rcheevos' built-in cue/bin reader). **`.chd` and `.cdi` discs are not
  identified yet**: rcheevos' built-in reader covers cue/bin only and this app has not
  supplied a CHD/CDI reader (RetroArch installs its own CD reader). The game runs and the
  app says why. A `.bin` file is a cartridge for this core and is hashed as one.
- **Memory.** The core publishes a memory map (`RETRO_ENVIRONMENT_SET_MEMORY_MAPS`: 2 MB
  system RAM at `$000000`). rcheevos' `rc_libretro.c`, the same code RetroArch builds,
  maps addresses onto it; nothing is byte-swapped. The RAM is a big-endian image and
  Jaguar sets use the big-endian operand sizes (`0xI` 16-bit, `0xG` 32-bit).
- **Hardcore.** While signed in, hardcore is on and a game is being tracked: loading
  save states is refused (saving works), pausing is rate-limited (`rc_client_can_pause`),
  enabling it mid-game (or signing in mid-game with it on) restarts the game for a clean run, core options that rcheevos disallows (today
  `virtualjaguar_pal = enabled`) pause hardcore, and `Achievements::allowCheats()`,
  `allowRewind()`, `allowSlowMotion()` and `allowFrameAdvance()` are false so those
  features (cheats, rewind, slow motion and frame advance are separate pieces) must ask.
  Fast-forward is allowed (it is not slower than real time). Softcore (the default)
  restricts nothing, and each state file gets a `.rcp` sidecar so a restored state does
  not replay stale hit counts.
- **Cheats.** No cheat reaches the core while hardcore is active: the cheat manager's `apply()`
  consults `Achievements::allowCheats()`, so loads, state restores, the window, manual codes and
  `--cheat`/`--cheat-on` all obey it. Entering hardcore empties the core's list, leaving it (softcore,
  sign-out, RetroAchievements off) puts the saved per-game selections back; they are never changed on
  disk meanwhile. The Cheats window stays openable with its controls disabled and says why.
- **Network.** libcurl (found at configure time; `libcurl4-openssl-dev` on Linux) or
  WinHTTP on Windows, on one worker thread; replies are handled on the main thread, so a
  slow network never delays a frame. Without libcurl the app still builds and the
  Achievements menu says it is unavailable. Unlocks are retried by rc_client when the
  network drops.
- **Tests** never touch the network or an account: `--ra-mock DIR` answers rc_client's
  requests from `app/tests/ra/<api>.json`. See `cmake/VJAchTests.cmake`.

## Smoke tests

`virtualjaguar --headless` runs with SDL's dummy video and audio drivers: no
window, no GL. Each game is delivered as a drop event (the same handler the
window uses), run for `--frames` frames, and reported as an FNV-1a frame hash
(the same hash `test/harness/harness.c` uses). CTest runs:

- `smoke_yarc`: boot `test/roms/yarc.j64` under HLE with no BIOS, check the hash.
- `smoke_reload`: yarc, jagniccc, yarc in one process; the two yarc runs must
  match, which proves a different game in between left no static state behind.
  The number of visible core options must match too (it differs between a
  cartridge and a disc, so a stale visibility table would show).
- `smoke_state`: save state and thumbnail, run on, load, run on; both runs match.
- `smoke_bad_option`: an `--option` the core does not register is rejected, not
  silently ignored.
- `smoke_cd`: load the synthetic CD image by path (no BIOS).
- `library_unit` (`vj_library_tests`): dat parsing, CRC identification of synthetic files
  (copier-headered and plain carts, a `.cue` set), thumbnail naming against known libretro
  examples, cache paths, a scan of a temp folder with the hash cache, and on POSIX the whole
  art pipeline (scan, identify, download, cache, decode, texture; database update; then
  offline) against a loopback HTTP server. No internet, no GL.
  `vj_library_tests --scan DIR --db DIR` prints identification counts for a folder (dev tool).
- `remap_unit`: the remapping logic with no core (bindings, precedence, config
  round trip, keypad translation, overlay, bind capture, content identity).
- `input_e2e`: drives the real core with every Jaguar control on both ports and
  reads back the console's joypad matrix; keypad 7-9, `*`, `#` included.
- `devices_unit` / `devices_e2e`: the non-pad devices (kinds, per-game persistence, Team Tap
  player slots, light-gun geometry, mouse-capture policy, locked core options, the Pro
  Controller keyboard layout, the controls cheat sheet and its auto-show rule), then the
  same host-input routing driving the real core and reading back the mouse / rotary
  quadrature state, analog / driving / 6D values decoded off `$F14000` as a game does,
  the paddle ADC at `$F17C00`, the light gun's aim point and the Team Tap socket slots.
  Every group has a negative control (no host input reads the rest value).
- `controls_overlay_*`: the overlay through the real app: shown for a keyboard player,
  hidden after "don't show again", text follows a saved rebinding.

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
| `src/video.*` | XRGB8888 texture and presentation (plain, or through the shader chain) |
| `src/present_util.*` | GL-free: where the picture goes (aspect / integer / stretch), `.slangp` parsing and folder scan |
| `src/shaders.*`, `src/shaders_ui.cpp` | librashader loader and GL filter chain; the Shader section of Settings > Video |
| `shaders/` | first-party presets copied beside the executable |
| `cmake/VJShaders.cmake` | pinned header fetch, optional prebuilt runtime, shader tests |
| `src/audio.*` | SDL3 audio stream with dynamic rate control |
| `src/input.*` | keyboard and gamepad snapshots to libretro input |
| `src/remap.*`, `src/remap_ui.cpp` | profiles, per-game overrides, keypad delivery, overlay, bind capture, and their UI |
| `src/devices.*` | device kinds, per-port / per-game selection, player slots (Team Tap), light-gun geometry, mouse-capture policy, locked core options |
| `src/router.*` | host input to what the core reads for every device (SDL-free, tested against the real core) |
| `src/devices_ui.*` | Settings > Input > Devices & players, claiming devices with the core, per-frame mouse capture |
| `src/cheatsheet.*`, `src/cheatsheet_ui.cpp` | the controls cheat sheet: text, auto-show rule, window |
| `src/ui.*` | ImGui: menu bar, settings, options menu, state manager, OSD |
| `src/library.*` | game library logic: dat parser, CRC identification, folder scan + hash cache, libretro-thumbnails names |
| `src/library_service.*` | scan / decode / download worker threads, art cache and texture handover |
| `src/library_ui.cpp` | the Game library window |
| `src/http.*` | blocking HTTP(S) GET: libcurl, WinHTTP or nothing |
| `src/cheats.*` | `.cht` parsing, conversion to core codes, per-game cheat state (no SDL, unit tested) |
| `src/cheats_core.cpp`, `src/cheats_ui.cpp` | core glue (retro_cheat_*, title identity, CLI) and the Cheats window |
| `cmake/VJCheatData.cmake`, `cmake/VJCheatManifest.cmake`, `cmake/VJCheatTests.cmake` | pinned optional database download, cheat tests |
| `src/paths.*`, `src/config.*` | data folders (portable mode) and the settings file |
| `src/achievements.*` | RetroAchievements: rc_client glue, memory, hardcore policy, mock server for tests |
| `src/achievements_ui.cpp` | ImGui: sign-in, achievement list, toasts, trackers |
| `src/ach_http.*` | libcurl / WinHTTP worker for rc_client's requests |
| `cmake/VJRcheevos.cmake`, `cmake/VJAchTests.cmake` | rcheevos fetch + static lib; headless achievement tests |

## Not in this first piece

Packaging and signing (#839), the installer (#840).
Microphone and netpacket are reported unsupported.
