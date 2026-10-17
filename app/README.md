# Virtual Jaguar app (standalone desktop front end)

A desktop front end for the Virtual Jaguar core: SDL3 for window, audio and
input, Dear ImGui for the UI, OpenGL 3.3 core for presentation, on macOS, Linux
and Windows. The core stays C89 and is linked **statically**; the app drives it
only through the standard `retro_*` API, the same one RetroArch uses, so every
option and save state behaves exactly as it does there.

Everything lives under `app/`. The core's own `make` build is untouched.

Installable packages (macOS zip, Windows installer and portable zip, Linux AppImage and
`.deb`) are built by CI and attached to the releases: see [`packaging/README.md`](packaging/README.md).
To package and sign your own build locally (a Developer ID or Apple Development certificate
found in your keychain on macOS, a `.pfx` or store certificate on Windows, optionally configured in a
gitignored `.env`), run `bash app/packaging/local-package.sh --sign [--notarize] [--build-dir DIR]`: see
"Signing locally" in [`packaging/README.md`](packaging/README.md).

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

FreeType 2.14.1 is the default font rasteriser: it is fetched (pinned, SHA-256 checked), built static with every
optional library off, so it needs no system libraries. `-DVJ_APP_FREETYPE=OFF` uses Dear ImGui's built-in
stb_truetype instead (no hinting or anti-aliasing choices); offline builds use `-DFETCHCONTENT_SOURCE_DIR_FREETYPE=<dir>`.

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
whichever you last used - and never takes input. A click or tap anywhere, Esc or F3 hides it
(the click is not passed to the game). It is not shown by itself: a game that starts with no
gamepad on Player 1 gets a short "Press F3 for the controls" hint, once per session.
**Settings > Interface > Overlays > Show the controls overlay when a game starts** (off by
default) brings the overlay up at boot instead - for keyboard players every time, for gamepads
once per controller model - and hides it again after a few seconds.

