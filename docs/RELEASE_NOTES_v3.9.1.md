# Virtual Jaguar libretro v3.9.1

**Xenowings gets its sound effects back, Doom's GPU wait loop no longer eats a RISC overclock,
and the desktop app gains shaders on every package, an Interface settings tab and a new font
picker.**

v3.9.1 is a polish release on top of v3.9.0. For RetroArch and Provenance users it brings two
DSP/GPU accuracy fixes derived from the original chip netlists, a faster GPU idle-loop
fast-forward, a GDB stub that works with stock gdb, and a framebuffer bounds fix. With default
settings the cartridge corpus is byte-identical to v3.9.0 except Xenowings (sound effects now
play) and one frame of Skyhammer (a GPU divide by zero now returns the hardware's value). No
option keys were removed, no listed option values changed, and the savestate format stays v17.

---

## Core

- **Xenowings shot and explosion sounds play again**
  (https://github.com/libretro/virtualjaguar-libretro/issues/895,
  https://github.com/libretro/virtualjaguar-libretro/pull/954). A store issued right after a
  `div` normally writes the register's old value (Atari bug list item 2, modelled since v3.7.1).
  The jag_sim netlists show the exception: the scoreboard compares an indexed store's offset
  field against the divide's target as if it were a register number, and when they match the
  store waits for the quotient. Xenowings' sound mixer hits that case (`store r7,(r14+7)`), its
  joypad reader does not (`store r8,(r14+1)`), so both now behave as on hardware. Rule and
  citations: `docs/jtrm-errata.md` B25.
- **RISC divide keeps 33 bits** (same PR). Divisors of `$80000000` and above, 16.16 divides
  that overflow, and divide by zero now give the netlist's result (B26). The only corpus change
  is one Skyhammer frame (HLE, Fast blitter).
- **GPU idle-loop fast-forward covers more loops**
  (https://github.com/libretro/virtualjaguar-libretro/pull/957). A wait loop may now contain a
  long store when its address and value never change, the target is GPU local RAM, and the loop
  never reads what it writes; and the probe reaches the full `jr` range (30 bytes, was 14).
  Doom's wait loop is now skipped: instructions per frame on Doom EX drop from 83 M to 36 M at
  1x and from 141 M to 37 M at RISC 2x, so a 2x overclock costs about the same as 1x. Video,
  audio and savestate hashes are identical with the change on 160 carts (six configurations
  including 2x), with idle skip off, and on 35 CD images in HLE and BIOS mode.
- **Framebuffer overrun fixed**
  (https://github.com/libretro/virtualjaguar-libretro/issues/944,
  https://github.com/libretro/virtualjaguar-libretro/pull/946). Wild video registers (seen with
  a garbage custom boot ROM) could make the scanline renderer write past the end of the host
  framebuffer. Rows the buffer can't hold are now skipped, and a left edge past the row width
  is clamped; valid video states are unaffected. v3.9.0 was affected.
- **GDB remote stub works with stock gdb**
  (https://github.com/libretro/virtualjaguar-libretro/issues/936,
  https://github.com/libretro/virtualjaguar-libretro/pull/948). It now serves a real 68K
  target description, so `brew install gdb` (or `gdb-multiarch`) connects with only
  `set endian big`. `detach` and `kill` leave the emulator running, watchpoints work (one slot),
  and long `monitor` output is no longer cut at 512 bytes; `monitor trace` shows the newest
  entries. LLDB can read memory but has no 68K target. Recipe: `docs/gdb-stub-guide.md`.

## The desktop app

- **Shaders everywhere.** librashader is now built from source in CI, so the Intel slice of
  the macOS app, the AppImage and the `.deb` ship a working shader runtime
  (https://github.com/libretro/virtualjaguar-libretro/pull/955), and the Flatpak builds it as
  its own offline module (https://github.com/libretro/virtualjaguar-libretro/pull/953). When the
  runtime is unavailable, the Shader section says why and links to the docs.
- **Settings > Interface** (https://github.com/libretro/virtualjaguar-libretro/pull/941). Theme,
  font, frame-rate display, the controls overlay, resume, library display and desktop options
  moved out of Video and Storage into their own tab. Config keys are unchanged.
- **Font picker** (https://github.com/libretro/virtualjaguar-libretro/pull/943). Pick any font
  installed on the computer; favorites and recents; every name is drawn in its own typeface;
  and a separate monospace font for the debug windows, shown as samples. Fonts are scanned in the
  background the first time the list opens.
- **Smoother pacing when a game can't keep up**
  (https://github.com/libretro/virtualjaguar-libretro/pull/956). The app no longer sleeps or
  waits for vsync while it is behind: Doom at RISC 300% (idle skip off) went from 45 to 52 fps on
  an M2, against 55 for the core alone.
- **Fixes:** the BIOS selector picks the file you click when the same dump is in several folders
  (https://github.com/libretro/virtualjaguar-libretro/pull/940); the frame-rate label no longer
  draws over menus; duplicate ImGui IDs in the disc menu, option lists and cheat lists are gone.

## Website

- The download page is regrouped by platform and architecture, with a Nightly / Latest / Older
  releases picker (https://github.com/libretro/virtualjaguar-libretro/pull/958).
- New app screenshots: achievements with real artwork, Jaguar CD, themes, debug tools, and a
  gdb recipe (https://github.com/libretro/virtualjaguar-libretro/pull/959).
- The release log is generated from these notes, and the pages are current to v3.9
  (https://github.com/libretro/virtualjaguar-libretro/pull/964).

---

## Known issues

- **Doom's attract demo runs too fast** at every setting; gameplay is close. The GPU finishes a
  frame in 1-2 fields where hardware needs 4-8
  (https://github.com/libretro/virtualjaguar-libretro/issues/401). The timing options help only
  at RISC 1x, since an overclock speeds the GPU back up.
- **Blit Memoization, DRAM Timing and GPU/DSP Pipeline Timing switch the idle-loop
  fast-forward off** for every game while enabled, so overclocking with them on costs the full
  price. Leave them off unless a title needs them.
- **Shaders** are verified to load on every package, but rendering is only checked by hand on
  macOS (CI has no GPU).
- **Signing.** macOS packages are signed and notarized; Windows packages are not signed yet.
- The other v3.9.0 known issues still apply (RetroAchievements for `.chd`/`.cdi`, LAN-only
  link play, second instance on double-click on Windows and Linux).

---

## Upgrading

- **RetroArch / Provenance:** drop-in. The core reports v3.9.1; options and savestates (v17)
  carry over.
- **App:** install over v3.9.0. Settings carry over; the moved options appear under
  Settings > Interface.
