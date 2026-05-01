# Virtual Jaguar libretro v2.2.0

A large libretro-fork release — bumping from upstream v2.1.0.
Closes libretro/virtualjaguar-libretro#27, #85, and many sub-issues of #38.

## Highlights

- HLE BIOS that actually boots most commercial titles — no real BIOS image required.
- Save states, SRAM/EEPROM, cheat codes, and a RetroAchievements memory map.
- Audio pipeline rewrite — interleaved JERRY with the main loop, no more frame-edge dropouts.
- ~2x speedup on DSP / GPU / memory hot paths, plus a SIMD-accelerated blitter (SSE2 / NEON / scalar) with bit-exactness CI.
- Wide hardware-accuracy pass across DSP, GPU, blitter, Object Processor, TOM, JERRY, and 68K.
- Per-button retropad remapping for the Jaguar's 21-key controller (numpad included).
- Pre-built libretro cores for 14 platforms with an automated tagged-release workflow.
- Headless `make test` harness covering HLE BIOS, event queue, blitter SIMD bit-exactness, DSP MAC40, save states, and screenshot regressions.

## What's new

### Compatibility

- HLE BIOS improved — boot most commercial titles without a real
  BIOS image. Many games that previously needed the real BIOS, or
  worked-but-broken with it, now run on HLE BIOS too.
- Many homebrews now boot — non-Alpine raw .bin/.j64/.rom layouts
  are accepted at common startup bases ($4000, $20000, $802000),
  with conservative validation so unknown content fails fast
  instead of running into garbage.
