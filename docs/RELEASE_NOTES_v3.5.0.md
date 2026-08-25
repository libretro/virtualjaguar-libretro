# Virtual Jaguar libretro v3.5.0 — Texture packs, controllers, and performance

Every Jaguar controller Atari shipped (and one it never did) is now emulated,
community HD texture packs can replace a title's own blitter art, the Voice
Modem can carry an actual voice, and the core can finally be profiled **on
the device** instead of only on a developer's desktop.

---

## Highlights

### Texture replacement packs (#369, #528)

Present community HD art in place of a title's own blitter tiles.

- **Texture Dump Mode** (`virtualjaguar_texture_dump`) writes every unique blitter
  source tile a title uses to `<system>/vj_texdump/<cart CRC32>/` as a PNG, for
  pack authoring. Tiles are identified by a hash of their raw source bytes only —
  the palette is advisory metadata, never identity, because the Jaguar blitter
  never sees one.
- **Texture Replace** (`virtualjaguar_texture_replace`) presents packs from
  `<system>/vj_texpacks/<cart CRC32>/<hash>.png`, and the option is shown only
  when a pack exists for the loaded title. Packs are true colour; a pack pixel
  with alpha below 128 falls through to the stock pixel instead of covering it.
- **Tier 1** (16bpp source tiles at 1x through straight-copy blits) and **tier 3**
  (pack art on the >1x internal-resolution shadow surface) both ship this
  release. Tier 3 required a parallel per-page replacement plane alongside the
  existing hi-res shadow surface, since pack RGB isn't a function of the stock
  CRY word the hi-res path stores. A follow-up in the same cycle wired the
  RGB16-direct presentation path (packs previously didn't show there at any
  resolution) and stopped static tiles — a HUD, a menu, a title card left on
  screen — from ageing out to flat colour after ~16 frames.
- **Tier 2, indexed (≤8bpp) sources, is still not implemented.** Entries of that
  kind are skipped and the stock pixel presents.
- Presentation-only by construction: **the emulated machine, savestates and
  netplay are bit-identical with or without a pack** — replacement rides the
  existing true-colour shadow framebuffer, gated on a per-pixel straight-copy
  witness, and never writes anything the emulated machine can observe.

### Every Jaguar controller (#513, #514, #538, #505)

A complete pass over the input hardware:

