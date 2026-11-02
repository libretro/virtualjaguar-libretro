# Virtual Jaguar v3.11.0

**The emulation thread is on by default, with "Match display" pacing, community cheats in
the app, a finished TV mode, faster GPU and DSP emulation, and AvP's marines fight back.**

This is a feature release for the standalone app on macOS, Windows and Linux. The libretro core
gains the AvP and GPU/DSP changes below. Core options and v17 savestates carry over.

## The emulation thread is on by default

The app now runs the emulation on its own thread in every build, so the UI, menus and tool windows
no longer slow the game down.
([#1134](https://github.com/libretro/virtualjaguar-libretro/pull/1134))

- **Tools and the debugger work with the thread on.** This covers the Tools windows, debugger
  halts, single-stepping and the GDB stub. Detach, Reset, Open and Close release a halt that GDB
  holds.
  ([#1061](https://github.com/libretro/virtualjaguar-libretro/pull/1061),
  [#1068](https://github.com/libretro/virtualjaguar-libretro/pull/1068),
  [#1090](https://github.com/libretro/virtualjaguar-libretro/pull/1090))
- **Neuro AI players** run on the emulation thread too.
  ([#1066](https://github.com/libretro/virtualjaguar-libretro/pull/1066))
- **Settings > Video > Frame pacing** offers two modes:
  - **Jaguar rate**, the default, runs the game at the console's own rate.
  - **Match display** starts every frame on a display refresh, and the audio follows the display.
    The game runs at the display's rate (on a 60 Hz screen, 0.1% slower than a Jaguar) with no
    repeated or skipped images. Where the display can't be locked, the status line says why
    and Jaguar rate is used.
  ([#1125](https://github.com/libretro/virtualjaguar-libretro/pull/1125),
  [#1128](https://github.com/libretro/virtualjaguar-libretro/pull/1128))
- **The emulation thread asks for real-time scheduling**, so frames start on time on a busy
  machine. macOS uses a time-constraint thread, Windows a time-critical thread in the MMCSS
  "Games" task, and Linux `SCHED_RR` where the system allows it.
  ([#1135](https://github.com/libretro/virtualjaguar-libretro/pull/1135))
- **Rolling back:** set **Settings > Interface > Emulation thread** to Off, or start the app
  with `--emu-thread off`. Without the thread, the app no longer repeats and drops frames in
  bursts as the display's refresh drifts against the console's.
  ([#1145](https://github.com/libretro/virtualjaguar-libretro/pull/1145))
- To report a threading problem, follow
  [the app README](https://github.com/libretro/virtualjaguar-libretro/blob/v3.11.0/app/README.md#reporting-a-threading-problem).
  ([#1131](https://github.com/libretro/virtualjaguar-libretro/pull/1131))

## Community cheats

- **Cheats > Community cheats** installs, updates or removes the pack the project site publishes
  for the loaded cartridge.
  - Packs are matched to the cartridge's CRC32.
  - Every cheat starts off.
  - Hardcore achievement mode is respected.

  **Library > Download art and cheat packs** fetches packs for the whole library.
  ([#1073](https://github.com/libretro/virtualjaguar-libretro/pull/1073),
  [#1076](https://github.com/libretro/virtualjaguar-libretro/pull/1076))
- The packs are converted from jguffer's
  [BigPEmu_Scripts](https://github.com/jguffer/BigPEmu_Scripts), with the author's permission
  and credit. The converter now also handles the following, so more of the scripts convert:
  - bounded integer settings;
  - read-only lookup tables;
  - nested constant password tables;
  - constant locals.
  ([#1091](https://github.com/libretro/virtualjaguar-libretro/pull/1091),
  [#1097](https://github.com/libretro/virtualjaguar-libretro/pull/1097),
  [#1113](https://github.com/libretro/virtualjaguar-libretro/pull/1113),
  [#1119](https://github.com/libretro/virtualjaguar-libretro/pull/1119),
  [#1120](https://github.com/libretro/virtualjaguar-libretro/pull/1120))

## TV mode is complete

- **TV Settings** (Picture, Speed, Sound, Interface), **Overlays**, **Achievements** and
  **More** screens.
  ([#1060](https://github.com/libretro/virtualjaguar-libretro/pull/1060),
  [#1077](https://github.com/libretro/virtualjaguar-libretro/pull/1077))
- **Automatic switching:** Settings > Interface > TV mode can be Off, Manual or Auto. A
  first-use hint explains how to get back.
  ([#1067](https://github.com/libretro/virtualjaguar-libretro/pull/1067))

## Emulation

- **Alien vs Predator:** the marines now attack when you play as the Alien. TOM's horizontal
  counter is derived from emulated time, as on hardware. Games use it as a random source, and the
  old per-read counter never produced the marines' fire roll. This also restores Atari Karts'
  AI variety.
  ([#1094](https://github.com/libretro/virtualjaguar-libretro/pull/1094),
  [#1096](https://github.com/libretro/virtualjaguar-libretro/pull/1096))
- **Faster GPU and DSP, bit-exact:**
  - The GPU interpreter inlines its hot opcodes and dispatches directly inside its loop.
  - The DSP's idle-loop skip now covers more wait loops, including Iron Soldier 2's and
    Cybermorph's.
  - The idle skip stays on with Blit Memoization enabled.

  Video, audio and savestates are unchanged.
  ([#1057](https://github.com/libretro/virtualjaguar-libretro/pull/1057),
  [#1117](https://github.com/libretro/virtualjaguar-libretro/pull/1117),
  [#1118](https://github.com/libretro/virtualjaguar-libretro/pull/1118),
  [#1121](https://github.com/libretro/virtualjaguar-libretro/pull/1121),
  [#1137](https://github.com/libretro/virtualjaguar-libretro/pull/1137),
  [#1139](https://github.com/libretro/virtualjaguar-libretro/pull/1139),
  [#1140](https://github.com/libretro/virtualjaguar-libretro/pull/1140))

## Desktop app

- **Windows without a modern OpenGL driver** (stock Windows on some machines and VMs offers only
  OpenGL 1.1) no longer exits at startup. The app retries with the bundled Mesa, using Direct3D 12
  when the GPU has it and software rendering otherwise.
  ([#1138](https://github.com/libretro/virtualjaguar-libretro/pull/1138),
  [#1143](https://github.com/libretro/virtualjaguar-libretro/pull/1143))
- **Memory viewer bitmap mode** previews graphics at any address. You can choose the depth, the
  colour mode and the CLUT, and the viewer can guess the width.
  ([#1095](https://github.com/libretro/virtualjaguar-libretro/pull/1095))
- **macOS:** the app no longer hangs in the GL swap when it starts with the display asleep, and the
  late latch's event wait ends on time.
  ([#1058](https://github.com/libretro/virtualjaguar-libretro/pull/1058),
  [#1089](https://github.com/libretro/virtualjaguar-libretro/pull/1089))
- **Windows portable updates** keep your `data/` folder (settings, saves, states, ROMs).
  ([#1059](https://github.com/libretro/virtualjaguar-libretro/pull/1059))
- The installers fetch their scripts from `master` (the released code), not `develop`.
  ([#1087](https://github.com/libretro/virtualjaguar-libretro/pull/1087))

## libretro core

- `.info`: `savestate_features = "deterministic"` (RetroArch already read the old value as
  deterministic).
  ([#1064](https://github.com/libretro/virtualjaguar-libretro/pull/1064))

## For packagers

- Packaging for the Microsoft Store (MSIX), Steam and a sandboxed Mac App Store build is in the
  tree. Store releases are not published yet.
  ([#1111](https://github.com/libretro/virtualjaguar-libretro/pull/1111),
  [#1112](https://github.com/libretro/virtualjaguar-libretro/pull/1112))
- The app has CMake presets. Configure warns when no librashader runtime will ship.
  ([#1132](https://github.com/libretro/virtualjaguar-libretro/pull/1132))

## Known limitations

- Doom's attract-demo timing and the other emulation limitations documented for v3.9.1 remain.
- Windows signing depends on the signing service's availability and approval, so not every
  Windows package may be signed.

## Downloads and upgrading

Install the app over your existing version; settings carry over. A stored emulation-thread
setting is kept. Only installs that never set it pick up the new default. RetroArch and
Provenance users can replace the core without changing options or v17 savestates.

Download the app, libretro cores and Jaguar CD tools from
[the v3.11.0 release](https://github.com/libretro/virtualjaguar-libretro/releases/tag/v3.11.0).
Use the release's `SHA256SUMS.txt` to verify downloads. macOS packages are universal; Windows has
portable and installer packages; Linux has AppImage and Debian packages.

## Validation

Candidate `5a976df8` passed a clean build on the Mac Studio. The library reports `v3.11.0`, and the
Makefile, `.info` and fallback versions agree. The full core suite (`make TEST_EXPORTS=1 test`) passed
with the private corpus, including:

- the audio presence and clipping tests;
- the TOM readback and MMULT tests;
- the GDB stub tests;
- the idle-skip A/B tests, which confirm that skip-on and skip-off output is bit-identical;
- the Doom pace gate.

Six checks were skipped because their fixtures or tools were missing on that host.

Against v3.10.0:

- **Cartridge matrix (155 titles, HLE and BIOS):** zero backward moves with either blitter. Gorf 2000
  improves in both modes, from a trap loop to game code. The three known HLE/BIOS asymmetries are
  unchanged.
- **CD matrix (28 title/mode rows, full disc corpus):** zero backward moves.
- **Corpus A/B (160 cartridges, 300 frames with audio):**
  - 157 are bit-identical, with the DSP idle skip off and on.
  - Atari Karts, Gorf 2000 and White Men Can't Jump differ, as expected. All three read TOM's horizontal
    counter, which #1094 changed. None of them differs in audio.

On the Mac Studio, in both a quiet and a loaded state, the app ran Cybermorph and yarc with the thread
off:

- Repeated and dropped images fell from 19-53 a minute to 0-1 repeats.
- Skips stay near the 3 a minute that a 60.05 Hz core has to make on a 60 Hz display. A loaded host
  reached up to 8 a minute.
- PAL is unchanged.

GitHub CI on the release pull request is the remaining gate.
