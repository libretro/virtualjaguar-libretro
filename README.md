# Virtual Jaguar libretro — Atari Jaguar & Jaguar CD emulator core

**Virtual Jaguar libretro** is an open-source **Atari Jaguar emulator** — cartridges *and* **Jaguar CD** — packaged as a **libretro core**, so it runs everywhere **RetroArch** runs.

All four Jaguar processors are emulated (68000, GPU, DSP, Object Processor), plus the CD drive's BUTCH controller, the link port, the Memory Track cartridge and the Jaguar GameDrive. CI builds 16 platforms on every release tag. Every accuracy, performance and compatibility claim below links to the commit, pull request, test log or committed document behind it.

[![C/C++ CI](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/libretro/virtualjaguar-libretro/actions/workflows/c-cpp.yml)
[![Latest release](https://img.shields.io/github/v/release/libretro/virtualjaguar-libretro)](https://github.com/libretro/virtualjaguar-libretro/releases)
[![License](https://img.shields.io/github/license/libretro/virtualjaguar-libretro)](LICENSE)
[![Platforms](https://img.shields.io/badge/platforms-16-blue)](.github/workflows/release.yml)

![Three-panel comparison of Cybermorph terrain rendered by the Virtual Jaguar libretro core: the stock 16-bit CRY frame on the left, the same frame with true-color rendering enabled in the middle, and on the right a difference map amplified 64 times showing shading refinement spread across the polygon surfaces.](site/assets/truecolor_ab_cybermorph.png)

<sub>Cybermorph, frame 850. Stock / true-color / difference. The difference panel is amplified **64×** on purpose — the per-pixel delta is deliberately subtle. Details and numbers in [Enhancements](#enhancements).</sub>

---

## What makes this core different

### Everything RetroArch does, you get for free

This is a libretro core, so the whole frontend feature set applies — and the core does the work required to make the harder ones actually function:

| Frontend feature | What the core does to support it |
| --- | --- |
| **RetroAchievements** | Exposes libretro memory maps and asserts achievement support; a harness checks the rcheevos Atari Jaguar console mapping resolves against this core's RAM ([`test_memory_map.c`](test/tools/test_memory_map.c), [`test_rcheevos_e2e.sh`](test/tools/test_rcheevos_e2e.sh)) |
| **Run-ahead, rewind, netplay** | Declares deterministic savestates, backed by a harness that reproduces RetroArch's rollback loop and demands byte-identical replay ([PR #327](https://github.com/libretro/virtualjaguar-libretro/pull/327), [`test_runahead_determinism.c`](test/tools/test_runahead_determinism.c)) |
| **Save states that keep working** | Every savestate format any released version of this core ever wrote still loads — versions 1 through 8, with the one documented exception that v1 predates four DAC fields and restores them to defaults ([`savestate-compat.md`](docs/savestate-compat.md)) |
| **Shaders, overrides, remapping, fast-forward, screenshots, streaming** | Clean XRGB8888 video and 48 kHz stereo audio out of [`libretro.c`](libretro.c); RetroArch does the rest |
| **Cheats** | Native cheat-code decoding, covered by the test suite |

### And the Jaguar-specific parts

- **Jaguar CD, two ways.** CUE/BIN and CDI images boot through a high-level-emulated CD BIOS *or* through the real CD BIOS driving emulated BUTCH/DSA/FIFO hardware. Audio CDs play and the Virtual Light Machine visualizer works. ([v3.0.0 notes](docs/RELEASE_NOTES_v3.0.0.md) · [v3.1.0 notes](docs/RELEASE_NOTES_v3.1.0.md))
- **Damaged-rip repair.** CDI images from the old V2 ripper that lost each track's leading bytes used to be rejected outright. The loader now classifies the boot track and repairs slides or head-loss at read time; when bytes are provably gone it logs a precise diagnosis instead of a black screen. ([PR #342](https://github.com/libretro/virtualjaguar-libretro/pull/342))
- **JagLink / netlink link-cable networking.** The Jaguar's link-port serial hardware is emulated at the register level with a TCP transport and a multi-peer hub — Doom deathmatch over LAN validated end to end. ([user guide](docs/netlink-user-guide.md) · [design doc](docs/netlink-design.md))
- **Jaguar GameDrive (JagGD).** Detection plus full 16 MB bank switching (6×1 MB pages over 16 banks), implemented from RetroHQ's published GDBIOS bindings, so GD-locked homebrew boots. ([interface notes](docs/jgd-interface-notes.md) · v3.1.0 #312, #320)
- **Memory Track saves.** The Jaguar CD's Memory Track cartridge is emulated — a flash window at `$900000` plus an HLE'd NVM BIOS module — so CD titles keep their saves, round-tripping through the frontend's `.srm`. ([`memory-track.md`](docs/memory-track.md) · [PR #259](https://github.com/libretro/virtualjaguar-libretro/pull/259))
- **A crash watchdog that names the bug.** A runtime watchdog recognises hang and crash signatures (GPU/DSP program-counter escape, wedges, video stalls, CD seek wedges) and writes the diagnosis into your frontend log at the moment of failure — so a bug report points at the subsystem, not just "black screen". ([`crash_detect.c`](src/core/crash_detect.c))
- **No BIOS files required.** The console boot ROM and both Jaguar CD BIOSes ship inside the core. See [BIOS](#bios).
- **Hardware-referenced accuracy.** Emulation decisions are checked against the Jaguar Technical Reference Manual, and an "acid" suite of hardware-behaviour assertions gates CI ([`test/acid`](test/acid)). A distilled, machine-readable JTRM summary is committed as [`docs/jtrm-*.md`](docs).

### One comparative claim

We know of no other Atari Jaguar emulator offering a true-color rendering mode or an internal-resolution upscaling track like the ones described below. If we're wrong, we'd genuinely like to know — [tell us in Discussions](https://github.com/libretro/virtualjaguar-libretro/discussions) and this file gets corrected. We make no other claims about any other emulator's internals.

---

## Enhancements

Features the original hardware never had, built so the stock pipeline stays bit-identical when they're off — and measured, not just screenshotted, when they're on. Every one is a core option you set in your frontend's core-options menu.

### True-color rendering

The Jaguar's blitter computes Gouraud-shaded intensity with **16 fraction bits** of precision, then throws them away writing a 16-bit CRY pixel. The true-color shadow framebuffer keeps them: with `virtualjaguar_true_color = enabled`, shaded pixels are *also* stored at full precision (CRY chroma × 24-bit intensity) in a shadow buffer, and the scanline renderer substitutes them at output time. Game-visible RAM stays bit-identical — the game cannot tell the difference.

Measured on Cybermorph, both blitter engines agreeing exactly ([PR #341](https://github.com/libretro/virtualjaguar-libretro/pull/341) validation record):

| Measurement | Result |
| --- | --- |
| Unique on-screen colors | 304 → **450** (peak frame 423 → 702) |
| Pixels refined | **45.9%**, every changed channel moving by *exactly one* quantization step |
| Difference panel above | amplified **64×** — the per-pixel delta is deliberately subtle |
| Identity with the option off | 600 frames across three cartridge titles hash bit-identical to develop |
| Cost | CRY 16bpp only, default off, ~8 MB allocated only when enabled |

Honest caveat: **Checkered Flag shows no change** — it turns out to be flat-shaded and never uses blitter Gouraud at all. This is banding removal on shaded geometry, not a filter repainting the image, so titles that don't shade don't move.

Design doc: [`docs/true-color-shadowfb-design.md`](docs/true-color-shadowfb-design.md). Status: **merged to `develop`** (PR #341, after the v3.1.0 tag) — available in [nightly builds](https://github.com/libretro/virtualjaguar-libretro/releases/tag/nightly) today, ships in the next tagged release.

### Overclocking that doesn't distort pacing

Two independent clock-scale options — the 68000 (`virtualjaguar_m68k_clock_scale`, 0.5×–3×) and the RISC GPU/DSP pair (`virtualjaguar_risc_clock_scale`, 0.5×–2×) — for framerate-limited titles. Bus and DRAM latencies are charged in wall time under any clock scale (the cycle-domain contract), defaults are bit-identical to previous behaviour, and audio pitch is proven unchanged at every scale by CI rows running at non-1× scales. ([v3.1.0 notes](docs/RELEASE_NOTES_v3.1.0.md) #314, #316 · [issue #318](https://github.com/libretro/virtualjaguar-libretro/issues/318) via [PR #337](https://github.com/libretro/virtualjaguar-libretro/pull/337))

### CD read-speed control

`virtualjaguar_cd_read_speed`: 1× (hardware-faithful) through 2×/4×/8× up to instant, trimming load screens on the HLE CD path. Implemented with streaming reads so accelerated transfers can't overwrite in-flight game code — the failure mode instant reads used to cause is documented, fixed and regression-tested. ([`docs/cd-read-speed.md`](docs/cd-read-speed.md))

### Crash watchdog

`virtualjaguar_crash_detect` (on by default) checks once per frame for known failure signatures and writes the diagnosis into the frontend log. Cost when enabled: one indirect call plus a ~256-pixel hash per frame. ([`src/core/crash_detect.c`](src/core/crash_detect.c))

### Per-title enhancement defaults

`virtualjaguar_pertitle_defaults` (on by default) applies known-safe enhancement presets automatically for recognized games — only for options you've left at their default value. Any option you've changed yourself always wins, and disabling this option restores stock behaviour for every title. Seeded titles: **Alien vs Predator** (2x internal resolution + true color) and **Cybermorph** (true color). New entries require committed evidence — propose candidates via [issue #368](https://github.com/libretro/virtualjaguar-libretro/issues/368).

### Roadmap: internal hi-res upscaling — *in design*

The true-color shadow framebuffer is deliberately the 1× prototype of a larger architecture: rendering above native resolution inside the core. That work, and the rest of the enhancement suite, is tracked as epic [#338](https://github.com/libretro/virtualjaguar-libretro/issues/338). It is **in design** — no dates, no promises beyond what's in the issue.

---

## Compatibility

### Jaguar CD boot matrix

Generated by [`test/tools/cd_boot_matrix.sh`](test/tools/cd_boot_matrix.sh) into [`docs/cd-boot-matrix.md`](docs/cd-boot-matrix.md) — not typed in by hand. Every image is exercised in **both** CD modes: HLE (high-level-emulated CD BIOS) and BIOS (the real CD BIOS driving emulated BUTCH/DSA/FIFO hardware).

| Measured from the committed matrix | Count |
| --- | --- |
| Disc images in the corpus | **11** |
| Reach game code in HLE mode | **11** |
| Reach game code in real-BIOS mode | **11** |

Titles in the current corpus, all recorded `GAME_CODE` in both modes: Baldies (CUE and CDI), Battle Morph, BrainDead 13, Dragon's Lair, Highlander, Hover Strike — Unconquered Lands, Iron Soldier 2, Myst, Primal Rage, Space Ace.

**Read this honestly.** "Reaches game code" means the 68000 was observed executing game-owned code in RAM with a healthy, non-looping PC trace — a headless regression gate, **not** a completed-the-game certificate. Rows are *disc images*, not distinct games (`baldies.cdi` and `Baldies (USA) (Rev 1).cue` are the same title from two rips). Several rows also record a `cd_seek_wedge` or `video_stall` watchdog signature alongside a passing trace; those are frequently benign (Myst fires one during the intro movie's ~6 s all-black pause while the movie plays from RAM) — the stage column, not the watchdog column, is the gate, and the matrix doc discusses each case. Runs are on the maintainer's own images at the build stamp recorded in the matrix, so your dump may differ.

### Cartridges

Cartridge titles have no equivalent committed per-title matrix yet. Named, verified fixes and open known issues are tracked in [`docs/cart-issue-triage.md`](docs/cart-issue-triage.md) and the release notes; the CD backlog lives in [`docs/cd-known-issues.md`](docs/cd-known-issues.md). Open, tracked examples: Doom combat pacing still runs fast ([#313](https://github.com/libretro/virtualjaguar-libretro/issues/313)), Battle Morph runs fast ([#331](https://github.com/libretro/virtualjaguar-libretro/issues/331)), Battle Sphere menu text renders dark.

libretro also hosts a community-maintained **[Atari Jaguar compatibility list](https://docs.libretro.com/library/compatibility/jaguar/)** — a separate document from the machine-generated boot matrix above, with different methodology and a wider scope.

**If a title works (or doesn't) for you, a report in [Discussions](https://github.com/libretro/virtualjaguar-libretro/discussions) is genuinely useful.** Attach your frontend log: the crash watchdog writes the failure signature at the moment things break.

---

## Quick start

1. **Install the core.** In RetroArch: *Main Menu → Online Updater → Core Downloader → Atari - Jaguar (Virtual Jaguar)*. Or download a build from [Releases](https://github.com/libretro/virtualjaguar-libretro/releases) and drop it in your `cores` folder.
2. **No BIOS files needed.** Nothing to hunt down — see [BIOS](#bios).
3. **Load a game.** Supported: `.j64`, `.jag`, `.rom`, `.abs`, `.cof`, `.bin`, `.prg` (including inside ZIP archives), plus `.cue` and `.cdi` for Jaguar CD images, and conservative headerless raw homebrew loading.

### Core options worth knowing

| Option | Why you'd touch it |
| --- | --- |
| `virtualjaguar_usefastblitter` | Fast vs. accurate blitter. The accurate path is SIMD-accelerated (SSE2/NEON); a few titles still prefer fast |
| `virtualjaguar_true_color` | Full-precision Gouraud shading (see [Enhancements](#enhancements)); default off |
| `virtualjaguar_cd_boot_mode` | HLE CD BIOS (default) vs. real CD BIOS. Audio-only discs need `Auto`/`Real BIOS` |
| `virtualjaguar_cd_read_speed` | Trim CD load screens |
| `virtualjaguar_m68k_clock_scale` / `virtualjaguar_risc_clock_scale` | Overclock a framerate-limited title |
| `virtualjaguar_pal` | NTSC (320×240) vs. PAL (320×256) |
| `virtualjaguar_crash_detect` | Leave on — it's what makes your bug report actionable |
| `virtualjaguar_alt_inputs`, `virtualjaguar_p*_*` | 2-player input and full numpad remapping |

The canonical, always-current list is the [core options reference](https://docs.libretro.com/library/virtual_jaguar/#core-options) on docs.libretro.com.

### Where saves live

Cartridge EEPROM/SRAM and the Jaguar CD Memory Track both go through the libretro SRAM interface, so RetroArch writes them to its **`saves` folder as `<gamename>.srm`**; save states land in the **`states` folder**. Nothing is written next to your ROMs.

---

## BIOS

**No BIOS files are required.** The Jaguar console boot ROM and both Jaguar CD BIOSes (retail and developer) are embedded in the core, so every boot mode works out of the box:

- **Cartridges** — the `BIOS (Cartridges)` core option chooses between the HLE BIOS (default: the core performs the boot setup itself, skipping the boot animation) and the real boot ROM. The console boot ROM is always the embedded copy; it is never loaded from disk. The HLE BIOS reproduces hardware-equivalent post-boot state — MEMCON1, clocks, GPU auth magic, OLP, video-interrupt compare, exception vectors, TOM/JERRY timing — pinned by 219 checks in `test_hle_bios` across NTSC and PAL.
- **CD discs** — the `CD Boot Mode` core option chooses between the HLE CD BIOS (default, recommended) and a real CD BIOS (`Real BIOS`, or `Auto`, which currently also boots the real BIOS). In the real-BIOS modes the `CD BIOS Type` option selects the retail or developer image.

### Optional external CD BIOS override

In the real-BIOS CD modes only (`CD Boot Mode` set to `Real BIOS` or `Auto`), a CD BIOS ROM file in the RetroArch `system` directory takes precedence over the embedded images — useful if you want to run a specific BIOS revision. The file must be exactly 256 KiB and can live directly in `system/` or in an `Atari - Jaguar/`, `Atari - Jaguar CD/`, `jaguar/`, or `jaguarcd/` sub-folder, under one of these names:

| Type | Accepted filenames |
| --- | --- |
| Retail | `[BIOS] Atari Jaguar CD (World).j64` / `.rom` / `.bin` |
| Developer | `[BIOS] Atari Jaguar Developer CD (World).j64` / `.rom` / `.bin` |
| Generic | `jaguarcd_bios.bin`, `jagcd_bios.bin`, `jaguarcd.bin`, `jagcd.bin`, `Jaguar CD BIOS.rom`, `Jaguar CD BIOS.bin` |

Selection is by filename only — the core does not verify which BIOS a file actually contains, so a mislabelled file will boot to a black screen (a truncated or wrong-console file falls back to the embedded BIOS with a log line). If real-BIOS CD boots misbehave, remove or rename any CD BIOS files in `system/` to fall back to the known-good embedded images.

---

## What's new

### Since the v3.1.0 tag — merged to `develop`, in nightlies, not yet in a tagged release

- **True-color shadow framebuffer** — full-precision Gouraud rendering, the opener of the [#338](https://github.com/libretro/virtualjaguar-libretro/issues/338) enhancement suite. ([PR #341](https://github.com/libretro/virtualjaguar-libretro/pull/341))
- **Damaged CDI V2 rips boot** — per-track boot-header repair. World Tour Racing reaches an in-game race and the Myst demo plays through the Cyan intro, in both CD modes; images whose bytes are provably gone get a precise logged diagnosis instead of a silent failure. ([PR #342](https://github.com/libretro/virtualjaguar-libretro/pull/342))
- **EEPROM save-data corruption fixed** — EEPROM data-out is sampled at `$F14000`, the *joystick* register, and our read was destructive: an interrupt-context pad scan landing inside an in-flight EEPROM read stole data bits. On Raiden that flipped a stored `$0000` into `$00FF` and permanently disabled the music option — persisted back to the save file. Now 93C46-correct: reads sample, only the clock strobe shifts. Affects any title polling the pad from an interrupt during an EEPROM read. ([PR #344](https://github.com/libretro/virtualjaguar-libretro/pull/344))
  *If you were hit by this, re-enable music in Raiden's options menu — the bad value may be sitting in your `.srm`.*
- **Raiden boots in HLE mode** — the HLE fast-boot path never programmed TOM's video-interrupt compare register, which Raiden inherits from the boot ROM rather than setting itself, so its VBlank ISR never fired. Black screen since [#20](https://github.com/libretro/virtualjaguar-libretro/issues/20)/[#70](https://github.com/libretro/virtualjaguar-libretro/issues/70). ([PR #339](https://github.com/libretro/virtualjaguar-libretro/pull/339))
- **CD FMV verification** — a real-BIOS evidence table plus a documented CD boot-mode gotcha in the visual-verification harness. ([PR #340](https://github.com/libretro/virtualjaguar-libretro/pull/340))

### v3.1.0

Jaguar GameDrive support, clock-scale options, audio-CD / Virtual Light Machine playback, savestate compatibility restored for every released format, a **~22% smaller binary** (the UAE 68K core shipped two complete handler tables and dispatched one), and the **accurate blitter ~16% faster** on blit-heavy titles (Tempest 2000 +16.2%, bit-identical across 132,912 compared blits). [Full notes](docs/RELEASE_NOTES_v3.1.0.md).

### v3.0.0

Jaguar CD support matured, JagLink networking, a compatibility wave (Wolfenstein 3D music, Pitfall, Tempest 2000, Alien vs Predator, Iron Soldier 2), and the hardware-referenced acid suite gating CI. [Full notes](docs/RELEASE_NOTES_v3.0.0.md).

Earlier: [v2.3.2](docs/RELEASE_NOTES_v2.3.2.md) · [v2.3.1](docs/RELEASE_NOTES_v2.3.1.md) · [v2.3.0](docs/RELEASE_NOTES_v2.3.0.md) · [v2.2.0](docs/RELEASE_NOTES_v2.2.0.md) · [full changelog](docs/WHATSNEW)

---

## Building

```bash
make -j$(getconf _NPROCESSORS_ONLN)            # Auto-detects platform
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1    # Debug build (-O0 -g)
make platform=ios-arm64                         # Cross-compile target
make clean
```

Output: `virtualjaguar_libretro.{so,dylib,dll}` at the repo root. Source is **C89/GNU89-strict** (the libretro buildbot uses MSVC); `bash scripts/c89-lint.sh` is a CI gate.

```bash
make test                                       # full white-box test suite
./test/regression_test.sh ./virtualjaguar_libretro.so   # screenshot regression
make benchmark                                  # headless wall-clock perf
```

Full contributor mechanics — branching, lint gates, commit style, pre-commit hook — are in [CONTRIBUTING.md](CONTRIBUTING.md). **New PRs target `develop`**, not `master`.

### Platforms built by CI on every release tag

Linux (x86_64, aarch64, i686) · macOS (Apple Silicon, Intel) · Windows (x64, x86) · Android (arm64-v8a, armeabi-v7a, x86_64, x86) · iOS · tvOS · WebAssembly · PS Vita · Nintendo Switch — 16 targets, defined in [`release.yml`](.github/workflows/release.yml). Other handhelds and consoles get the core through the libretro buildbot.

---

## The machine being emulated

Four processors on a unified, big-endian memory map:

| Part | Role |
| --- | --- |
| **Motorola 68000** @ ~13.3 MHz | Main CPU ([`src/m68000/`](src/m68000), UAE-derived) |
| **GPU** @ ~26.6 MHz RISC | Graphics coprocessor ([`src/tom/gpu.c`](src/tom/gpu.c)) |
| **DSP** | Same RISC ISA, audio ([`src/jerry/dsp.c`](src/jerry/dsp.c)) |
| **Object Processor** | Sprite/bitmap display list ([`src/tom/op.c`](src/tom/op.c)) |
| **TOM / JERRY** | Video + blitter / audio, timers, EEPROM, UART |
| **BUTCH** | Jaguar CD controller ([`src/cd/`](src/cd)) |

System clock 26.590906 MHz NTSC / 26.593900 MHz PAL. Full source map: [`docs/source-layout.md`](docs/source-layout.md).

**Known limitations:** the blitter is not fully cycle-accurate (a few games need fast mode) and GPU/DSP pipeline hazards are not yet modelled ([#313](https://github.com/libretro/virtualjaguar-libretro/issues/313)), which shows up as some titles running fast.

---

## Documentation and support

- **[Official libretro documentation for this core](https://docs.libretro.com/library/virtual_jaguar/)** — the canonical reference manual: supported extensions, the [core options reference](https://docs.libretro.com/library/virtual_jaguar/#core-options), [controller tables](https://docs.libretro.com/library/virtual_jaguar/#controllers), frontend feature support. Maintained in [`libretro/docs`](https://github.com/libretro/docs) separately from this repository, so it can lag the newest releases.
- **[GitHub Discussions](https://github.com/libretro/virtualjaguar-libretro/discussions)** — questions and compatibility reports. Bring your frontend log.
- **[Issues](https://github.com/libretro/virtualjaguar-libretro/issues)** — confirmed bugs.
- **[Releases](https://github.com/libretro/virtualjaguar-libretro/releases)** — tagged builds for 16 platforms. The rolling **[`nightly` prerelease](https://github.com/libretro/virtualjaguar-libretro/releases/tag/nightly)** is rebuilt from every push to `develop`; it is gated on *compiling*, not on the test suite — treat it as bleeding edge.
- **[`docs/`](docs)** — the full document index: [network play setup](docs/netlink-user-guide.md), [file formats](docs/README), [source layout](docs/source-layout.md), [savestate compatibility](docs/savestate-compat.md), [CD read speed](docs/cd-read-speed.md), [CD known issues](docs/cd-known-issues.md), [cartridge triage](docs/cart-issue-triage.md), [profiling](docs/profiling.md), [release process](docs/release-process.md), [changelog](docs/WHATSNEW), [TODO](docs/TODO), and the distilled JTRM hardware references (`docs/jtrm-*.md`).
- **[SECURITY.md](SECURITY.md)** — security policy and binary verification.

## Contributors

Built on the work of many people — see the [full list on GitHub](https://github.com/libretro/virtualjaguar-libretro/graphs/contributors).

- Original Virtual Jaguar by David Raingeard (Potato Emulation).
- SDL/Linux/Win32 port by Niels Wagenaar & Carwin Jones (SDLEMU).
- Cleanups, GUI/Qt port, and ongoing upstream maintenance by James Hammons (Shamus) — upstream: `git clone http://shamusworld.gotdns.org/git/virtualjaguar` ([GitHub mirror](https://github.com/mirror/virtualjaguar)).
- libretro core port by libretro/RetroArch contributors.
- Current maintainer — Joseph Mattiello ([@JoeMatt](https://github.com/JoeMatt)). This repository is where Virtual Jaguar development continues today.

## License

[GNU General Public License v3.0](LICENSE).
