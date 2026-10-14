# Virtual Jaguar libretro v3.8.1

**RISC overclocking no longer costs more than it should, and the Virtual Jaguar desktop app
ships: a standalone emulator for macOS, Windows and Linux built on the same core.**

v3.8.1 is a fix release for the core plus the debut of the standalone app (https://github.com/libretro/virtualjaguar-libretro/issues/833). For
RetroArch and Provenance users the change is the overclock fix, an experimental DSP timing
model behind an existing option, and measurement tooling. With default settings the core is
byte-identical to v3.8.0 in the cartridge and CD boot matrices. No option keys were removed, no
listed option values changed, and the savestate format stays v17.

---

## Core

- **RISC overclock (GPU/DSP clock 0.5x, 1.5x, 2x) keeps the idle-loop fast-forward.** Any
  non-stock RISC clock used to switch the idle-loop fast-forward off, so games that spend most
  of each frame in a wait loop emulated twice the cycles, all of them spinning, and a 2x
  overclock ran far slower than it should. The fast-forward is exact at any clock scale, so it
  now stays on (https://github.com/libretro/virtualjaguar-libretro/pull/891, https://github.com/libretro/virtualjaguar-libretro/issues/569, https://github.com/libretro/virtualjaguar-libretro/issues/378). Headless harness on an M4 Max, core fps at 2x:

  | Title | 2x before | 2x after | 1x |
  | --- | --- | --- | --- |
  | Doom | 52 | 82 | 135 |
  | Alien vs Predator | 71 | 204 | 240 |
  | Tempest 2000 | 77 | 209 | 225 |

  Video, audio and savestates are byte-identical with the fast-forward on and off at 0.5x, 1.5x
  and 2x on 12 titles in HLE and 7 on the real BIOS; stock 1x is unchanged. The Settings
  sublabel and the `[perf]` log line no longer name the RISC clock as a suppressor. Doom's GPU
  loops are still not fast-forwarded, so Doom at 2x still takes about 1.65x the time of 1x.
- **Timing work, part of the accuracy plan (https://github.com/libretro/virtualjaguar-libretro/pull/908, https://github.com/libretro/virtualjaguar-libretro/issues/408, https://github.com/libretro/virtualjaguar-libretro/issues/313).** Two pieces are
  measurement tooling with no behaviour change: a Doom pace gate, a GPU busy-time counter and a
  Checkered Flag canary (S0), and a bus-traffic census with a grant-latency function (S2). The
  third (S1) is a DSP pipeline scoreboard plus a 16-bit external-bus timing model. It rides on
  the existing **GPU/DSP Pipeline Timing (Experimental)** option (formerly "GPU Pipeline
  Timing"), which stays **off by default**. **Anyone who already had that option on now also
  gets the DSP model**, and like the GPU model it turns the idle-loop fast-forward off. It does
  not improve pacing yet: with it on, Checkered Flag runs 4.9 fields per game frame against its
  3.0 cap.
- **Clock scale options also accept a percentage; GDB halt timeout clamped.** `175` or `175%`
  (clamped to 25-400) is read in addition to the listed values, which map exactly as before;
  the GDB halt timeout is clamped to 3600 seconds (https://github.com/libretro/virtualjaguar-libretro/pull/906).
- **A private frontend channel and a debug interface that RetroArch never sees.** The core can
  tell the desktop app about link up/down, link statistics, wire speed and crash-watchdog hits
  (https://github.com/libretro/virtualjaguar-libretro/pull/906, `docs/app-frontend-extension.md`), pick a BIOS file and boot a BIOS on its own
  (https://github.com/libretro/virtualjaguar-libretro/pull/910), and expose memory, registers, breakpoints and the display list to the app's
  debug tools (https://github.com/libretro/virtualjaguar-libretro/pull/919, `docs/app-debug-tools.md`). All of it is opt-in by a handshake only
  the app answers; under RetroArch and Provenance it is a no-op.

---

## The desktop app

A front end of its own in `app/`: SDL3 for the window, audio and input, Dear ImGui for the
interface, OpenGL 3.3 for presentation. It drives the core through the same `retro_*` API
that RetroArch uses, so core options and save states behave the same. It carries the core's
version number and ships with each release. It was built in https://github.com/libretro/virtualjaguar-libretro/pull/867; docs and site in
https://github.com/libretro/virtualjaguar-libretro/pull/896. Screenshots: https://jaguar.provenance-emu.com/app.html

### Library and artwork

- **Game library on the home screen.** Your games in a grid under Cartridges, Jaguar CD and
  BIOS & system tabs, identified by CRC32 against the libretro database (No-Intro, Redump), with
  box art from libretro-thumbnails downloaded on demand and cached (https://github.com/libretro/virtualjaguar-libretro/issues/838, https://github.com/libretro/virtualjaguar-libretro/pull/881,
  https://github.com/libretro/virtualjaguar-libretro/pull/889). Ctrl/Cmd + scroll (or a trackpad pinch on macOS) resizes the tiles, and BIOS
  files anywhere under a library folder show in the BIOS tab (https://github.com/libretro/virtualjaguar-libretro/pull/899).
- **Favorites, recently played and save states.** Star a game (on its tile or right-click > Add
  to Favorites); the games you boot are kept under Recently played. Each shows as its own tab, as
  a strip at the top of the Cartridges and Jaguar CD tabs, both, or not at all (Favorites &
  recents...). A Save states tab lists every game with states and the screenshot saved with each;
  click one to start the game from it. Right-click a game for Play, Load state, favorites, Show in
  Finder / File Explorer and Copy path (https://github.com/libretro/virtualjaguar-libretro/pull/915).
- **Covers for Jaguar CD.** libretro-thumbnails has no Jaguar CD art, so discs get a readable
  title card, and you can set your own cover: right-click a tile > Set cover image (PNG), drop a
  PNG on a tile, or put `<Title>.png` files in the app's `covers` folder (https://github.com/libretro/virtualjaguar-libretro/pull/901). Until
  libretro-thumbnails has a Jaguar CD set, discs fall back to box art, title screens and snaps
  hosted on the project site (https://jaguar.provenance-emu.com/thumbnails/), asked only after
  libretro-thumbnails misses (https://github.com/libretro/virtualjaguar-libretro/pull/907).
- **Better disc identification.** `.chd` discs are checksummed from their first track (like
  `.cue`), `.cdi` best-effort, and a disc whose checksum is not Redump's (a non-Redump bin/cue or
  `.cdi`) is matched by file or folder name when only one game fits. Hover a tile to see how it
  was identified (https://github.com/libretro/virtualjaguar-libretro/pull/901). CHD hashing is verified against synthetic images only; checking
  real Redump CHDs is still to do.

### BIOS

- **BIOS choice and boot (Settings > BIOS, BIOS & system tab).** Pick the exact boot ROM or CD
  BIOS file the core runs (any name, any folder; Auto stays the default), switch cartridges and CD
  games between HLE and the real BIOS without loading anything first, and **Boot** a BIOS on its
  own: the CD BIOS with no disc (its disc screen, Memory Track plugged in) or the cartridge boot
  ROM with an empty slot. A BIOS dropped on the window is imported, not run as a cartridge;
  `JAGCD.BIN`-style upper-case names are treated as the core's search sees them on macOS and
  Windows. One click copies a BIOS into the system folder under the name the core reads
  (https://github.com/libretro/virtualjaguar-libretro/pull/910, https://github.com/libretro/virtualjaguar-libretro/pull/899, https://github.com/libretro/virtualjaguar-libretro/issues/838).

### Playing

- **Controls.** Per-device and per-game remapping, all 12 keypad keys on a gamepad, keyboard play
  with the Pro Controller 6-button layout, an on-screen keypad (F2) and a controls overlay (F3)
  that appears at boot when no player-1 controller is connected. Mice, the Tempest 2000 rotary
  controller, analog, driving, paddle, 6D, light gun and Team Tap are chosen under Settings >
  Input > Devices & players (https://github.com/libretro/virtualjaguar-libretro/issues/837, https://github.com/libretro/virtualjaguar-libretro/pull/879, https://github.com/libretro/virtualjaguar-libretro/pull/887).
- **Save states and resume.** Save states with thumbnails (F5 save, F7 load, F6 slot) and a state
  manager. Settings > Storage > Resume where I left off (Off / Ask / Always, default Ask): closing
  a game, switching games or quitting keeps an Auto-save of it (shown in the Save states tab and
  the tile menu), and the next boot offers to resume from it or does so at once. Not in
  RetroAchievements hardcore or netplay, and not for a game that ran under 5 seconds or crashed
  (https://github.com/libretro/virtualjaguar-libretro/pull/915).
- **Cheats.** The libretro cheat database for the running game is bundled, and you can enter
  codes by hand (https://github.com/libretro/virtualjaguar-libretro/issues/835, https://github.com/libretro/virtualjaguar-libretro/pull/878).
- **Shaders.** RetroArch `.slangp` presets through librashader, with CRT, scanline and
  sharp-bilinear presets from libretro's slang-shaders bundled, plus integer scaling
  (https://github.com/libretro/virtualjaguar-libretro/issues/834, https://github.com/libretro/virtualjaguar-libretro/pull/880). Builds from source fetch the librashader runtime on macOS and Windows
  by default (https://github.com/libretro/virtualjaguar-libretro/pull/897).
- **Several windows at once.** File > New window (Ctrl/Cmd+N), or `open -n` on macOS, starts
  another copy for netplay tests on one machine or local multiplayer. Extra copies keep their game
  saves in their own folder and never rewrite the shared settings, and each can be pinned to one
  controller (Settings > Input) (https://github.com/libretro/virtualjaguar-libretro/pull/902).
- **Netplay for the link-cable games.** A Netplay window hosts or joins Doom, BattleSphere,
  AirCars and Ultra Vortek (over the Voice Modem) on a local network, with LAN discovery
  (https://github.com/libretro/virtualjaguar-libretro/pull/888). While a session is set up, the link settings it controls are greyed out in
  Settings (https://github.com/libretro/virtualjaguar-libretro/pull/898).
- **Typed settings.** Options that take a number or an address (link port and host, GDB port and
  halt timeout, mouse and rotary sensitivity, the dead zone / offset / response-curve tuning, the
  clock scales) show the core's presets plus a "Custom..." entry that opens a validated box; an
  invalid entry says why and changes nothing. The Netplay window takes any port 1-65535 and a
  typed host name or IPv4 address directly (https://github.com/libretro/virtualjaguar-libretro/pull/906).
- **Reset buttons.** Each Settings tab and each core option category has a Reset group button,
  and Reset all settings... (with a confirmation) puts everything back to defaults while keeping
  your library folders, recents, covers, saves, RetroAchievements login, key and button bindings
  and per-game overrides unless you tick them (https://github.com/libretro/virtualjaguar-libretro/pull/911).
- **Performance.** Release builds compile the core and the app at -O3, and the shader chain no
  longer re-runs every pass when the emulated frame has not changed (paused, or on a display
  faster than the game) (https://github.com/libretro/virtualjaguar-libretro/pull/894).

### Look and feel

- **Light and dark themes.** Settings > Video > Appearance picks System (the default), Dark or
  Light; System follows the operating system and switches live. The in-game overlays (controls
  sheet, on-screen messages, achievement toasts, frame rate) stay dark and translucent in both
  themes so they read on any game picture. **Customize...** opens a theme editor: start from Dark
  or Light, change the main colours, the rounding and the spacing with a live preview and a
  warning when text drops below 4.5:1 contrast, then save it as a named theme (a small `.ini` in
  the app's `themes` folder), duplicate, delete, export or import it (https://github.com/libretro/virtualjaguar-libretro/pull/920). Window
  backgrounds are opaque, so text no longer shows through About and Settings (https://github.com/libretro/virtualjaguar-libretro/pull/904).
- **Choose the interface font.** Settings > Video > Font: Retro (the pixel font, still the
  default), System UI (San Francisco, Segoe UI, the desktop's sans-serif), a system monospace
  face, or a font file of your own, at 11-24 pt, applied live. Text is drawn at the display's
  full pixel density, so it is crisp on Retina and 125-200% Windows scaling, and rendered with FreeType
  (statically linked, nothing to install) which adds Hinting (none / light / full) and Smooth edges choices
  (https://github.com/libretro/virtualjaguar-libretro/pull/925).
- **Feels native** (https://github.com/libretro/virtualjaguar-libretro/pull/912):
  - macOS: a real menu bar (Cmd shortcuts, Open Recent, State Slot, Window and Help menus) and a
    Dock menu with your recent games.
  - Windows: a dark title bar, recent games in the taskbar Jump List and an optional installer
    checkbox that opens `.j64` / `.jag` / `.cue` / `.chd` and the rest with Virtual Jaguar.
  - Linux: a proper MIME type and desktop actions, and the window now matches its `.desktop` file.
  - All three: double-clicking a game, Open With and dropping a game on the app icon work; the
    screensaver no longer stays blocked while you are in the library or paused; an optional tray /
    menu-bar icon offers pause, reset and recent games; "Show in Finder / Explorer" is in the menu.

### RetroAchievements

- **Achievements.** Softcore by default. Hardcore is optional; it refuses loading save states and
  disables cheats (https://github.com/libretro/virtualjaguar-libretro/issues/836, https://github.com/libretro/virtualjaguar-libretro/pull/882). The Achievements window shows your avatar, the game
  icon and every achievement's badge (also in the unlock toasts), filters and sorts the list, and
  has a Jaguar games tab listing every Jaguar and Jaguar CD game with achievements, your progress
  in each and a Play button for the ones in your library (https://github.com/libretro/virtualjaguar-libretro/pull/917).
- **The login token is kept in the system keychain.** It used to sit in plain text in
  `config.ini`. It now lives in the macOS Keychain, the Windows Credential Manager or the Linux
  Secret Service (libsecret, loaded at run time, so nothing new is required); a token already in
  `config.ini` is moved there the first time you start this version, and Sign out erases it.
  Where no keyring is available the token stays in `config.ini` as before and Settings says so.
  Your password was never stored (https://github.com/libretro/virtualjaguar-libretro/pull/923).

### Discord

- **Rich Presence (Settings > Discord).** Shows the game you are playing, its box art, how long,
  and RetroAchievements progress / hardcore / netplay mode on your Discord profile, with a **Get
  Virtual Jaguar** button (and the game's RetroAchievements page) for other people to find the
  app. On by default, needs Discord running locally; the app speaks Discord's local protocol
  itself (no SDK), connects in the background and never waits on Discord, and only the first
  window publishes (https://github.com/libretro/virtualjaguar-libretro/pull/922).

### Debug tools (Tools menu)

- A memory viewer / editor and a palette (CLUT) editor (https://github.com/libretro/virtualjaguar-libretro/pull/913), a cheat scanner whose finds
  become ordinary cheats (https://github.com/libretro/virtualjaguar-libretro/pull/914), an Object Processor list (the whole display list decoded,
  with a bitmap preview, an on-screen outline, PNG export and a pixel painter) (https://github.com/libretro/virtualjaguar-libretro/pull/918), a
  blitter log, a texture browser (export a tile, replace it with your own PNG) and an audio scope
  (https://github.com/libretro/virtualjaguar-libretro/pull/916), and a CPU debugger with 68000, GPU and DSP tabs: registers, disassembly,
  breakpoints, step, step over and run to cursor (https://github.com/libretro/virtualjaguar-libretro/pull/924). F9 breaks into the game and F10
  steps (https://github.com/libretro/virtualjaguar-libretro/pull/919, https://github.com/libretro/virtualjaguar-libretro/issues/652, https://github.com/libretro/virtualjaguar-libretro/issues/833).
- Writes are limited to RAM unless you tick "Allow register writes", and the CD registers plus
  `B_CMD`, `G_CTRL`, `D_CTRL`, `OLP`, `LTXD` and `RTXD` are never written. Everything is off under
  RetroAchievements hardcore, and RetroArch is unaffected (`docs/app-debug-tools.md`).

### Packaging and signing

- **Downloads.** Attached to this release; Windows ARM64 and the Flatpak are new in this release
  (https://github.com/libretro/virtualjaguar-libretro/pull/903):

  | System | Package |
  | --- | --- |
  | macOS 11+, Apple Silicon and Intel | `VirtualJaguar-3.8.1-macos-universal.zip` |
  | Windows x64 | `VirtualJaguar-3.8.1-windows-x64.zip` (portable) or `VirtualJaguar-3.8.1-windows-x64-setup.exe` (installer) |
  | Windows ARM64 | `VirtualJaguar-3.8.1-windows-arm64.zip` (portable) or `VirtualJaguar-3.8.1-windows-arm64-setup.exe` (installer) |
  | Linux x86_64, aarch64 | `VirtualJaguar-3.8.1-linux-<arch>.AppImage` or `virtualjaguar_3.8.1_<amd64\|arm64>.deb` |
  | Linux x86_64 (Flatpak) | `VirtualJaguar-3.8.1-linux-x86_64.flatpak` (`flatpak install --user` it; the runtime comes from Flathub) |

  Nightly app builds are linked from the pinned builds issue and are rebuilt from `develop` on
  every push (https://github.com/libretro/virtualjaguar-libretro/issues/839, https://github.com/libretro/virtualjaguar-libretro/issues/840, https://github.com/libretro/virtualjaguar-libretro/pull/884, https://github.com/libretro/virtualjaguar-libretro/pull/883, https://github.com/libretro/virtualjaguar-libretro/pull/886).
- **Sign your own build.** `app/packaging/local-package.sh --sign` packages your build and signs
  it with a Developer ID (or Apple Development) identity found in the macOS keychain, or a `.pfx`
  / store certificate on Windows, optionally set in a gitignored `.env`; `--notarize` also
  notarizes on macOS (https://github.com/libretro/virtualjaguar-libretro/pull/905). The macOS dev executable is signed by default outside CI
  (https://github.com/libretro/virtualjaguar-libretro/pull/909).

---

## Known issues

- **Signing.** macOS packages are signed with a Developer ID and notarized; Windows packages are
  not signed yet (SmartScreen may warn). <!-- SIGNING: verify macOS notarization in the release CI before tagging -->
- **Shaders on Intel Macs.** The macOS universal package bundles an arm64-only librashader, so
  shaders may be unavailable on Intel Macs.
- **Shaders on Linux** need librashader installed separately (a distribution package, or built
  from source). The Flatpak does not include librashader yet, so it has no shaders.
- **The MSVC Windows build is experimental.** The released Windows packages are built with MinGW
  (the ARM64 ones with MinGW's clang).
- **RetroAchievements** identifies cartridges and `.cue` discs, but not `.chd` or `.cdi` discs
  yet.
- **Netplay is link-cable play on a local network only.** It is not rollback netplay, and
  Internet or VPN play is not supported. General rollback netplay is tracked in https://github.com/libretro/virtualjaguar-libretro/issues/864.
- **Double-clicking a game while the app is running opens a second instance** on Windows and
  Linux. On macOS a double-click activates the running copy; use `open -n` for another.
- **CHD identification** in the library is verified on synthetic images only.
- **GPU/DSP Pipeline Timing (Experimental)** does not improve pacing yet (see Core).

---

## Upgrading

- **RetroArch / Provenance:** drop-in. The core reports v3.8.1; options and savestates (v17)
  carry over. If you had GPU Pipeline Timing on, it now also applies the DSP model.
- **App:** unzip or install the package for your system. The app does not include BIOS
  images or games.