**Settings > Core options** is generated from the core's options v2 definitions,
categories and `SET_CORE_OPTIONS_DISPLAY` visibility included, so a new core
option shows up with no change to the app. Changes are saved; options that say
"Restart required" apply on **Restart game to apply**. Every core option category and
every other Settings tab has a **Reset group** button (it skips options the app locks and
options a Netplay session or `--option` pins), and **Reset all settings...** asks first and
leaves library folders, recents, covers, saves, the RetroAchievements login, key and button
bindings and per-game overrides alone unless you tick the matching boxes.

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
  headers (the tag in `app/packaging/librashader/pin.env`, pinned by SHA256; `-DVJ_LIBRASHADER_INCLUDE_DIR=<dir>`
  for offline builds, `-DVJ_APP_SHADERS=OFF` to leave it out). At run time the
  app looks for `librashader.dylib` / `.so` / `.dll` in `$VJ_LIBRASHADER` (a file
  or folder; a path that does not exist means "missing"), next to the executable
  (and `../Frameworks` in a macOS bundle), in `<data folder>/librashader/`, and
  then the system library path (Homebrew's `/opt/homebrew/lib` included).
  The release packages (macOS universal, Windows, AppImage, `.deb`) ship it:
  Windows and Apple-silicon dev builds download upstream's pinned prebuilt
  (`-DVJ_APP_FETCH_LIBRASHADER=OFF` for offline builds), while the **macOS (both
  architectures) and Linux libraries are built from source in CI**
  ([`librashader.yml`](../.github/workflows/librashader.yml), pinned commit and
  archive SHA-256 in `app/packaging/librashader/pin.env`), because upstream's
  Intel-macOS zip holds an arm64 dylib and it publishes no Linux binary. Any
  build can be given a library it built itself with `-DVJ_LIBRASHADER_LIB=<file>`;
  it is copied beside the executable and installed by `cmake --install`.
  When no library can be used, **Settings > Video > Shader** says why: *not
  bundled in this build* (none found anywhere), *the bundled runtime is
  missing*, or *failed to load: ...* with the loader's reason (wrong
  architecture, wrong ABI version, a missing dependency), and links here. It is
  informational: the app never installs or downloads anything for you.
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
- **OpenGL version.** The chain compiles to the context's GLSL version (4.10 on macOS, usually
  4.60 elsewhere). macOS's OpenGL 4.1 cannot build presets with arrays of arrays (CRT-Geom,
  CRT-Hyllian and 13 others in the bundle); the list greys them out and says why. Details:
  [packaging/README.md](packaging/README.md#macos-notes).
- **Settings keys.** `shader.enabled`, `shader.preset`, `shader.folder`.

### Getting the shader runtime

Only needed for a build that does not ship one (a distro or source build, or a
package where the file was removed). The app loads any librashader of the right
ABI (`LIBRASHADER_CURRENT_ABI`, currently 2) and CPU architecture; put
`librashader.so` (Linux), `librashader.dylib` (macOS) or `librashader.dll`
(Windows) next to the executable, in `<data folder>/librashader/`, or anywhere on
the system library path, or point `VJ_LIBRASHADER` at the file.

- **Linux, a system package.** librashader's author publishes packages on the
  openSUSE Build Service (build recipes for RPM and Arch-style packages are in
  upstream's `pkg/obs`); pick your distribution on
  <https://software.opensuse.org//download.html?project=home%3Achyyran%3Alibrashader&package=librashader>.
  The package installs `librashader.so` (and `librashader.so.2`, whose SONAME is the ABI
  version) in the system library folder, where the app finds it by name; nothing else to do. If
  your distribution only has `librashader.so.2`, add a `librashader.so` symlink next to the
  app or set `VJ_LIBRASHADER=/path/to/librashader.so.2`. There was no Arch User Repository
  package when this was written (October 2026); build from source, below.
- **From source (Linux, macOS, Windows).** Needs stable Rust 1.88 or newer, a
  C/C++ compiler, curl and tar:

  ```bash
  rustup target add <triple>      # e.g. x86_64-unknown-linux-gnu, x86_64-apple-darwin
  bash app/packaging/librashader/build-from-source.sh <triple> ./lr
  # -> ./lr/librashader.so (or .dylib / .dll), built from the pinned, SHA-256-verified source
  cmake -S app -B build -DVJ_LIBRASHADER_LIB="$PWD/lr/librashader.so"   # optional: ship it with your build
  ```

  This is exactly what CI runs (`cargo xtask build --profile optimized --target <triple> -- --locked`
  inside upstream's tree). Upstream's `README.md` documents the same command.
- **macOS.** The release `.app` already includes a universal library. For a source
  build on Apple silicon the default configure downloads upstream's arm64 library;
  on an Intel Mac, or to avoid the download, build it as above with
  `x86_64-apple-darwin` and pass `-DVJ_LIBRASHADER_LIB`. A Homebrew or
  hand-built copy in `/opt/homebrew/lib` or `/usr/local/lib` is picked up too (a
  signed release app additionally needs the library-validation entitlement in
  `app/packaging/macos/entitlements.plist` to load a library someone else signed).

`vj_shader_tests` (CTest `shaders_unit`, `shaders_bundled`, `shaders_library`, `shaders_gl`) covers
the pure logic and the missing-library fallback anywhere; the other two need the
library (and a display, for the GL one) and report **skipped**, not passed,
without them. Point `VJ_LIBRASHADER` at the library, and optionally
`VJ_TEST_PRESETS="a.slangp;b.slangp"` / `VJ_TEST_SLANG_DIR=<checkout>`, to run them.

## Game library

The library is an optional grid of your games. It never replaces the file picker, drag and drop
or the command line, which all keep working.

**Home screen.** With no game loaded the main window *is* the library: search, filter, size
slider, **Add folder...**, rescan, and a click on a tile launches it. With no folders yet it shows
a short welcome with an **Add folder...** button and the open / drag-and-drop hints. **Tile size**
(0.5x to 2.5x, default 1.0x, `library.size`) is the slider, and also a trackpad pinch (where SDL
reports one: macOS), Ctrl/Cmd+mouse wheel and Ctrl/Cmd + `+` / `-` / `0` (reset) while the pointer is
over the grid or the library has the keyboard focus; never during gameplay or while typing in a
field. Loading a game
replaces it with the game; **File > Close game** brings it back. **Settings > Interface > Show the
game library on the home screen** (`ui.home_library`, also View > Library on home screen) turns it
off for the old plain welcome. The separate **Game library** window (File > Game library, Ctrl/Cmd+L)
stays for in-game use (pick another game without closing this one); on the home screen Ctrl/Cmd+L
puts the cursor in the search box instead of opening a second copy of the same view.

**Sections.** Tabs split the library into **Cartridges**, **Jaguar CD** and **BIOS & system** (plus Favorites, Recently played and Save states, below)
(the choice is remembered, `library.section`). BIOS and system files are not games and are never
in the game grids: files are recognized by CRC32 (the Series K and Model M boot ROMs, the two
Stubulator developer ROMs, the retail and developer CD BIOS and the Memory Track cartridge: the
checksums the core itself checks), by the names the core searches for (`jagboot.rom`, `boot.rom`,
`jagcd.bin`, `[BIOS] Atari Jaguar ... .j64`, ...), by "bios" or "memory track" in the name of a small
file (512 KiB or less), or by a `[BIOS]` entry in the dat. The core's own system folder is always
scanned for these too (only BIOS files are kept from it), so the tab shows what the core can read.
A BIOS tile says what it is and whether the core's own search finds it where it sits, and **a click opens
its menu**: **Use as the Cart boot ROM / CD BIOS** (the core runs exactly that file, whatever it is called and
wherever it lives, see below), **Boot** (below) and, for a file the core would find in its system folder under the
right name, **Copy to the system folder as ...** (the fixed names, for RetroArch sharing the folder; never over a
file already there). BIOS files are found anywhere under a library folder (any depth the walk reaches, any
upper/lower case in the name). The Series K image, the Memory Track and files whose size the core rejects get no
copy offer. The core has
every standard image built in, so none is required. Loose `(Track N).bin` files are skipped like the
tracks a `.cue` owns.

**Favorites, recently played and save states.** Click the star on a tile (or right-click > **Add to
Favorites**) and the game is a favorite; every game that boots, from the library, a drop, File > Open or the
command line, goes to the front of **Recently played** (the last 20; right-click > **Remove from Recently
Played**, or **Clear**). Where each list shows is up to you, separately, under **Favorites & recents...** in the
library toolbar and in Settings > Interface (`library.favorites.show`, `library.recents.show`: 0 hidden, 1 its
own tab, 2 a strip at the top of the Cartridges and Jaguar CD tabs, 3 both, the default). A tab lists both
media (favorites by title, recents newest first); a strip is one scrolling row at the top of each media tab with
that media's games only, under the same search and filter. The **Save states** tab lists every game that has
states, newest first, with the screenshot saved with each state, its slot and when it was saved: click one to
start the game from that moment (a state of the game already running just loads into it), **Delete** asks first
and removes the state, its screenshot and its RetroAchievements sidecar. Right-clicking any game tile gives
**Play**, **Load state** (the game's states with screenshots, newest first), **Add/Remove Favorite**,
**Remove from Recently Played**, the cover items, **Show in Finder / File Explorer / file manager** (the
containing folder) and **Copy path**. Lists remember a game by path, checksum and dat name together, so moving
a folder or an update that identifies a game does not lose it; a game whose file is away (an unplugged drive)
is skipped but not forgotten. A game booted from outside the library folders is not listed. Hardcore
RetroAchievements mode refuses a state load before leaving the running game.
**Resume where I left off.** Settings > Interface > Save states: **Off / Ask / Always** (`saves.resume`, default
Ask). When a game is closed (File > Close game), left for another game, or the app quits, its state is written
as an **Auto-save**: its own slot beside the numbered ones (`<file name>.auto.state` + `.auto.png`, one per game,
only the latest, written atomically). The next time that game boots, Always loads it right after boot, Ask shows
"Resume from <time>?" with **Resume**, **Start fresh** and an "Always resume without asking" box, Off does
nothing (and writes nothing). The Save states tab and the tile's Load state list show it as **Auto-save**, and
**Delete** works on it. Rules: never in RetroAchievements hardcore (state loads are refused, so there is no
prompt either) and never during netplay (a state would desynchronise the link); no auto-save for a game that
ran under 5 emulated seconds (still the boot logo; a quick open-and-close must not replace a real auto-save with
a title screen) or that the crash watchdog flagged (resuming would land in the hang); File > Reload game and
headless runs neither keep nor resume one; picking a state in the library skips the offer. Another running copy
(instance 2+) keeps its own auto-saves in `states/instance-N/` so two windows on one game cannot overwrite each
other's; the numbered states stay shared.

The lists live in `<data>/library/user.tsv`, not in `config.ini` (rules for several copies in
[Running several instances](#running-several-instances)). Save states are named `<file name>.slotN.state` in
`<data>/states/`, as always, so two games whose files share a name share states.

**BIOS & boot.** The BIOS & system tab starts with the same panel as **Settings > BIOS** (also File > Boot BIOS >
BIOS & boot settings...; the home screen's "BIOS & boot..." button reaches it with no folder added), so a BIOS
can be chosen with no game loaded and whatever the loaded game is (the core hides the CD options while a
cartridge runs, and the Core options tab is empty before the first load).

- **Which file.** Each role, the console boot ROM (128 KiB) and the Jaguar CD BIOS (256 KiB), is **Auto** or one
  chosen file. Auto is the core's default: a file in the system folder under the names it searches, else the image
  built into the core (**Auto picks** = Series K / Model M / Custom for the boot ROM, Retail / Developer for the CD
  BIOS). A chosen file is handed to the core by path through a private call (`VJ_ENV_GET_BIOS_PATH`,
  `docs/app-frontend-extension.md`), so it needs no copying and no special name. The core still checks it (exact
  size; a CD BIOS also by checksum or header) and, if it refuses, uses its own choice and the panel says so. The
  choice is saved per role (`bios.cart`, `bios.cd` in config.ini; Auto = no key) and applies when the game starts.
- **When it is used.** The boot ROM runs when cartridges boot through the **Real boot ROM** (instead of HLE), for
  GPU-only demos, and on **Boot**. The CD BIOS runs when CD games boot through **Real BIOS** (instead of HLE), for
  audio CDs and on **Boot**. These two switches are the core options BIOS (Cartridges) and CD Boot Mode, shown here
  whatever is loaded. Panel lines say "Running now: ..." from what the core reported staging (built in, system
  folder file, or your file, with its CRC32) and warn when a chosen file is idle because the matching switch is HLE.
- **Boot.** **Boot the CD BIOS (no disc)** / **Boot this ROM (empty slot)** (panel, tile menu, File > Boot BIOS)
  starts the console with nothing inserted: the CD BIOS comes up on its disc screen with the Memory Track plugged
  in; the boot ROM shows the Jaguar logo on an empty slot, as the hardware does. A tile's **Boot** uses that file
  for this boot only. The known Memory Track cartridge dump boots as a cartridge (its own manager program); a
  Memory Track image the core does not recognise, a wrong-sized file or another system file is disabled with the
  reason in a tooltip. Command line: `--boot-bios cart|cd`, `--cart-bios FILE`, `--cd-bios FILE`.
- **Import.** **Import BIOS file...**, or dropping (or File > Open'ing) a boot ROM or CD BIOS on the window, copies
  it to `system/bios/` (never over a different file), chooses it and rescans. A dropped BIOS is imported, not run
  as a cartridge; the dialog also takes a dump with no recognisable name, by its size.

**File > Game library** (Ctrl/Cmd+L) opens the window.

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
     `.cue` is identified by the CRC32 of the first file it references (Track 1). A `.chd` is
     identified the same way: its Track 1 sectors are read back as the Redump `.bin` has them (the
     way the core reads them: subcode dropped, audio put back in `.bin` byte order, a virtual
     pregap left out). A `.chd` with several audio tracks and no session metadata (which the core
     refuses too) is not hashed. A `.cdi` is hashed best-effort from its Track 1 descriptor;
     rips whose layout differs from the descriptors (the core repairs these) will not match.
     This is verified against synthetic images; real Redump CHDs are still to be checked.
   - when the checksum does not match (a bin/cue or `.cdi` that is not a Redump dump) the disc is
     matched by name: the file name, then the name of its folder, against the dat's titles with
     case, punctuation, region and `(Track N)` / `(Disc N)` tags ignored. An exact title wins; a
     file name that is the start of exactly one longer title also matches (file name only, 4+
     characters). A name that fits two games (a two-disc set, "Space" for "Space Ace" and "Space
     Ace Gold") matches neither: it stays unidentified rather than guessing.
   A name-only match is badged `?` (titled and given art from the dat entry), an unidentified
   file `??` (titled from its file name, no art). Hover a tile for the path, CRC32 and how it
   was identified (checksum, file name, folder name, or not at all).
3. **Box art** comes from [libretro-thumbnails](https://github.com/libretro-thumbnails/Atari_-_Jaguar)
   (`thumbnails.libretro.com/Atari - Jaguar/Named_Boxarts/<dat title>.png`; the libretro rule
   replaces `& * / : ` " < > ? \ |` with `_`). It is fetched only for tiles you can see, one title
   at a time, on a background thread, and cached under `<data>/thumbnails/` forever: art you
   have seen works offline. When there is no box art the title screen, then a snapshot, is used;
   when upstream has none the tile shows a coloured placeholder and the miss is remembered for
   30 days. Upstream coverage is the limit here: of the 209 titles in the cartridge dat, 70 have
   art under their exact dat name (64 box art); of the 26 Jaguar CD titles, 12 have a snapshot
   and none a box art. (Measured against libretro-thumbnails at the pinned dat's date.)
   For Jaguar CD titles libretro-thumbnails is asked first and, on a miss, the project site's mirror
   (`jaguar.provenance-emu.com/thumbnails/Atari - Jaguar CD/...`, box art from TheGamesDB plus title screens and snaps
   made with this emulator, hosted until libretro-thumbnails has a Jaguar CD repository, issue #141 there); each
   source has its own cache folder, so mirror art never hides upstream art that appears later. A libretro-thumbnails
   miss is remembered for 30 days; a mirror miss only until the app quits, so art the site gains shows on the next
   launch. Set `library.art_mirror` in `config.ini`
   to another base URL, or to an empty value to turn the mirror off.
   **Download box art** turns all downloading off.
   **Your own covers.** libretro-thumbnails has no Jaguar CD repository at all, so no disc gets
   downloaded art. A tile with no art is a drawn card with the game's title and a `CD` / `Cart`
   badge. Right-click a tile > **Set cover image...** (PNG) to give it one, or drop a PNG onto the
   tile while the library is showing (a PNG dropped anywhere else is opened as before, and fails
   as not a game); **Remove cover image** undoes it. The PNG is copied to
   `<data>/covers/custom/<key>.png` (the dat title when the game is identified, else a checksum
   of the disc or of its path). For many at once, put PNGs named after the game in
   `<data>/covers/`, e.g. `<data>/covers/Myst (USA).png` (dat title first, then the title shown
   on the tile), then press **Rescan**. A cover you set beats the folder, which beats a
   downloaded image, which beats the card. Only PNG is read: SDL3 has no JPEG decoder and the app
   adds no dependency for one. Covers larger than 8192 px or 32 MB are refused.
4. **Launch** is a click on the tile; it posts the same "open this path" event as the file
   dialog. **Close on launch** hides the window afterwards.

Files: `<data>/library/scan.tsv` (the hash cache: a rescan only stats files whose size and
modification time are unchanged), `<data>/library/db/` (databases fetched by **Update
database**), `<data>/thumbnails/`, `<data>/covers/` (your cover art). Settings are the `library.*` keys in `config.ini`.

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

## Running several instances

Run as many copies of the app as you like: to test netplay on one machine (host in one window,
join `127.0.0.1` in the other), or for local multiplayer on a big screen. Nothing is locked to a
single instance.

- **Any platform:** **File > New window** (Ctrl/Cmd+N) starts another copy. On macOS it runs
  `open -n` on the app bundle, which is also what you do by hand: a double-click only activates the
  copy that is already running, so from a shell use `open -n "/Applications/Virtual Jaguar.app"`
  (or `open -n -a "Virtual Jaguar" --args --data-dir DIR`). On Windows and Linux just start the
  program again (a Linux AppImage starts itself again from `$APPIMAGE`). A copy started with
  `--data-dir` hands the same folder to the copies it opens.
- **Numbering.** The first copy running on a data folder is instance 1 and behaves exactly as it always
  did. The next ones are 2, 3, ... (the lowest number nobody holds; an OS file lock on
  `instance-N.lock` in the data folder, released automatically when a copy quits or crashes). The number
  is in the window title (`[Instance 2]`). Copies with different `--data-dir` folders, or a portable
  copy, share nothing and are all instance 1.
- **What each copy writes.** Instance 2+ never rewrites the shared files:

| File | Instance 1 | Instance N (2+) |
| --- | --- | --- |
| `config.ini` (settings, bindings, core options, recents, RetroAchievements user name) | read and written as before | read at start (so it has your settings); **never written** |
| `config-N.ini` | not used | its window size and its controller choice only |
| `imgui.ini` | `imgui.ini` | `imgui-N.ini` |
| game saves `saves/<game>.srm` (EEPROM, SRAM, Memory Track) and the GameDrive SD folder | `saves/` | `saves/instance-N/`; a game's first run there starts from a copy of instance 1's save (read only) |
| save states `states/`, cheat selections `cheats/games/` | shared | shared: written only when you press save, each file atomically, last save wins. A state saved in one copy loads in another |
| library cache, box art, database files | shared | shared, written atomically with a per-process temporary name |
| favorites and recently played `library/user.tsv` | shared | shared and written by every copy, but as **operations**: each change re-reads the file, applies that one change (star, play, remove, clear) and writes it atomically, so a copy never replays a stale snapshot over another's list; two changes in the same few milliseconds can lose one. Other copies pick a change up within a second |
| library display options (`library.*` in `config.ini`) | saved with the other settings | read, not saved (like every setting) |
| netplay address (`system/vj_netlink.txt`) | written when you join, as before | not written (no copy writes it): the address and port go to the core as per-process option overrides, so two copies can join different hosts |

  Why: two copies both writing one `.srm` or one whole-file `config.ini` would silently lose
  one side's changes. Settings you change in a second window are therefore not remembered, so make
  them in the first window. Instance numbers are ordinal slots (the window you opened second),
  not identities, which is why nothing else is persisted under them.
- **Controllers.** SDL gives every connected controller to every running copy, so two windows would both
  answer the same pad. **Settings > Input > Devices & players > Controllers used by this window** picks
  *All controllers* (the default), *Keyboard only*, or *Controller k* for each pad, remembered per
  instance number. Controller numbers follow the order SDL lists them (connection order). A window pinned
  to one controller keeps reading it while another window has focus (SDL otherwise ignores controllers in the
  background), so two players can play side by side; the keyboard goes to whichever window has focus. Which
  Jaguar port or player a controller drives inside a window is still chosen under **Devices & players**.
- **Netplay on one machine:** start a game in instance 1 and choose Netplay > Host; in instance 2 start the
  same game and choose Netplay > Join with address `127.0.0.1` and the same port. Two copies that both
  host need different ports (the window offers 42171-42174).

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
once, gets a login token from the server and keeps **only the token** (the password is
wiped from memory as soon as the request is built). The user name is `ra.user` in `config.ini`;
the login token is kept in the system keychain (macOS Keychain, Windows Credential Manager, or
the Linux Secret Service through libsecret, loaded at run time) and not in `config.ini`. A token
that an earlier version left in `config.ini` is moved into the keychain at start-up. Where no
keyring is available (no libsecret, no running keyring service, a Flatpak without the secret
portal) the token stays in `config.ini` as `ra.token`, Settings says "Token stored in the settings
file because no system keyring is available", and that file is then a credential: treat it like a
password file. Sign out deletes the token from the keychain (and from `config.ini`). A second copy
of the app (see below) reads the keychain and writes it only when you sign in or out there. The menu also has the hardcore toggle, "include unofficial achievements"
and the **Achievement list**, a dashboard with your avatar, badges and the Jaguar games that
have achievements (see below). Unlocks, game completion,
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
- **Pictures.** Your avatar (`rc_client_user_get_image_url`), the game icon
  (`rc_client_game_get_image_url`) and every achievement's badge, earned or greyed
  (`badge_url` / `badge_locked_url`), show in the Achievements window, the menu and the unlock
  toasts. They download on a worker thread of their own (so they never queue behind rc_client's
  requests) and are kept in `ra-cache/` in the data folder: a badge never changes, so a saved
  file is reused forever; the avatar is saved per `avatar_last_updated`. The folder is kept under
  48 MB (oldest pictures go first) and at most 256 are held as textures at once (least recently
  drawn goes first; it comes back from disk when asked for again). They follow the Library's
  **download art** switch: with it off, or with **Achievements > Download pictures** unticked,
  only pictures already saved are shown and nothing is fetched. Headless runs and `--ra-mock`
  sessions never fetch.
- **Dashboard.** **Achievements > Achievement list...** is a window with three tabs.
  *This game*: icon, title, hardcore / softcore rule in force, progress bar, and the achievement
  list with badges, points, rarity, earned date and a Hardcore / Softcore marker, with a filter
  (all, unlocked, locked, unlocked in hardcore, missable), a sort (default, title, points, rarest,
  recently unlocked, type) and a search box. *Jaguar games*: every Atari Jaguar (console 17) and
  Jaguar CD (console 77) game that has achievements (`rc_client_begin_fetch_game_list`), with icon,
  achievement count, points and your progress in each (`rc_client_begin_fetch_all_user_progress`,
  signed in only); filter by all / in my library / in progress / completed / not started. Games in
  your library get a **Play** button: cartridges are matched by the MD5 RetroAchievements lists
  for the game (hashed on a background thread), everything else by title against the library's
  names. The list is saved in `ra-cache/games-<console>.tsv` and refreshed when it is a day old
  (and only while downloads are on); **Refresh** fetches it again on request. *Profile*: avatar,
  name, hardcore and softcore points, and what you unlocked in this session. rc_client has no
  call for the account's older unlock history (that needs the web API and its key), so it
  is not shown. Neither list needs a web API key: both use the login token like the rest.
- **Tests** never touch the network or an account: `--ra-mock DIR` answers rc_client's
  requests from `app/tests/ra/<api>.json` (`<api>_<console>.json` first for `systemgames` and
  `allprogress`, one list per console). `vj_ra_dash_test` covers the picture cache (paths, disk,
  offline mode, LRU), the games-list parsing and library matching, the cache file, and the
  filters and sorts; `ra_dashboard_mock` fetches the lists through the mock server. See
  `cmake/VJAchTests.cmake`.

## Netplay

The **Netplay** menu opens a window for the link-cable games (Doom deathmatch, BattleSphere,
AirCars, and Ultra Vortek over the Voice Modem) between two computers on a local network (or two windows on one machine, see
[Running several instances](#running-several-instances)). It
configures the core's existing link networking (`docs/netlink-user-guide.md`) through core options
and takes status from the core's event channel (see Core events below; a core that never answers
falls back to reading its `[NETLINK]` log lines); it is not rollback netplay, and play over
the Internet or a VPN is not supported. Both players need the same game and the same version of
the app, and the same device (JagLink / CatBox cable or Voice Modem).

- **Host a game** lists this machine's LAN addresses and starts listening on a port: one of the
  core's four presets (42171-42174) or any port from 1 to 65535 under **Custom...**.
- **Join a game** finds hosts on the LAN (the core's UDP beacon) or takes a typed host name or IPv4
  address, set straight on the core's host option (the core resolves names itself).
- Mode, host and port are session overrides and are never saved, so relaunching the app does not start
  listening on its own; the link device is an ordinary saved option.
- Command line twin: `--netplay host|join|join-discovered|browse|loopback`, `--netplay-addr`,
  `--netplay-port`, `--netplay-device jaglink|voicemodem`. **Advanced > Echo test** runs the link
  against itself with no partner.

## Platform integration

What the app does so it behaves like a program of the OS it runs on (#833). The code is behind one
interface, `src/platform/platform.h`; every OS-specific file is small and a platform with nothing to
add compiles a no-op, so adding a platform feature never touches the others.

| | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Menu bar | native, with Cmd shortcuts (the in-window bar is hidden) | in-window | in-window |
| Dock / taskbar menu | Dock menu: recent games, library, open, pause, reset | Jump List: recent games | desktop Actions: Open Game Library, Boot Without Cartridge |
| Recent games | `NSDocumentController` (Dock, Open Recent) | Explorer Recent and the Jump List (`SHAddToRecentDocs`) | not written (no GTK; see below) |
| File types (double-click, Open With, drop on the icon) | `CFBundleDocumentTypes` + an exported type for `.j64` / `.jag` | installer task: per-user `OpenWithProgids` (and the default for `.j64` / `.jag`) | shared-mime-info XML + `MimeType=` |
| Window | fullscreen follows the green button / Ctrl+Cmd+F; App Nap held while a game runs | title bar follows the app theme (dark or light), per-monitor-v2 DPI, long paths, one taskbar button with the installer shortcut (AppUserModelID) | the application id is the Wayland `app_id` / X11 `WM_CLASS`, so the icon and "pin to dock" match the `.desktop` file |
| Tray / menu-bar icon | optional (Settings > Interface > Desktop) | optional | optional where the desktop has a StatusNotifier host (KDE and others; stock GNOME needs an extension) |
| Screensaver / display sleep | allowed in the library and while paused, blocked while a game runs | same | same (through the portal inside Flatpak) |
| Show in Finder / Explorer | File > Show in Finder | - | opens the folder |

**One model, many surfaces.** `src/platform/commands.cpp` builds the menus (File, Emulation, View,
Achievements, Netplay, Window, Help, the tray and the Dock menu) from a snapshot of the app state, and
every click is posted as an SDL user event and run by the main loop (`App::runNativeCommand`), never
inside Cocoa's menu-tracking loop or SDL's tray callback. The macOS menu bar is built after `SDL_Init`
over SDL's default one and leaves SDL's app delegate alone (it turns Finder's "open" into the drop event
the app already handles). Only Cmd-modified shortcuts are bound there; F5 / F7 / F8, Tab and the other hotkeys
stay with the app. While a native menu is open the macOS event loop is blocked by Cocoa, so emulation and
audio pause for as long as the menu is held open.

**File types.** The associated extensions are `.j64 .jag .rom .abs .cof` (games) and `.cue .chd .cdi`
(discs). `.bin` and `.prg` open through File > Open and drag and drop but are too generic to claim. The list lives
in `src/platform/commands.h` (`VJ_ASSOC_LIST`); the CTest `platform_assoc_sync` fails when the Info.plist,
installer, `.desktop`, mime XML or the application id drift from it. On Windows and Linux a double-clicked
game starts a new process, which is a second instance when the app is already running (see "Running several
instances": its own save folder, settings not kept); on macOS the running copy opens it.

`virtualjaguar --dump-native-menu` (macOS) prints the real menu bar and Dock menu and checks every item;
exit 77 means no window-server session (ssh) or not macOS.

**Windows outside the main window, and the in-window menu bar.** Dear ImGui is the `-docking` build of the same
version (`v1.92.8-docking`; pinned in `CMakeLists.txt` and the Flatpak manifest), used only for its multi-viewport
mode: Settings > Interface > Desktop > "Let windows move outside the main window" lets a Settings / Achievements /
Cheats / Netplay / debug-tool window dragged past the edge of the main window become an OS window of its own
(real title bar, no Dock / taskbar entry, positions kept in `imgui.ini`). Default on for macOS and Windows, off on
Linux; it is forced off on Wayland (a client cannot position its own extra windows there; X11 and XWayland are
fine), while the window is fullscreen, and in headless runs (`src/platform/viewports.*`, unit-tested in
`platform_unit`). Docking is left off: it would put tab bars and drop targets on windows people already arranged.
The coordinates of the main window are screen coordinates while this is on, so code that mixes them with SDL's
window-relative pointer events subtracts `GetMainViewport()->Pos`. On macOS, "Show the menu bar inside the window"
(`ui.menubar_in_window`, off by default; F1 flips it) keeps the ImGui menu bar as well as the native one.

## Discord

**Settings > Discord** shows what you are playing on your Discord profile (Rich Presence), and the
activity doubles as app discovery: friends see "Playing Virtual Jaguar", the game's box art, and a
**Get Virtual Jaguar** button that opens the site's download page (`https://jaguar.provenance-emu.com/app.html`).
Discord has to be running on the same computer; nothing is sent anywhere else, and nothing is installed (the app
speaks Discord's local RPC protocol itself, no Discord SDK).

- **While playing:** the title (dat name without region and revision tags) as the headline; the line below says
  `Atari Jaguar` or `Jaguar CD` plus `| Paused`, `| Netplay: hosting / joined` (never an address),
  `| Hardcore` and `| 12/30 achievements` when a RetroAchievements game is tracked; the elapsed time counts from
  boot. The large picture is the game's box art, a public https address (libretro-thumbnails, or the project
  site's Jaguar CD mirror) that Discord's media proxy fetches. It is used only when the library already
  downloaded that picture, so Discord never shows a broken image; otherwise the application's `jaguar` (cartridge)
  or `jaguarcd` (disc) asset is shown. The small picture is the app icon (`app`) with "Virtual Jaguar <version>".
- **In the library or an empty window:** "Browsing the library" with the `library` picture.
- **Buttons:** **Get Virtual Jaguar**, and **View on RetroAchievements** (the game's page) while a RetroAchievements
  game is tracked. Discord shows buttons to other people only, so your own profile never shows them.
- **Settings:** *Show what I'm playing on Discord* (default on), *Show game artwork*, *Show buttons*, and the
  connection status ("Connected to Discord", "Discord is not running"). Config keys `discord.enabled`,
  `discord.art`, `discord.buttons`.
- **Application ID:** the default is the project's "Virtual Jaguar" Discord application, set by the CMake cache
  string `VJ_DISCORD_APP_ID` (`-DVJ_DISCORD_APP_ID=` empty turns the feature off; a fork passes its own). The config
  key `discord.app_id` (Settings > Discord > Application ID) overrides it.
- **Several windows:** only the first copy (instance 1) publishes, so two windows never fight over one status
  (see [Running several instances](#running-several-instances)).
- **Behaviour:** the connection and every write live on a background thread; if Discord is not running the app
  looks again every 15 seconds and never waits on it, quitting takes the status down within a quarter of a second,
  and updates are sent only when something changed and at most 4 per 20 seconds (Discord allows 5).

### Maintainer setup (Discord developer portal)

1. Create an application named exactly **Virtual Jaguar** (Rich Presence cannot set the name; this is what people
   see after "Playing"). Its Application ID is the number in `VJ_DISCORD_APP_ID`.
2. Upload these art assets under *Rich Presence > Art Assets*, with exactly these lowercase keys (at least 512x512):
   `jaguar` (cartridge fallback large image), `jaguarcd` (Jaguar CD fallback), `library` (browsing), `app` (the app icon).
3. CI: the repository variable `DISCORD_APP_ID` (Settings > Secrets and variables > Actions > Variables) overrides the
   CMake default in `app-package.yml`; leave it unset to use the default. An application ID is public, not a secret.
4. Test from a second Discord account: buttons are hidden on your own profile.

## Neuro API (AI players)

**Settings > Neuro** connects the app to a [Neuro game API](https://github.com/VedalAI/neuro-sdk) server, so
Neuro-sama, or any AI backend that speaks the API (the SDK's Randy, Tony and Gary included), can play as Player 1.
The app registers the Jaguar controller as actions (`press_buttons`, `button_sequence`, `hold_buttons`,
`release_all`), plus a game profile's own moves (Tempest 2000 is built in; more go in `<data folder>/neuro/*.json`),
asks for the next move whenever the last one has played out, and presses the chosen buttons frame by frame.

- `--neuro ws://HOST:PORT` turns it on for one run; the default server is `$NEURO_SDK_WS_URL`, else
  `ws://localhost:8000` (Randy). Only `ws://`; no TLS.
- Config keys `neuro.enabled`, `neuro.url`, `neuro.auto_force`, `neuro.force_interval`, `neuro.game_name`.
- The socket lives on a background thread and reconnects by itself; the game never waits on it.
- [docs/neuro.md](../docs/neuro.md) has the actions, the profile format and testing with Randy.

Tests: `ctest -R discord` (frame encode/decode, JSON escaping, socket paths per OS, the rate limiter, the activity
built from app state, and a loopback test against a fake Discord server on a Unix socket; skipped on Windows).

## Typed settings

The core publishes every option as a list, but for a handful it reads any value, so Settings >
Core options shows the core's presets plus a **Custom...** entry that opens a box (applies on Enter
or when it loses focus; an invalid value says why in red and is not applied). The keys are in
`src/typed_options.h`, with the range the core actually honours: link port, GDB port (1-65535),
GDB halt timeout (1-3600 s), mouse / rotary sensitivity (1-1600%), the per-axis dead zone (0-64),
offset (-64..64) and response curve (1-800%), the link host (host name or IPv4, 1-127
characters, no IPv6: the link sockets are IPv4), and the M68K / RISC clock scales (a percentage,
25-400, e.g. `175%`). Saved values and `--option K=V` use the same validators, so
`--option virtualjaguar_netlink_port=50123` works; every other option stays a strict list.
Tested by `typed_options` (validators, CoreHost, and the real core reading a custom port, host and
clock scale).

## Core events

The core reports link up/down, link statistics, wire speed and crash-watchdog hits to the app
through a private libretro environment call that only this app answers
(`src/core/vj_frontend_ext.h`, `docs/app-frontend-extension.md`); under RetroArch it is a no-op.
`CoreHost` answers the handshake and hands each event to `onEvent`; Netplay consumes them instead
of scraping log text (the log parser remains the fallback), and a crash-watchdog hit shows as an
on-screen message naming the signature.

## Debug tools

The **Tools** menu is a set of developer windows over a private debug interface the core hands only to this app
(`src/debug/vj_debug_iface.h`, `docs/app-frontend-extension.md`); RetroArch never sees it.  Full reference:
[`docs/app-debug-tools.md`](../docs/app-debug-tools.md).

- **Memory viewer** (hex / ASCII grid over every bus region, go-to, search, change highlighting, edits, "Add as cheat")
  and **Palette (CLUT)** (256 swatches as CRY or RGB16, with an editor).
- **Cheat scanner**: find an address by narrowing (changed / increased / equal to ...), then keep it as an ordinary cheat.
- **Object list**: the whole display list the Object Processor walks, decoded, with a bitmap preview, an outline over the
  game picture, PNG export and a pixel painter (RAM only).
- **Blitter log**: the last 256 blits with their decoded command and registers.
- **Textures**: browse the core's texture dump, export a tile, replace its art with your own PNG.
- **Audio scope**: waveforms and peak meters of the audio the core produces.
- **F9** breaks into the game (continue when halted), **F10** steps while halted; a "HALTED" bar shows over the picture.
  The CPU debugger window is still to come.

Writes go to RAM freely; hardware registers need "Allow register writes"; the CD registers (`$DFFF00-$DFFFFF`) and
`B_CMD`, `G_CTRL`, `D_CTRL`, `OLP`, `LTXD`, `RTXD` are never written from here.  Everything greys out (and closes) while
RetroAchievements hardcore is on.  Extra app instances keep their own windows but do not dump textures.  Developers can
open one at start-up: `--show-window memory|palette|scanner|objects|blitter|textures|scope` (with `--screenshot`).

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
- `smoke_boot_cd_bios` / `smoke_boot_cart_rom`: boot the CD BIOS with no disc, and the boot ROM with an empty
  slot, from the images built into the core (no files needed); both must draw (no blank frame) and match a hash.
- `bios_unit` (`vj_bios_tests`): scanning BIOS files in nested folders with upper and mixed case names, what may
  be chosen (size), what Boot does and why not, the saved choice, importing (reuse, never overwrite, refusals),
  and how the running BIOS is described. Synthetic files only.
- `bios_e2e` (`vj_bios_e2e`): the real core and the private BIOS channel: a chosen CD BIOS / boot ROM file is the
  image that runs (checksum, source), a refused or vanished file falls back, a frontend without the capability is
  never asked, and a no-content boot enters the boot ROM or the CD BIOS as asked.
- `library_unit` (`vj_library_tests`): dat parsing, CRC identification of synthetic files
  (copier-headered and plain carts, a `.cue` set), thumbnail naming against known libretro
  examples, cache paths, a scan of a temp folder with the hash cache, and on POSIX the whole
  art pipeline (scan, identify, download, cache, decode, texture; database update; then
  offline) against a loopback HTTP server; favorites / recents (persistence, ordering, matching by
  path, checksum and name, the shared file under two simulated copies), the favorites / recents display
  options and tab layout, and save-state naming, listing and deletion over synthetic state files. No
  internet, no GL.
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
- `theme_unit` (`vj_theme_test`): the theme choice (System / Dark / Light, the OS's answer, the theme-changed
  event) and WCAG contrast of every palette and style colour in both themes, and of the in-game overlays on
  white and black frames; the custom-theme files (round trip, import validation, the themes folder, read-only
  secondary copies) and the editor's contrast warnings.
- `controls_overlay_*`: the overlay through the real app: hidden by default (with the F3 hint),
  shown for a keyboard player when the setting is on, hidden when it is off, text follows a saved rebinding.
  `devices_unit` covers the show / hide policy (click, tap, Esc, F3, timeout).

- `debug_*`: the debug tools.  `debug_session_unit` and the per-tool unit tests (`debug_memory_unit`, `debug_scan_unit`,
  `debug_op_unit`, `debug_texture_unit`, `debug_blitter_unit`) run the pure logic against a fake core
  (`tests/debug_mock.h`); `debug_memory_ui_smoke` and `debug_scan_ui_smoke` draw the windows headlessly over it;
  `debug_e2e`, `debug_scan_e2e` and `debug_op_e2e` drive the real core on `yarc.j64` (halt / step / continue, a found counter
  kept as a working cheat, the display list captured by the core).

To regenerate the expected hash after an intentional emulation change:
`virtualjaguar --headless --frames 300 test/roms/yarc.j64` and update
`VJ_SMOKE_YARC_HASH` in `app/cmake/VJSmokeTests.cmake`.

## Window sizing and UI screenshots

**Settings > Interface > Appearance** picks the colour theme: System (the default) follows the OS and switches live when it does, or Dark / Light / a custom theme made in **Customize...** and kept as `<data>/themes/<name>.ini` (`--theme system|dark|light|custom:NAME` sets it for one run); the palettes are in `src/theme.cpp`.

The UI scale follows the display's content scale (200% on a 4K Windows/Linux display; 1.0 on a Retina
Mac, where ImGui renders at the pixel density by itself). `ui.scale` in `config.ini` (default 1.0) multiplies it.

### Fonts

Settings > Interface > Font picks the typeface and its size (11-24 pt, default 13): **Retro** (Dear ImGui's embedded
pixel font, the look the app always had, still the default), **System UI** (San Francisco on macOS, Segoe UI on
Windows, the desktop's sans-serif on Linux through `fc-match`, with DejaVu / Noto / Liberation as fallbacks),
**Monospace** (SF Mono / Menlo, Consolas, DejaVu Sans Mono) or **Custom file...** (a `.ttf` / `.otf` / `.ttc`; it
is checked, then copied to `<data>/fonts/`). Changes apply at once and persist as `ui.font.*` in `config.ini`.
Text is rasterised at the display's pixel density (Dear ImGui 1.92's dynamic atlas), so it is crisp on Retina and
at 125-200% Windows scaling; it is grayscale anti-aliased with sub-pixel glyph positioning. LCD (RGB) sub-pixel
anti-aliasing is not possible: the glyph atlas is a single coverage channel (and macOS dropped LCD smoothing in
Mojave). A missing or unreadable font falls back to Retro with a note in the Font section. Debug windows use
`fonts::mono()` / `fonts::pushMono()` (`src/fonts_imgui.h`), a monospace face whatever the UI font is. Glyphs the
chosen font lacks (arrows, star, check mark) come from a symbols font or the built-in vector face.
The typeface list shows every choice in its own typeface: the built-in ones, then **any font installed on the computer**
(the standard font folders are scanned on a worker thread the first time the list is shown, reading only each file's
name / OS/2 / post tables: `/System/Library/Fonts`, `/Library/Fonts`, `~/Library/Fonts` on macOS, `%WINDIR%\Fonts` and
`%LOCALAPPDATA%\Microsoft\Windows\Fonts` on Windows, `/usr/share/fonts`, `~/.local/share/fonts` and `$XDG_DATA_DIRS` on
Linux; `src/fonts_system.*`). A search box filters it, the star keeps a font in **Favorites**, and the last eight picks
are listed under **Recent**; they persist as `ui.font.fav.N` / `ui.font.recent.N`, the chosen OS font as `ui.font.os`
(`path|face`). The previews load lazily: only rows on screen ask for their face, a few per frame, at most 48 faces / 48 MB
stay loaded (least recently used go first), and a face that cannot draw its own name (no Latin letters, damaged, over 8 MB)
is shown in the UI font with the reason. **Debugger font** picks the monospace face of the debug windows from the
fixed-pitch fonts, each sample drawn in its own face (`ui.font.debug`; Automatic = the system monospace font).
`--font installed:/path/Font.ttf[|FACE]` selects an installed font for one run.
FreeType is the default renderer (FreeType License, fetched and pinned, built static) and provides the
**Hinting** (none / light / full) and **Smooth edges** choices. `-DVJ_APP_FREETYPE=OFF` builds with Dear ImGui's
stb_truetype instead, which hides those two choices; the Retro pixel font always uses stb_truetype.

Every window opens at a size in font heights (so it scales with DPI), at most 80% of the main
window, is constrained to 95% of it each frame (a size saved in `imgui.ini` by a bigger window never
overflows a smaller one) and is pulled back on screen. Long text wraps, tables stretch, controls take
a share of the width that is left (`src/ui_util.h`). To check a layout:

```sh
# window size comes from window.w / window.h in <data>/config.ini; --ui-scale 2 stands in for a HiDPI display
virtualjaguar --data-dir /tmp/d --ui-scale 2 --screenshot out.png --screenshot-frame 40 \
  --show-window settings-input game.j64
```

`--low-dpi` opens a 1x window even on a Retina display (screenshots of a standard-density screen).

`--show-window` takes `settings[-video|-input|-audio|-storage|-interface][:FILTER]`, `states`, `about`,
`cheats`, `library`, `login`, `achievements`, `keypad` and `theme-editor`.

## Layout

| file | job |
| --- | --- |
| `cmake/VJCore.cmake` | core + libchdr static libraries from `Makefile.common` |
| `cmake/VJSmokeTests.cmake` | CTest smoke tests |
| `src/core_host.*` | libretro host: environment calls, options, SRAM, states, discs, the core's private event channel |
| `src/typed_options.h`, `src/typed_ui.h` | which options take a typed value and their validators; the Custom... combo and box |
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
| `src/fonts.*`, `src/fonts_system.*` | UI fonts with no ImGui: `ui.font.*` settings, favorites / recents, per-OS system font lookup, installed-font enumeration, custom file validation + import, glyph ranges (unit tested) |
| `src/fonts_imgui.*`, `src/fonts_ui.cpp` | the live font atlas (typeface, size, FreeType flags, `mono()`, the lazy preview cache), and Settings > Interface > Font (font list, debugger font) |
| `src/platform/` | native-OS integration: menu model + dispatch (`commands.*`, no OS), per-OS files (`platform_macos.mm`, `platform_windows.cpp`, `platform_other.cpp`), tray (`tray.cpp`); `src/desktop.cpp` is the app side |
| `src/library.*` | game library logic: dat parser, CRC identification, folder scan + hash cache, libretro-thumbnails names |
| `src/library_lists.*` | favorites, recently played, display options, tab layout, save-state listing (pure) |
| `src/library_service.*` | scan / decode / download worker threads, art cache and texture handover |
| `src/library_ui.cpp` | the Game library: the home screen and the window |
| `src/ui_util.h` | layout helpers every window uses: sizes in em clamped to the main window, width fractions, wrapped rows |
| `src/http.*` | blocking HTTP(S) GET: libcurl, WinHTTP or nothing |
| `src/cheats.*` | `.cht` parsing, conversion to core codes, per-game cheat state (no SDL, unit tested) |
| `src/cheats_core.cpp`, `src/cheats_ui.cpp` | core glue (retro_cheat_*, title identity, CLI) and the Cheats window |
| `cmake/VJCheatData.cmake`, `cmake/VJCheatManifest.cmake`, `cmake/VJCheatTests.cmake` | pinned optional database download, cheat tests |
| `src/paths.*`, `src/config.*` | data folders (portable mode) and the settings file |
| `src/achievements.*` | RetroAchievements: rc_client glue, memory, hardcore policy, mock server for tests |
| `src/achievements_ui.cpp` | ImGui: sign-in, the dashboard window (this game / Jaguar games / profile), toasts, trackers |
| `src/achievements_games.cpp` | dashboard data: picture URLs, achievement rows, the games list and progress, library matching |
| `src/ra_dash.*` | dashboard logic with no ImGui or network: games parsing and matching, cache file, filters and sorts |
| `src/ra_images.*` | the avatar / icon / badge cache: disk files, bounded textures, offline mode |
| `src/ach_http.*` | libcurl / WinHTTP worker for rc_client's requests |
| `cmake/VJRcheevos.cmake`, `cmake/VJAchTests.cmake` | rcheevos fetch + static lib; headless achievement tests |

## Not supported yet

General rollback netplay for any game (#864); the Netplay window covers the link-cable
games only.
Microphone and netpacket are reported unsupported.
