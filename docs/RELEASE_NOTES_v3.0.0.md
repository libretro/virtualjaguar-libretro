# Virtual Jaguar libretro v3.0.0

Major release — 303 commits (246 non-merge) since v2.3.2. Jaguar CD support matures into a
full local-corpus boot pass in both BIOS and HLE modes, Jaguar Link
networking ships (JagLink/CatNet over TCP and RetroArch netplay), core
options are reorganized into categories with content-aware hiding, and a
wide compatibility wave lands across retail carts. This is also the first
release built on a hardware-referenced acid test suite with a CI regression
gate that actually blocks.

## Highlights

- **Jaguar CD support matured.** The full locally-available 10-title corpus
  (Baldies, Battle Morph, BrainDead 13, Dragon's Lair, Highlander, Hover
  Strike, Iron Soldier 2, Primal Rage, Space Ace, Myst) boots to game code in
  both HLE and real-BIOS mode. Memory Track NVM is HLE'd so Vid Grid saves
  again (#258); a `virtualjaguar_cd_read_speed` option (1x/2x/4x/8x/
  instant) is available on the HLE path; the `cue2cdi` CUE/BIN→CDI converter
  tool ships for building interchange images.
- **Jaguar Link networking.** JagLink/CatBox link-port hardware is emulated
  at the JERRY UART level, with pluggable transports: a direct TCP link
  (designed to work in any frontend without netplay support -- validated on
  macOS; the iOS/Provenance device test is pending, see Known Issues), the
  libretro netpacket interface (real RetroArch netplay), and a CatNet-style
  multi-peer hub for more than two consoles. Hostname resolution (not just dotted-quad
  addresses) and a core-option-driven host/port config round out the
  ease-of-use work. Validated over TCP on localhost/LAN: Doom deathmatch
  (host+client join, playable), BattleSphere Gold (to networked-lobby entry),
  AirCars (2-player direct + 3-console hub).
- **Reorganized core options.** All options are grouped into 9 categories
  with content-aware hiding (CD-only options disappear for cart content, and
  so on), clarified BIOS/CD-BIOS wording, and an i18n pipeline (Crowdin
  workflow + `libretro_core_options_intl.h` scaffolding) ready for
  translations.
- **Compatibility wave.** Wolfenstein 3D missing music (DSP `D_FLAGS`
  pipeline-stage fix), Pitfall's black screen shortly into gameplay (68000
  group-0 exception frame + GPU STOREP masking, #138), Tempest 2000's one-frame playfield collapse
  (TOM level-2 IRQ request held across the CPU interrupt mask, #187), Alien
  vs Predator's first-frame cyan bar and in-game brown bottom bar (#178),
  Brutal Sports Football's copier-header carts (skip instead of reject), and
  a cluster of CD-specific fixes (Iron Soldier 2 match-load hang + cold-boot
  black screen, Battle Morph HLE-ISR stomp, Myst and Primal Rage boot/audio,
  Hover Strike's instant-CD-read code-stomp lockup, Baldies HLE progress
  reporting).
- **Hardware-referenced acid suite with a blocking CI gate.** The acid test
  suite's three live regressions (`unaligned_word`, `op_gpu_int_object`,
  `pit_countdown_rate`) were reconciled against the JTRM, the 68000 bus-wait
  model was corrected to charge only wait states (the 68K's own bus cycle
  already covers DRAM access — only slow cartridge ROM stalls it), and a
  `pipefail` bug that let the acid CI job silently pass on regressions is
  fixed. The suite is now a real regression gate, not an aspirational one.

## Breaking changes

- **`.iso` CD images are no longer supported.** A bare 2048-byte-sector ISO
  cannot represent a Jaguar CD's multi-session layout (session 1 audio
  warning track, session 2 data recorded as byte-swapped 2352-byte audio-type
  sectors, track lead-in offsets) — no retail disc can boot from one, and
  BigPEmu likewise does not accept bare `.iso` images. The loader now refuses
  `.iso` at load time with an explanatory error instead of presenting a BIOS
  screen that goes nowhere. Use CUE/BIN or CDI (see `test/tools/cue2cdi` to
  convert).
