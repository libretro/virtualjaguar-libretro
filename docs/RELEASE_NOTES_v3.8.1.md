# Virtual Jaguar libretro v3.8.1

**RISC overclocking no longer costs more than it should, and the Virtual Jaguar desktop app
ships: a standalone emulator for macOS, Windows and Linux built on the same core.**

v3.8.1 is a bug-fix release for the core plus the debut of the standalone app
(https://github.com/libretro/virtualjaguar-libretro/issues/833). For RetroArch and Provenance users the change is the overclock fix
below, plus two small additions for the app (a clock-scale percentage form and a private event
channel that RetroArch never sees). No option keys or listed values changed and the savestate format
stays v17.

---

## Core fix

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
- **Clock scale options also accept a percentage; GDB halt timeout clamped.** `175` or `175%`
  (clamped to 25-400) is read in addition to the listed values, which map exactly as before;
  the GDB halt timeout is clamped to 3600 seconds.

---

## The desktop app

A front end of its own in `app/`: SDL3 for the window, audio and input, Dear ImGui for the
interface, OpenGL 3.3 for presentation. It drives the core through the same `retro_*` API
that RetroArch uses, so core options and save states behave the same. It carries the core's version number and ships with each release.
Screenshots: https://jaguar.provenance-emu.com/app.html

- **Game library on the home screen.** Your games in a grid under Cartridges, Jaguar CD
  and BIOS & system tabs, identified by CRC32 against the libretro database (No-Intro,
  Redump), with box art from libretro-thumbnails downloaded on demand and cached
  (https://github.com/libretro/virtualjaguar-libretro/issues/838, https://github.com/libretro/virtualjaguar-libretro/pull/881, https://github.com/libretro/virtualjaguar-libretro/pull/889). BIOS files anywhere under a library folder show in the BIOS tab,
  and one click copies a BIOS into the system folder under the name the core reads; Ctrl/Cmd
  + scroll (or a trackpad pinch on macOS) resizes the tiles (https://github.com/libretro/virtualjaguar-libretro/pull/899).
- **Favorites, recently played and save states in the library.** Star a game (on its tile or
  right-click > Add to Favorites); the games you boot are kept under Recently played. Each shows as
  its own tab, as a strip at the top of the Cartridges and Jaguar CD tabs, both, or not at all
  (your choice under Favorites & recents...). A new Save states tab lists every game with
  states and the screenshot saved with each; click one to start the game from it. Right-click a
  game for Play, Load state, favorites, Show in Finder / File Explorer and Copy path
  (https://github.com/libretro/virtualjaguar-libretro/issues/838).
- **Resume where I left off.** Settings > Storage > Resume where I left off (Off / Ask / Always, default
  Ask): closing a game, switching games or quitting keeps an Auto-save of it (shown in the Save
  states tab and the tile menu), and the next boot offers to resume from it or does so at once.
  Not in RetroAchievements hardcore or netplay, and not for a game that ran under 5 seconds or
  crashed (https://github.com/libretro/virtualjaguar-libretro/issues/833).
- **Covers for Jaguar CD.** libretro-thumbnails has no Jaguar CD art, so discs now get a
  readable title card, and you can set your own cover: right-click a tile > Set cover image
  (PNG), drop a PNG on a tile, or put `<Title>.png` files in the app's `covers` folder.
  Until libretro-thumbnails has a Jaguar CD set, discs fall back to box art, title screens and snaps hosted on the project
  site (https://jaguar.provenance-emu.com/thumbnails/), asked only after libretro-thumbnails misses.
- **Better disc identification in the library.** `.chd` discs are now checksummed from
  their first track (like `.cue`), `.cdi` best-effort, and a disc whose checksum is not
  Redump's (a non-Redump bin/cue or `.cdi`) is matched by file or folder name when only
  one game fits. Hover a tile to see how it was identified. (CHD hashing is verified
  against synthetic images; checking real Redump CHDs is still to do.)
- **Shaders.** RetroArch `.slangp` presets through librashader, with CRT, scanline and
  sharp-bilinear presets from libretro's slang-shaders bundled, plus integer scaling
  (https://github.com/libretro/virtualjaguar-libretro/issues/834, https://github.com/libretro/virtualjaguar-libretro/pull/880). Builds from source fetch the librashader runtime on macOS and Windows by
  default (https://github.com/libretro/virtualjaguar-libretro/pull/897).
- **Cheats.** The libretro cheat database for the running game is bundled, and you can
  enter codes by hand (https://github.com/libretro/virtualjaguar-libretro/issues/835, https://github.com/libretro/virtualjaguar-libretro/pull/878).
- **RetroAchievements.** Softcore by default. Hardcore is optional; it refuses loading save
  states and disables cheats (https://github.com/libretro/virtualjaguar-libretro/issues/836, https://github.com/libretro/virtualjaguar-libretro/pull/882).
  The Achievements window now shows your avatar, the game icon and every achievement's badge
  (also in the unlock toasts), filters and sorts the list, and has a Jaguar games tab listing every
  Jaguar and Jaguar CD game with achievements, your progress in each and a Play button for the
  ones in your library.
- **Controls.** Per-device and per-game remapping, all 12 keypad keys on a gamepad,
  keyboard play with the Pro Controller 6-button layout, an on-screen keypad (F2) and a
  controls overlay (F3) that appears at boot when no player-1 controller is connected.
  Mice, the Tempest 2000 rotary controller, analog, driving, paddle, 6D, light gun and
  Team Tap are chosen under Settings > Input > Devices & players
  (https://github.com/libretro/virtualjaguar-libretro/issues/837, https://github.com/libretro/virtualjaguar-libretro/pull/879, https://github.com/libretro/virtualjaguar-libretro/pull/887).
- **Several windows at once.** File > New window (Ctrl/Cmd+N), or `open -n` on macOS, starts
  another copy for netplay tests on one machine or local multiplayer. Extra copies keep their
  game saves in their own folder and never rewrite the shared settings, and each can be pinned to
  one controller (Settings > Input) (https://github.com/libretro/virtualjaguar-libretro/issues/833).
- **Save states** with thumbnails (F5 save, F7 load, F6 slot) and a state manager.
- **Netplay for the link-cable games.** A Netplay window hosts or joins Doom, BattleSphere,
  AirCars and Ultra Vortek (over the Voice Modem) on a local network, with LAN discovery
  (https://github.com/libretro/virtualjaguar-libretro/pull/888). While a session is set up, the link settings it controls are shown greyed out in
  Settings (https://github.com/libretro/virtualjaguar-libretro/pull/898).
- **Typed settings.** Options that take a number or an address (link port and host, GDB port and
  halt timeout, mouse and rotary sensitivity, the dead zone / offset / response-curve tuning, the
  clock scales) show the core's presets plus a "Custom..." entry that opens a validated box; an
  invalid entry says why and changes nothing. The Netplay window takes any port 1-65535 and a typed
  host name or IPv4 address directly (https://github.com/libretro/virtualjaguar-libretro/issues/833).
- **Reset buttons.** Each Settings tab and each core option category has a Reset group button, and
  Reset all settings... (with a confirmation) puts everything back to defaults while keeping your library
  folders, recents, covers, saves, RetroAchievements login, key and button bindings and per-game
  overrides unless you tick them (https://github.com/libretro/virtualjaguar-libretro/issues/833).
- **Core events for the app.** The core tells the app about link up/down, link statistics, wire
  speed and crash-watchdog hits over a private channel (`docs/app-frontend-extension.md`) instead
  of the app reading log text; a watchdog hit now also shows on screen. RetroArch and other
  frontends never see it (https://github.com/libretro/virtualjaguar-libretro/issues/888).
- **BIOS choice and boot (Settings > BIOS, BIOS & system tab).** Pick the exact boot ROM or CD BIOS file the
  core runs (any name, any folder; Auto stays the default), switch cartridges and CD games between HLE and the
  real BIOS without loading anything first, and **Boot** a BIOS on its own: the CD BIOS with no disc (its disc
  screen, Memory Track plugged in) or the cartridge boot ROM with an empty slot. A BIOS dropped on the window
  is imported, not run as a cartridge; `JAGCD.BIN`-style upper-case names are treated as the core's search sees
  them on macOS and Windows. Needs a small private core call (`docs/app-frontend-extension.md`) that RetroArch
  never answers, so RetroArch is unchanged (#838, #833).
- **Discord Rich Presence (Settings > Discord).** Shows the game you are playing, its box art, how long, and
  RetroAchievements progress / hardcore / netplay mode on your Discord profile, with a **Get Virtual Jaguar**
  button (and the game's RetroAchievements page) for other people to find the app. On by default, needs Discord
  running locally; the app speaks Discord's local protocol itself (no SDK), connects in the background and never
  waits on Discord, and only the first window publishes
  (https://github.com/libretro/virtualjaguar-libretro/issues/833).
- **Local signing.** `app/packaging/local-package.sh --sign` packages your own build and signs it
  with a Developer ID (or Apple Development) identity found in the macOS keychain, or a `.pfx` /
  store certificate on Windows, optionally set in a gitignored `.env`; `--notarize` also notarizes
  on macOS (https://github.com/libretro/virtualjaguar-libretro/issues/839).
- **Performance.** Release builds compile the core and the app at -O3, and the shader chain
  no longer re-runs every pass when the emulated frame has not changed (paused, or on a
  display faster than the game) (https://github.com/libretro/virtualjaguar-libretro/pull/894).

### Downloads

Attached to this release:

| System | Package |
| --- | --- |
| macOS 11+, Apple Silicon and Intel | `VirtualJaguar-3.8.1-macos-universal.zip` |
| Windows x64 | `VirtualJaguar-3.8.1-windows-x64.zip` (portable) or `VirtualJaguar-3.8.1-windows-x64-setup.exe` (installer) |
| Windows ARM64 | `VirtualJaguar-3.8.1-windows-arm64.zip` (portable) or `VirtualJaguar-3.8.1-windows-arm64-setup.exe` (installer) |
| Linux x86_64, aarch64 | `VirtualJaguar-3.8.1-linux-<arch>.AppImage` or `virtualjaguar_3.8.1_<amd64\|arm64>.deb` |
| Linux x86_64 (Flatpak) | `VirtualJaguar-3.8.1-linux-x86_64.flatpak` (`flatpak install --user` it; the runtime comes from Flathub) |

Nightly app builds are linked from the pinned builds issue and are rebuilt from `develop`
on every push (https://github.com/libretro/virtualjaguar-libretro/issues/839, https://github.com/libretro/virtualjaguar-libretro/issues/840, https://github.com/libretro/virtualjaguar-libretro/pull/884, https://github.com/libretro/virtualjaguar-libretro/pull/883, https://github.com/libretro/virtualjaguar-libretro/pull/886).

---

## Known issues

- **The packages are not signed yet.** On macOS, right-click the app and choose Open the
  first time. On Windows, SmartScreen may warn about the installer.
- **Shaders on Intel Macs.** The macOS universal package bundles an arm64-only librashader,
  so shaders may be unavailable on Intel Macs.
- **Shaders on Linux** need librashader installed separately (a distribution package, or
  built from source). The Flatpak does not include librashader yet, so it has no shaders.
- **The MSVC Windows build is experimental.** The released Windows packages are built with
  MinGW (the ARM64 ones with MinGW's clang).
- **RetroAchievements** identifies cartridges and `.cue` discs, but not `.chd` or `.cdi`
  discs yet.
- **Netplay is link-cable play on a local network only.** It is not rollback netplay, and
  Internet or VPN play is not supported. General rollback netplay is tracked in
  https://github.com/libretro/virtualjaguar-libretro/issues/864.

---

## Upgrading

- **RetroArch / Provenance:** drop-in. The core reports v3.8.1; options and savestates (v17)
  carry over.
- **App:** unzip or install the package for your system. The app does not include BIOS
  images or games.