- **Team Tap** (#513) — the 16-row-select decode for the 4-player adapter, plus
  the socket-3 detection diode and the interleave-immunity rules from TR10.
- **Pro Controller** (#514) — the six-button pad's five extra buttons alias onto
  existing standard-matrix keypad slots per Atari's own SDK header; the manual
  never documents the device at all, so this ships as a core-option preset with
  no published detection method to test against.
- **6D flight stick** (#538) — six degrees of freedom (X/Y/Z + TX/TY/TZ), seven
  buttons plus `Rezero`, three banks of the same bank-switching wire protocol as
  the analog/driving controller, built entirely from Technical Reference V10.
  **This one is a best-attempt, not a verified implementation**: no released or
  homebrew software using it is known to exist, and the one available real-bus
  validation tool (darthcloud's AtariJaguarPadtest) turns out to hardcode socket
  0 to a standard pad whenever no Team Tap is present, so it cannot exercise the
  6D decoder at all. The docs and the core-option help say this plainly.
- **Paddle interface** (#505) — the `$F17C00` motherboard 8-bit ADC (ADC0844),
  present only on early Jaguar boards and deleted from production silicon.
  BattleSphere / BattleSphere Gold's analog-stick option and Club Drive's
  channel-select both read/drive it; the protocol is pinned by cross-checking
  both titles' code against the ADC0844 datasheet, since no register spec for
  this interface exists.

**Team Tap was subsequently validated through the real bus** against
darthcloud's published AtariJaguarPadtest ROM — not just at the register level
— and fully corroborated end-to-end. The 6D controller could **not** be
corroborated this way, for the reason above; it remains register-level-tested
only.

### Voice chat over the Jaguar Voice Modem (#485)

v3.4.0 shipped Ultra Vortek's data protocol over netlink; this release adds an
actual voice path so a "phone call" carries voice, not just game state:

- **Host-side capture** rides the netlink discovery socket for direct/TCP links.
- **RetroArch netplay** gets its own voice-chat path over netplay's own
  transport (#585).
- A same-cycle follow-up fixed **four independent gates** that silently kept
  voice from ever activating: `read_mic` returns all-silence unless a full
  request is satisfiable, so a fixed 20 ms pull request never filled from one
  60.054 Hz field's worth of samples; Hello negotiation gave up after 5 s
  exactly when a host was waiting for a peer; the mic interface was probed once
  before RetroArch's mic driver was necessarily up, latching "no mic" for the
  whole run; and transmit was gated on link mode being `auto` specifically.
  Every failure mode was also silent — the log now names which gate applied.

### Netlink wire-speed: negotiated auto (#552)

v3.4.0's opt-in 2x/4x wire-speed enhancement (#498) required both sides to pick
the same value by hand. It's replaced this cycle by a single `auto` mode: the
two instances negotiate the one available divisor out-of-band at link-up over
the existing LAN-discovery UDP socket, and either side falls back to stock the
instant its peer rejects the request, doesn't understand it, or never answers.
Still **not authentic** — a real Voice Modem or JagLink cable is exactly this
slow — and still off by default.

### Widescreen aspect-ratio toggle (#530)

`virtualjaguar_widescreen` (default off) reports 16:9 to the frontend instead
of the Jaguar's native 4:3, for a cosmetic horizontal stretch. The Jaguar has
no wider-viewport hardware mode, so this is presentation metadata only — the
emulated framebuffer is bit-identical either way. Global option only; no
per-title gating yet, since no corpus evidence exists for which titles look
acceptable stretched.

### Per-title known-bad entries (#464)

`titledb` could previously only opt a title *in* to a setting. It now carries a
`negative[]` class that refuses an unsafe per-title **default** with a warning
while never overriding an explicit user choice. Ships with **zero rows** —
this is the mechanism, not the data. The motivating case, Cybermorph under RISC
overclock (#463), stays `blocked`: headless investigation came back clean and
the deciding experiment hasn't run.

---

## Performance (epic #569)

A profiling pass across the whole pipeline, prioritized by where the time
actually goes rather than by intuition:

- **RISC idle-loop fast-forward** (`virtualjaguar_risc_idle_skip`, **off by
  default**) — the DSP and GPU interpreters spend 74–99.7% of every frame inside
  a 3–5 instruction wait loop while still fully interpreting it. A tightly
  gated extrapolation (whitelisted loop shapes only, verified fixed-point across
  three snapshots) skips the redundant iterations. It's disabled by default
  because it's turned off entirely under any timing model, non-stock RISC
  clock, blit memoization, single-step, or an armed trace/watchpoint — each adds
  per-instruction state the extrapolation doesn't model — and hasn't yet earned
  default-on status. A/B verified byte-identical (framebuffer, audio, periodic
  savestate digest) across seven titles including two CD games.
- **Build flags**: ELF targets now build with `-fvisibility=hidden` and
  `-fno-semantic-interposition` (fewer GOT indirections per global in hot
  loops), Android's `Android.mk` now inherits real optimization flags, and
  `classic_armv7_a7`'s `-Ofast` now actually takes effect — it had been silently
  overridden to `-O2` by a later `-O` flag on the compile line since the
  platform block was added (#516).
- **Fast blitter routing** through the inline RAM helpers, cutting redundant
  `JaguarRead*`/`Write*` dispatch calls per pixel.
- **RISC interpreter quick wins** — one-call long reads in `GPUReadLong`, and
  single-shift `SH`/`SHA` instead of 32-iteration loops.
- **Event scheduler** now packs live events instead of scanning all 32 slots per
  dispatch.
- **68K hook-chain collapse** — the CD-HLE/Memory-Track/boot-strategy hook chain
  is skipped entirely when none of those subsystems are active.
- **TOM BG line-buffer clear** widened from byte to word stores.

### On-device profiling (#510)

`RETRO_ENVIRONMENT_GET_PERF_INTERFACE` is now wired, with scoped counters
around the real per-frame consumers (68K, GPU, DSP, OP halfline render,
blitter launch, DAC/audio mix). This matters because every profiling tool this
project had was host-side; none of it reaches a locked-down tvOS device.
Gated at runtime on a null callback and proven inert when the frontend
declines. Two RetroArch traps were found and documented in the process:
`perfcnt_enable` off means every counter reads zero (indistinguishable from
broken instrumentation, not "off"), and RetroArch itself accepts the
environment call unconditionally, so ~2,000 indirect calls/frame happen during
ordinary RetroArch play — negligible, but not the never-taken branch the
header originally claimed for a declining frontend.

**The capture tooling for an A10X measurement shipped this cycle (#563), but
the on-device capture itself has not been run.** Desktop profiling shows GPU
RISC interpretation at ~74% of instrumented time against the blitter's ~17%,
which is the current best guess at where a real optimization pass should
start — but #509 (the epic this all serves) stays open until that number is
confirmed on the actual hardware it's about.

### Purpose-built microbenchmark ROMs (#536)

Five deterministic, engine-isolated benchmark ROMs (68K-only, GPU
arithmetic-loop, GPU branch-heavy-loop, DSP-loop, blitter fill-throughput),
each asserted in CI so a performance regression on one engine shows up without
the noise of a full game trace. Replaces `yarc.j64` as the default
`make benchmark` target, whose GPU spends 73% of its time in a spin loop and
was measuring the wrong thing.

---

## Bug fixes

- **Club Drive `SIGABRT` at `risc_clock_scale=2x`** (#565) — `OPProcessFixedBitmap`'s
  REFLECT-mode pixel loop walked backward through `currentLineBuffer` with no
  lower bound; a garbage object-list phrase drove the write pointer ~3 KB
  behind `tomRam8[0]` and corrupted an unrelated heap pointer, aborting on the
  next `free()`. The crash log's `dsp_pc_escape` was a red herring — a harmless,
  already-handled `-1` PC from a delay-slot write. Fixed by clamping all six
  `OPProcessFixedBitmap` pixel-store paths to TOM's actual line-buffer bounds.
- **World Tour Racing CD FMV freeze, part one** (#564) — HLE's `CD_read` wrote a
  synthesized status word into the same GPU RAM address range where WTR uploads
  and runs its own FMV decoder, clobbering a live jump-table slot mid-execution.
  The GPU stayed busy afterward (no crash signature) but never produced another
  frame. Fixed; FMV now sustains motion through frame ~660 where it previously
  froze at ~240. **A second, mechanistically distinct freeze remains around
  frame 660** — see Known issues (#589).
- **Android CUE/BIN case-sensitivity** (#566) — a CUE sheet's `FILE` line and the
  extracted `.bin` filename can differ only in case; NTFS resolves that
  silently, Android's case-sensitive storage does not. Adds a case-insensitive
  directory-scan fallback for the CUE-referenced path.
- **RPi / cross-compiled ARM64 shipped the scalar blitter, not NEON** (#560) —
  the published `platform=unix` aarch64 buildbot binary (what a Raspberry Pi
  user actually installs) never set `ARCH`, so it silently fell through to
  scalar despite NEON being architecturally mandatory on that target. `ARCH` is
  now derived from the cross-compiler triple; a regression guard
  (`simd_matrix_check.sh`, 22 rows) is wired into `make test`. Android's own
  build (`Android.mk`) was never actually affected — a false alarm in the
  original report.
- **Per-SoC Raspberry Pi tuning** — `-mcpu`/`-O3` set per RPi target now that the
  SIMD selection above is trustworthy.
- **Netlink device-mismatch warning was dead** on the `vj_netlink.txt` and
  `VJ_NETLINK_HOST` paths (#501) — it compared against the raw option value
  instead of the resolved peer address.
- **Missing `C_JERENA` gate** on the DSP's own CPUINT raise path (#499),
  closing out the same interrupt-gating class v3.4.0 fixed for the ASI path.

---

## Testing

- **Voice modem over libretro netpacket** now has automated end-to-end
  coverage (#494).
- **Team Tap validated through the real bus** against AtariJaguarPadtest —
  closing a blind spot the 6D work itself flagged: register-level tests call
  the decode entry point directly and structurally cannot see a
  frontend-shaped defect. (The 6D controller could not be validated this way;
  see Highlights above.)
- **Tier-1 private ROM store** — ROM-gated tests that previously skipped
  silently can now fetch their corpus from a GitHub release asset or S3/R2
  backend.
- **Deterministic A/B in CI** (#534) — perf changes are now measured by
  cachegrind instruction counts instead of wall-clock time, which is noise on
  shared CI runners.

---

## Documentation

- **`docs/jtrm-*.md` provenance corrected** (#522) — 21 of 23 `Source:` lines
  cited *source code* rather than the manual, making the "these supersede
  source comments" claim circular. Every line is now labelled honestly, and
  the load-bearing clock/timing and interrupt sections carry real manual page
  citations. Two real doc/manual disagreements were found and recorded rather
  than silently patched.
- **True-colour A/B capture recipe** (#506) — a deterministic pipeline (same
  frame, same input, one option changed) plus the measured result that only
  Cybermorph shows a visible difference among the titles sampled.

---

## Infrastructure

- **Windows `chdman` CI build ~2.2x faster** via ccache plus a Windows Defender
  exclusion on the build directory. Not user-facing, but worth a line — it was
  a meaningful chunk of every Windows CI run.

---

## Known issues

- **World Tour Racing freezes a second time**, around frame ~660 in HLE CD
  mode, after the fix above resolves the first freeze. Preceded by an unusual
  `CD_read` (non-ASCII tag, misaligned destination, targeting a disc region no
  prior FMV chunk touched) and confirmed mechanistically distinct from the
  first freeze. Tracked in #589.
- **On-device performance work is not finished.** #510 delivered the
  measurement plumbing and #563 delivered the capture tooling; the actual
  per-subsystem millisecond breakdown on an A10X has not been captured, and
  the optimization phase is deliberately blocked on it. Tracked in #509.
- **`build-Linux i686` can still go red** on `test_frontend_pacing`'s
  fastest-frame assertion — that runner's throughput has degraded versus
  historical measurement. The assertion now prints CPU steal and load/core on
  every run so the next failure is evidence rather than a guess. Tracked in
  #421.
- **6D flight-stick support is unvalidated against any real program** — see
  Highlights. Two ambiguities in Technical Reference V10 itself (an axis sign,
  three torque signs) are resolved by best guess, not derivation.
- **titledb's negative-entry mechanism ships with no data** — Cybermorph under
  RISC overclock (#463), its motivating case, remains unresolved and `blocked`.
- **Same-host netlink auto-negotiation usually will not confirm** when two
  cores share one machine's discovery port. It fails safe — both sides run
  stock — and cross-machine play is unaffected.
- **Defender 2000 still hangs at level start under `m68k_clock_scale=3x`**
  (#460) — root-caused this cycle to a GPU task-dispatcher race (the 68K
  outruns the game's own handshake and the GPU consumes an unfilled descriptor
  slot) but not fixed. 2x and below are clean in every tested pairing; anyone
  hitting this should drop the 68K overclock to 2x.

---

## Stats

```
186 files changed, 30476 insertions(+), 2199 deletions(-)
184 commits since v3.4.0
```

## Downloads

CI publishes builds for 16 platforms with this release. Nightly builds from
`develop` are available under the rolling `nightly` prerelease — note those are
gated on *compiling*, not on the test suite.

## Maintainers

Joseph Mattiello, with the Virtual Jaguar libretro contributors.
Original Virtual Jaguar by David Raingeard (Potato Emulation) and
James Hammons.