- **Savestate format v7 (forward-compatibility note).** Savestates written
  by v3.0.0 will not load in older cores. In the other direction almost
  nothing breaks: all states from the v2.x releases (`STATE_VERSION` >= 2)
  continue to load, with newer fields defaulted; only pre-v2 states from far
  older cores are rejected. v7 is frozen as the stable format going
  forward.

## What's new

### Jaguar CD

- Full local corpus (10 titles across 11 images -- 10 CUE/BIN plus 1 CDI)
  reaches `GAME_CODE` in both HLE and real-BIOS mode — the CD boot-matrix regression gate (`docs/cd-boot-matrix.md`)
  is green across all active rows, with no genuine regressions across the
  re-run sweeps this cycle (one apparent backward move was traced to a
  substituted bad disc image and is documented in the matrix).
- Memory Track NVM BIOS module HLE'd (`_NVM` at `$2400`) alongside the
  `$900000` flash window — Vid Grid and other Memory-Track-dependent titles
  save correctly (#258).
- The real-BIOS path falls back to an embedded CD boot ROM when no external
  BIOS file is configured, so no BIOS file is strictly required.
- `virtualjaguar_cd_read_speed` core option (1x/2x/4x/8x/instant), HLE-path
  only by design — scaling the real-BIOS path's FIFO/DSA cadence would
  reopen DSA-steal and FIFO-storm race classes those constants encode.
- `test/tools/cue2cdi`: CUE/BIN → CDI converter with a batch mode, for
  building interchange images from disc rips.
- Known bad/copier-header dumps are now warned about and named instead of
  silently mis-parsed.
- CD trace ring (core option `virtualjaguar_cd_trace` / env `VJ_CD_TRACE=1`)
  and `test/tools/cd_wedge_probe` / `cd_visual_verify` tooling for diagnosing
  intermittent CD lockups without a device.

### Networking (Jaguar Link)

- JERRY asynchronous serial UART emulated at the register level
  (`ASIDATA`/`ASICTRL`/`ASISTAT`/`ASICLK`, `$F10030`-`$F10035`), verified
  against the JTRM rather than source comments.
- Pluggable link transport: loopback (default/disabled), direct TCP
  (`tcp_server`/`tcp_client` core-option modes, per-frame link poll), and the
  libretro netpacket interface for real RetroArch netplay integration.
- CatNet-style multi-peer hub in the TCP link server for more-than-two-console
  play.
- Sub-frame link latency: immediate TX flush plus mid-frame RX pump, a
  self-tuning bounded wall-clock reply wait, and a fix for Wi-Fi netplay lag
  caused by netpacket TX batching (#248).
- Netlink host configurable as a core option; `vj_netlink.txt` is now a
  fallback rather than the only path. Hostnames (not just dotted-quad
  addresses) resolve (#263).
- UART + link state serialize in savestates.

### Options menu

- Core options grouped into 9 categories (#251).
- Options that don't apply to the loaded content type (e.g. CD-only options
  with a cart loaded) are hidden dynamically (#251).
- CD BIOS Type wired up; BIOS vs. CD-BIOS wording clarified (#251).
- i18n scaffolding: Crowdin workflow + `libretro_core_options_intl.h`
  (0 languages translated yet — infrastructure only in this release).

### Bug fixes (compatibility)

- **Wolfenstein 3D**: missing game music fixed (DSP `D_FLAGS` pipeline-stage
  correction).
- **Pitfall**: the black screen shortly into gameplay is fixed — the 68000
  now builds the full 14-byte group-0 bus-error exception frame (it
  previously pushed only a standard frame), and GPU `STOREP` gained
  byte-lane masking; GPU-RAM sync correction (#138, 2 of 3 tracked
  sub-issues; the remaining item is a GPU ISR r31 leak, non-blocking, noted
  in `docs/cart-issue-triage.md`).
- **Tempest 2000**: one-frame playfield collapse fixed by holding TOM's
  level-2 interrupt request across the CPU interrupt mask instead of
  dropping it (#187).
- **Alien vs Predator**: first-frame cyan bar fixed (framebuffer now seeds
  opaque black, not cyan — a core-wide artifact, not AvP-specific); in-game
  brown bottom bar fixed (rows the TOM never writes are now blanked instead
  of showing stale data) (#178, 2 of 4 tracked sub-symptoms; green-dot
  artifact and a savestate-rejection edge remain open, see Known Issues).
- **Brutal Sports Football**: 512-byte copier header is now skipped instead
  of causing the image to be rejected.
- **Iron Soldier 2**: match-load hang fixed (single-step + driver-ownership
  handling); cold-boot black screen fixed (unaligned `CD_read` tail now
  carries the next disc bytes instead of padding).
- **Battle Morph**: HLE ISR stomp fixed — stream status base is now latched
  per `CD_read` instead of leaking across calls; boots in both HLE and
  BIOS mode.
- **Myst**, **Primal Rage**: boot-stub redirect and audio-path fixes; both
  play in HLE and BIOS mode with headless audio RMS in envelope.
- **Hover Strike**: instant `CD_read` code-stomp lockup fixed — CD reads now
  stream at drive rate instead of completing instantly and overwriting
  in-flight polling code.
- **Baldies**: HLE boot progress reporting and CDI third-party-rip loading
  fixed (landmark-scan CDI global-data-offset detection, #233 — also
  resolved #230, see Known Issues).
- **Blitter**: fixed pixel-mode data reads to use channel 0's data
  registers (they wrongly read channel 2).
- **GPU**: indexed-load alignment now tests the effective address rather
  than the raw register mode.
- **OP**: GPU objects correctly halt the Object Processor until the GPU
  releases OBF, then resume at the next phrase (fixed Primal Rage's black
  bottom-third during fights).

### Performance / experimental

- **`virtualjaguar_dram_timing` (Experimental, default disabled)**: a
  deterministic DRAM/bus timing model — 68000 wait-states charged only for
  accesses that leave the local bus (DRAM/IO are free to the 68K, matching
  its own 8-sysclk bus cycle; only slow cartridge ROM stalls it), GPU
  DRAM-access timing extracted from the bus arbiter, and OP-fetch + DRAM
  refresh (MEMCON2 `REFRATE`) occupancy charged against the 68K. This is a
  physics model, not a game-specific hack — it's disabled by default because
  calibration against real hardware is ongoing. **Honest status on the
  "Doom runs too fast" bug**: this occupancy work closes approximately 0% of
  the measured gap. Real hardware renders Doom at 19.98 fps typical and
  ~15 fps in heavy scenes, while we sit pinned at the engine's own 29.97 fps
  two-field cap — **~1.5x too fast typical, 2x in heavy scenes**. Measured by
  attract-demo *duration* (the tic-rate metric used previously is saturated
  at that same 29.97 cap and cannot discriminate), the occupancy model
  lengthens demo 1 by +0.0% and demo 2 by +0.1%, i.e. within run-to-run
  noise. (An earlier +2.0% / +0.7% figure, quoted in #265, was measured
  against a bus model that double-counted the 68K's own bus cycle; that bug
  is fixed in this release.) Sensitivity sweeps identify the
  GPU and DSP per-instruction/per-access timing as the levers actually on
  the critical path — the 68000 is not. That work is scoped as the next step
  and tracked in `docs/doom-pace-calibration.md`; it did not make this
  release.
- GPU DRAM-access timing extracted into its own accounting path in the bus
  arbiter (#169), separate from the 68K's self-cost.
- Savestate layout updated (v7) to persist the new bus-occupancy accumulators.

### Testing

- Acid test suite reconciled: all three previously-regressed cases fixed and
  the CI gate's `pipefail` bug fixed so the job actually blocks on
  regressions going forward, not just reports them.
- New harnesses: Doom gametic-rate and audio-gap probes for bus-occupancy
  calibration (`doom_renderrate`, per `docs/doom-pace-calibration.md`).
- CD boot-matrix regression tooling (`cd_boot_matrix.sh`) hardened: rows are
  build-stamped so a resume never resurrects a stale row as a fresh result
  (this caught a phantom intermittent Battle Morph flake that was actually
  an old build's row).
- `cd_visual_verify` (per-second motion timeline, non-black coverage, audio
  RMS, periodic screenshots) and `cd_wedge_probe` (frozen-framebuffer catch
  + register/trace-ring/RAM dump) added for CD lockup triage without a
  device.
- Netlink test stack: `test_jlink`, `test_uart_loopback`, `test_uart_core`,
  `netlink_pair`, `test_jlink_netpacket`, and `netlink_ra_matrix.sh` against
  real RetroArch 1.22.2.

## Known issues

- **Netlink internet play is best-effort.** Only localhost and LAN play are
  validated; there is no relay or matchmaking service, and that is an
  explicit non-goal for now.
- **BattleSphere Gold networked play** is validated only to networked-lobby
  entry; a sustained multi-round dogfight has not been verified.
- **AirCars under netpacket (RetroArch netplay)** is untested — its
  interrupt-driven receive path doesn't exercise the same code path TCP mode
  does. TCP mode is fully validated for AirCars.
- **iOS/Provenance TCP netlink mode** has not been device-tested; validation
  so far is macOS/localhost only.
- **CDI images with a damaged/truncated boot header are refused** (#269) —
  affects a small number of V2-format third-party rips where the boot
  landmark itself is missing, not recoverable by offset correction. V3.5
  images and self-generated (`cue2cdi`) images are unaffected; 10/14 of the
  local CDI corpus pass.
- **Savestates from cores older than `STATE_MIN_VERSION` are rejected**
  (#268) — pre-v2 states from far older builds cannot be migrated. States
  from the v2.x line load normally.
- **Alien vs Predator**: a green-dot / green title-bar-area artifact is still
  unreproduced (#266); a red-background-behind-the-shotgun report (#267) is
  also open. Both are narrower follow-ups split from the original
  #178 umbrella, which is otherwise resolved.
- **FMV scene-jump drift**: Dragon's Lair, Space Ace, and BrainDead 13
  occasionally jump cutscenes early or late in HLE mode — their own
  delivery-clock counters drift relative to our transfer pacing. Reproducible
  and understood; not yet fixed. Real-BIOS mode is unaffected.
- **Doom** still runs its combat/monster pacing too fast (~1.5x typical, 2x
  in heavy scenes); this is a known, understood gap — see "Performance /
  experimental" above and `docs/doom-pace-calibration.md`. Doom's demo/in-game music silence is
  authentic to the original hardware, not a bug.
- **Iron Soldier (cart)**: the wireframe tank is still missing under the
  fast blitter (#186 class); the accurate blitter is unaffected.
- **Battle Sphere Gold**: menu text renders too dark to read. The glyphs
  arrive already dark from the game's own pre-shading path — the blitter has
  been exonerated — and the writer routine is still under investigation.
  Networked lobby entry is unaffected.
- **Tempest 2000** (#187) and the AvP items above are not independently
  device-verified this cycle; treat as believed-fixed pending confirmation.
- The accurate blitter remains notably slower than the fast blitter; some
  games may still need the fast blitter on lower-end hardware.
- No bus contention modeling beyond the experimental `dram_timing` option
  above; VC register behavior is not fully accurate.

## Compared to v2.3.2

181 files changed, +40,390 / -839 lines across 303 commits (246 non-merge;
the type tally below counts non-merge commits only): 108 fix, 50 docs,
27 test, 27 feat, 7 perf, 6 chore, 2 ci, 1 refactor, and 18 commits with
untyped or non-standard subjects.

## Downloads

Pre-built libretro cores for 16 platforms:

- Linux: x86_64, aarch64, i686
- macOS: arm64, x86_64
- Windows: x86_64, i686 (MSYS2/MinGW)
- iOS: arm64; tvOS: arm64
- Android: arm64-v8a, armeabi-v7a, x86_64, x86
- Web: Emscripten (bitcode for WASM builds)
- Consoles: PS Vita, Nintendo Switch

Each binary has a matching debug artifact (`*-debug.tar.gz`): split debug
symbols where the toolchain supports it (`.debug` / `.dSYM`); the Emscripten
target ships its bitcode directly. SHA256 checksums in `SHA256SUMS.txt`.

## Credits

Thanks to the community members who filed and helped diagnose issues fixed
this cycle, including reports that led to the Pitfall (#138), Tempest 2000
(#187), Alien vs Predator (#178), and CD compatibility (#230, #233) fixes,
and to everyone who reported and re-tested the Wi-Fi netplay lag.

This release builds on the original Virtual Jaguar emulator and its
contributors, and on hardware documentation cross-referenced against the
Jaguar Technical Reference Manual. CD boot-sequence and TOC-format details
were additionally cross-checked against the MiSTer Jaguar core and BigPEmu's
documented behavior where cited in `docs/`.

## Maintainers

libretro/Provenance fork: Joseph Mattiello (@JoeMatt, Provenance-Emu).
