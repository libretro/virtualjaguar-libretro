# Virtual Jaguar v3.10.0

**Controller-friendly TV mode, a more responsive desktop app, multi-disc playlists,
and optional AI players.**

This is a feature release for the standalone app on macOS, Windows and Linux.
The emulation core is unchanged from v3.9.1: the core options and v17 savestates carry over.
Experimental emulation threading remains **off by default**.

## Play from the couch

- **TV mode** adds Home, Library and Game details screens and a paused in-game menu
  designed for a controller. Hold the configured TV-menu button to open it, or use
  Ctrl/Cmd+Shift+T to toggle TV mode. Initial setup still uses desktop mode.
  ([#995](https://github.com/libretro/virtualjaguar-libretro/pull/995),
  [#999](https://github.com/libretro/virtualjaguar-libretro/pull/999),
  [#1024](https://github.com/libretro/virtualjaguar-libretro/pull/1024))
- **Desktop library controller navigation**, with per-family and custom button profiles.
  ([#1030](https://github.com/libretro/virtualjaguar-libretro/pull/1030))
- **Jaguar Pro Controller support** exposes X, Y, Z, L and R as separate controls.
  ([#1019](https://github.com/libretro/virtualjaguar-libretro/pull/1019))
- **Fullscreen menu auto-hide** is optional. Detached tool windows use the native
  operating-system title bar, without duplicate in-app window controls.
  ([#1034](https://github.com/libretro/virtualjaguar-libretro/pull/1034))

## Desktop responsiveness

An optional emulation thread lets the UI keep responding while the core runs.
RetroAchievements evaluation and link play run on the core owner thread; long operations
show a busy indicator. Command ordering, achievements UI access and shutdown races are
covered by regression tests, including ThreadSanitizer tests in CI.
([#1000](https://github.com/libretro/virtualjaguar-libretro/pull/1000),
[#1031](https://github.com/libretro/virtualjaguar-libretro/pull/1031),
[#1033](https://github.com/libretro/virtualjaguar-libretro/pull/1033))

Presentation pacing and late input latching have more precise wake-ups, more deadline
headroom and slower safety-pad decay.
([#1018](https://github.com/libretro/virtualjaguar-libretro/pull/1018),
[#1023](https://github.com/libretro/virtualjaguar-libretro/pull/1023),
[#1035](https://github.com/libretro/virtualjaguar-libretro/pull/1035))

The setting is under **Settings > Interface > Performance > Emulation thread** and takes
effect after restarting. Keep it off to use the Tools/debugger windows or GDB: those features
still need to move across the thread boundary. Rare host scheduling stalls and display-rate
pacing remain follow-up work; this release does not claim to eliminate every repeated frame.

## Jaguar CD and achievements

- Open **`.m3u` multi-disc playlists**; every disc appears under File > Disc.
  ([#1012](https://github.com/libretro/virtualjaguar-libretro/pull/1012))
- The disc menu works after loading a CD and while booted into the CD BIOS without a disc.
  Swaps update the running content identity while keeping save ownership on the disc that
  opened the session.
  ([#1006](https://github.com/libretro/virtualjaguar-libretro/pull/1006),
  [#1009](https://github.com/libretro/virtualjaguar-libretro/pull/1009),
  [#1011](https://github.com/libretro/virtualjaguar-libretro/pull/1011))
- The achievements browser omits games with no achievement set.
  ([#1015](https://github.com/libretro/virtualjaguar-libretro/pull/1015))
- Missing CD mirror artwork is remembered only for the current session, so later launches
  can retry. The website also offers the CD artwork collection as one ZIP.
  ([#998](https://github.com/libretro/virtualjaguar-libretro/pull/998),
  [#1016](https://github.com/libretro/virtualjaguar-libretro/pull/1016))

## AI players

**Settings > Neuro** connects to a server implementing the Neuro game API, allowing an AI
backend to control Player 1. Tempest 2000 has a built-in profile; custom profiles can define
other games' moves. This is opt-in and requires a separate server. Disconnects release held
inputs, stopping is bounded, and hardcore achievements block AI input. See
[the Neuro guide](https://github.com/libretro/virtualjaguar-libretro/blob/v3.10.0/docs/neuro.md)
for configuration and privacy details.
([#1001](https://github.com/libretro/virtualjaguar-libretro/pull/1001),
[#1027](https://github.com/libretro/virtualjaguar-libretro/pull/1027))

## Known limitations and the road to 4.0

- Emulation threading is experimental, opt-in, and disables the Tools/debugger windows and GDB.
- TV mode settings, overlays, achievements screens and automatic switching remain in progress.
- Doom's attract-demo timing and the other emulation limitations documented for v3.9.1 remain.
- Windows signing depends on the signing service's availability and approval; do not assume
  every Windows package is signed.

The 4.0 milestone now tracks a fully integrated app: debugging while threaded, validated
60/120 Hz display pacing, finished TV mode, and dependable installation/update flows.
The hardware timing campaign and dual-Jaguar rollback remain separate future projects.

## Downloads and upgrading

Install the app over your existing version; settings carry over. RetroArch and Provenance
users can replace the core without changing options or v17 savestates.

Download the app, libretro cores and Jaguar CD tools from
[the v3.10.0 release](https://github.com/libretro/virtualjaguar-libretro/releases/tag/v3.10.0).
Use the release's `SHA256SUMS.txt` to verify downloads. macOS packages are universal;
Windows has portable and installer packages; Linux has AppImage and Debian packages.

## Validation

The candidate at `7e84cf22` passed the full core suite and C89 lint on Mac Studio,
with the private corpus present. Fresh cartridge (155 titles, HLE/BIOS) and CD
(28 title/mode rows) matrices show **zero backward moves, zero added/removed rows and no new
crash signatures** against v3.9.1. Existing known failures remain recorded in the matrices.
The acid suite ran all 150 cases: 147 pass, three known failures, **zero regressions**.

The app built and passed CTest on Studio (159 registered tests; network discovery skipped),
including the 53 emulation-thread tests. Focused windowed checks passed for inline/threaded
operation, paused/settings access and fullscreen menu hover/F1/restore/persistence. POSIX
installer hermetic tests passed 208 checks. These do not replace clean-machine Windows
installer qualification. GitHub release CI and published-artifact checks remain release gates;
current status is recorded in [release tracker #1036](https://github.com/libretro/virtualjaguar-libretro/issues/1036).