- Compatibility list with per-game status is maintained in
  docs/emulation-bug-hunt-todos.md (see the "Game compatibility
  (v2.2.0)" section).

  Closes the long-standing libretro tracker:
   - libretro/virtualjaguar-libretro#27 — improve emulation accuracy
   - libretro/virtualjaguar-libretro#85 — make HLE actually usable
   - libretro/virtualjaguar-libretro#38 — many sub-issues addressed
     (per-game details in the compatibility doc)

### New features

- Save states (retro_serialize / retro_unserialize) with
  deterministic serialization for run-ahead.
- SRAM / EEPROM via the libretro SRAM interface, preserved
  across soft resets.
- Cheat code support (retro_cheat_set / retro_cheat_reset).
- RetroAchievements memory map (RC_CONSOLE_ATARI_JAGUAR).
- Audio rewrite: eliminated per-sample events and interleaved
  JERRY with the main execution loop, fixing frame-edge audio
  dropouts.  retro_run() now does
  `update_input -> DACPrepareFrame -> JaguarExecuteNew ->
  cheat_apply_all -> SoundCallback -> video_cb` so the audio
  callback sees the same JERRY state the frame was rendered
  against and doesn't drift.
- Per-button retropad remapping via core options
  (RetropadOptionMapping table + virtualjaguar_p1/p2_retropad_*
  options).  Lets users rebind the Jaguar's 21-key controller
  including numpad keys that the Controls menu can't reach.
- libretro geometry change is now applied at the START of
  retro_run rather than after video_cb.  Fixes Wolf3D black
  screen on iOS Metal RetroArch (frontends that re-allocate
  texture on SET_GEOMETRY were dropping the frame submitted at
  the old size; pre-rendering means tomWidth and screenPitch
  stay in sync for the entire frame).

### Hardware accuracy (chip side)

- DSP: 40-bit MAC accumulator, FLAGS-write dispatch, IRQ
  enable-on-set, IMASK preservation, ADDC carry overflow.
- GPU: ADDC carry, IMASK preservation, DIVL exception PC,
  PACK coverage, missing symbol exports.
- Accurate blitter: daddmode NAND tree, daddbsel bit 3,
  ADDARRAY cinsel carry input, M2 BKGWREN+BCOMPEN phrase
  mode (Alien vs Predator red-noise), SRCSHADE color.
- Object Processor: scaled-bitmap firstPix + left/right/
  reflected edge clipping, 1:1/3:2/2:1 hscale ratios,
  fixed-bitmap firstPix for 2/4/16/24 BPP, OP GPU-object
  handling.
- TOM: IRQ pending latches even when CPU enables are clear;
  enabling a pending source raises IPL2 cleanly. Horizontal
  counter reads advance. Doom resolution hack replaced with
  proper PWIDTH pixel replication (the hack option is gone).
- JERRY: interrupt-control decode, aligned PIT register
  access.
- 68K: TOM underflow handling, mixed-declaration cleanup.
- Event system: zero eventTime on init, full slot scan,
  timing-rollover and due-event handling.

### Performance

- ~2x speedup on DSP / GPU / memory hot paths.
- SIMD-accelerated blitter (SSE2, NEON, scalar) with
  bit-exactness CI.
- Accurate-blitter inner loop tightened.
- Note: the accurate blitter is still notably slower than the
  fast blitter; some games (e.g. Tempest 2000) may run below
  full speed on lower-end hardware. The fast blitter remains
  available via core option.

### Tooling and testing

- Headless `make test` harness covering HLE BIOS state,
  event queue, blitter SIMD bit-exactness, DSP MAC40, cheat
  decoder, memory map / RetroAchievements wiring, save-state
  round-trip and rewind, plus a regression screenshot diff
  via miniretro on push.
- Boot-time audio clipping detector (test_audio_clipping):
  captures the libretro audio batch, asserts on saturation
  density / longest run at +/-32767 / sustained RMS, with a
  healthy negative control and `--expect-clipping`-tagged
  regression watchers for known-broken titles (Skyhammer,
  Iron Soldier 2) so a future DSP-side fix flips CI red and
  forces the manifest to be updated.
- Source-tree reorganization into src/core, src/tom,
  src/jerry, src/cd, src/bios, src/m68000.  Files now grouped
  by chip / subsystem rather than the flat layout inherited
  from upstream.
- General code cleanup: removed dead inline-comment guesses,
  promoted Jaguar memory layout / HLE BIOS magic numbers to
  named constants at file scope rather than `#define`s
  scattered inside JaguarReset(), and added explanatory
  comments to non-obvious BIOS-handshake addresses (e.g.
  $0804 Battle Sphere workspace flag, $F03000 GPU auth magic).
- C89/GNU89 lint enforcement.

### Build / platforms

- MSVC 2005/2010 build fix (boolean.h vs stdbool.h).
- Emscripten / WASM support, lighter NEON dcomp variant.
- Portable xorshift PRNG for cross-platform RAM init.
- Automated release workflow for tagged versions.
- Debug-build timestamp reporting.

## Game compatibility

Roughly 22 titles called out in issue #38 are fixed since v2.1.0
(Air Cars, Atari Karts, Battlesphere Gold, Club Drive, Cybermorph,
Doom, Flashback, I-War, Kasumi Ninja, Missile Command 3D, Pinball
Fantasies, Powerdrive Rally, Skyhammer, Supercross 3D, Syndicate,
Trevor McFur, Val D'Isere, Zero 5, Zoop, plus the Raiden / Kasumi
Ninja HLE bridge, and others). About a dozen titles are still in
flight for v2.3.0 (Hover Strike, Hyper Force, Iron Soldier 1/2,
Ruiner Pinball, Super Burnout, Fight for Life, Tempest 2000
flicker, Battle Sphere reticle on accurate blitter, Battlesphere
Gold menu on fast blitter, NBA Jam TE jitter, Raiden HLE).

See `docs/emulation-bug-hunt-todos.md` ("Game compatibility
(v2.2.0)" section) for the full per-game table with diagnosis
notes.

## Known issues

- Skyhammer / Iron Soldier 2 / Wolfenstein 3D audio: the
  cart-side DSP audio engine doesn't run correctly under HLE
  (Wolf3D produces no audio at all in either BIOS or HLE
  mode after the BIOS startup tone; Skyhammer/IS2 produce a
  saturated square wave on HLE).  Cause is shared: the BIOS
  audio engine relies on DSP register-bank state that HLE
  doesn't replicate, and copying just the engine code into
  DSP RAM lets the DSP escape its work RAM by reading
  uninitialized registers as jump targets.  Documented in
  docs/emulation-bug-hunt-todos.md with concrete next steps.
- See docs/emulation-bug-hunt-todos.md "Game compatibility
  (v2.2.0)" — Hover Strike crashes during gameplay, Hyper
  Force / Iron Soldier / Ruiner Pinball / Super Burnout /
  Fight for Life / Towers II still have open issues, and
  the accurate blitter has a Battle Sphere reticle-
  transparency bug (fast blitter has a separate Battlesphere
  Gold menu glitch).
- Of the still-broken hang/crash titles, four (Hyper Force,
  Iron Soldier, Ruiner Pinball, Super Burnout) fail
  identically with real BIOS and HLE — not HLE-init issues,
  real engine-level emulation bugs.
- Jaguar CD support is in flight on a separate branch
  (forthcoming PR), not part of this release.

## Compared to upstream v2.1.0

Approximately 119 files changed, +21,065 / -6,479 lines.
76 commits since the v2.1.0-equivalent `libretro/master` baseline.

Top contributors:

- Joseph Mattiello — 76

## Downloads

This release ships pre-built libretro cores for 14 platforms:

- Linux: x86_64, aarch64, i686
- macOS: arm64, x86_64
- Windows: x86_64, i686 (MSYS2/MinGW)
- iOS: arm64; tvOS: arm64
- Android: arm64-v8a, armeabi-v7a, x86_64, x86
- Web: Emscripten WASM
- Consoles: PS Vita, Nintendo Switch

Each binary has a matching `*-debug.tar.gz` with split debug symbols.
SHA256 checksums in `SHA256SUMS.txt`.

## Maintainers

libretro/Provenance fork: Joseph Mattiello (@JoeMatt, Provenance-Emu).
