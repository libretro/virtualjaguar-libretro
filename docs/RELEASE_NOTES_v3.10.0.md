# Virtual Jaguar v3.10.0

**Controller-friendly TV mode, a more responsive desktop app, multi-disc playlists,
and optional AI players.**

This is a feature release for the standalone app on macOS, Windows and Linux.
It also includes MMULT and TOM register-readback corrections from AtariAge reports.
Core options and v17 savestates carry over.
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

## Emulation corrections

- GPU and DSP **MMULT** preserve the advanced matrix-address counter and wrap
  coefficient reads within local RAM. Consecutive multiplies now render the supplied
  polygon demo as a filled solid instead of a line. Row/column stepping, address wrap
  and matrix-address savestate round trips have regression coverage.
  ([#1043](https://github.com/libretro/virtualjaguar-libretro/issues/1043))
- TOM PIT reads expose **live timer counters**. Unreadable TOM I/O registers fall back
  to MEMCON1, following the original Atari netlists. Timer reads use the existing
  scheduler and have scheduler-slice resolution; independent prescaler phase during
  partial reprogramming remains future work.
  ([#1044](https://github.com/libretro/virtualjaguar-libretro/issues/1044),
  [#1045](https://github.com/libretro/virtualjaguar-libretro/issues/1045))
- Expanded Z-buffer comparison tests cover both blitter engines, including a bootable
  greater-than test ROM. These cases already passed before this release; the separate
  reported GT defect remains unconfirmed and open.
  ([#1046](https://github.com/libretro/virtualjaguar-libretro/issues/1046))

## Jaguar CD and achievements

- Open **`.m3u` multi-disc playlists**; every disc appears under File > Disc.
  ([#1012](https://github.com/libretro/virtualjaguar-libretro/pull/1012))
- Invalid playlists no longer hide otherwise playable discs. States from every playlist disc
  remain available in the library and TV mode, including identical slot numbers on different
  discs. Resuming mounts the state-owning disc while retaining the playlist's full disc list
  and save-RAM ownership.
  ([#1042](https://github.com/libretro/virtualjaguar-libretro/pull/1042))
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

Combined candidate `344dc0bc` passed the core build, C89 lint and full core suite on
Mac Studio with the private corpus. Three checks skipped for missing optional
Fountain live-abort, CHSE-capable chdman and one-session Red Book fixtures/tools.
Host-sensitive network checks ran and passed. Explicit Skyhammer/Iron Soldier 2
clipping and Iron Soldier 1 presence checks also passed.

Fresh cartridge (155 titles, HLE/BIOS) and CD (28 title/mode rows) matrices show
**zero backward moves, zero added/removed rows and no new watchdog signatures**
against v3.9.1. Existing known failures remain recorded. The acid suite ran all
151 cases: 148 pass, three known failures, **zero regressions**, zero missing tests.
The new GT ROM also passes with the fast blitter.

The combined app passed all **159 CTest tests**, including 53 emulation-thread tests,
with zero failures or skips. Playlist tests cover invalid entries, per-disc state
ownership with identical slots, warm-cache lookup, TV resume routing, and actual
inline/threaded state-load events. Windows path expectations were corrected, and
all three previously failing Windows app jobs passed on that fix.

Independent review of the incoming core changes found no defects. Source-session
RetroArch checks confirmed the polygon demo's filled geometry, changing PIT counter
and matching MEMCON1 values. Earlier focused Studio windowed checks covered
inline/threaded operation, settings, fullscreen hover/F1/restore/persistence and
native-only chrome. POSIX installer hermetic tests passed 208 checks. Clean-machine
Windows qualification remains separate. Fresh GitHub CI and published-artifact
checks remain release gates; current status is in
[release tracker #1036](https://github.com/libretro/virtualjaguar-libretro/issues/1036).
