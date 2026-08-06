# Virtual Jaguar libretro v3.1.0

New-feature release: Jaguar GameDrive support, clock-scale (overclock) options,
audio-CD / Virtual Light Machine playback, and savestate compatibility restored
for every format any released core ever wrote — plus a substantially hardened
test suite that now covers the CD path in CI with no commercial discs.

## Highlights

- **Jaguar GameDrive (JagGD) cartridge support** (#312, #320). Detection plus
  full 16 MB bank switching (6×1 MB pages over 16 banks), implemented from
  RetroHQ's published GDBIOS bindings. GD-locked homebrew that previously hung
  forever now boots. New core option `virtualjaguar_jgd = auto` (default) /
  `enabled` (force, for GD-locked <6 MB images) / `disabled`. Also fixes a
  pre-existing loader buffer overflow for >6 MB images.
- **M68K / RISC clock-scale options** (#314, #316). Overclock or underclock
  each domain independently (`virtualjaguar_m68k_clock_scale` 0.5x–3x,
  `virtualjaguar_risc_clock_scale` 0.5x–2x) for framerate-limited titles.
  Defaults are bit-identical to previous behaviour; bus/DRAM costs stay in
  real time so the timing model is not distorted, and audio pacing is proven
  unchanged at every scale (no pitch shift).
- **Audio CDs play, and the Virtual Light Machine now works** (#291, #300,
  #325). Single-session audio discs no longer stall the BIOS, and the VLM
  unmutes on its own: its mute gate turned out to be the Q-channel
  CONTROL/ADR data-track flag, which we never served because BUTCH's subcode
  registers were unimplemented. Set `CD Boot Mode` to `Auto`/`Real BIOS` —
  audio-only discs have no session-2 boot stub for the HLE path.
- **Savestates: every released format loads again** (#268, #301). States from
  v2.2.0 / v2.3.0 / v2.3.1 / v2.3.2 were either rejected or — worse —
  silently mis-parsed from the CD chunk onward. All four released layouts now
  load exactly; the loader is exact, not best-effort. Savestate format bumps
  to v8 (JagGD state).

## Performance

- **Binary is ~22% smaller** (#321, #326). The UAE 68K core shipped two
  complete 68000 handler tables and dispatched only one; the linker now
  garbage-collects the dead table and its exclusively-reachable handlers.
  Makefile-only, per-platform gated, emulation bit-identical.
- **Accurate blitter ~16% faster on blit-heavy titles** (#332, #334). The
  SIMD helpers were reached through a function-pointer table across
  translation units with no LTO, so nothing inlined — 20.6% of runtime sat
  in five tiny leaf functions. Tempest 2000 +16.2%, other titles +6%.
  Bit-identical output (132,912 blits compared, zero differences).
- **MSVC x64/x86 and every Android ABI were silently falling back to the
  scalar blitter** (#333, #335) — they now get the SIMD path they should
  always have had.

## Bug fixes

- **AvP: red background behind the shotgun** with the accurate blitter
  (#267, #292) — DCOMPEN transparency now keys on the unshaded source pixel,
  so SRCSHADE can no longer make transparent pixels opaque.
- **CD: a redundant seek re-framed the in-flight stream** (#306, #307) —
  rewinding both the FIFO and SSI audio heads (up to ~13 ms of replayed
  CD-DA). Found by the new synthetic tests; pinned in both byte and audio
  domains.
- **External CD BIOS files are validated** (#296, #299) — a truncated or
  wrong-console file at the override path now falls back to the embedded
  BIOS with a log line instead of booting to a black screen. The developer
  CD BIOS dump is now accepted.
- **AvP overscan artefacts investigated and closed as hardware-faithful**
  (#266) — the green dot/bar lives at x≥320, where 67 corpus titles render
  real content; masking would damage them and miss the artefact.

## Accuracy / investigation record

- The FMV scene-jump investigation (#297) ran every hypothesis to ground with
  measurements now in `docs/fmv-drift-notes.md`: the presentation clock is
  video-field-locked (drift structurally impossible), transfer rates are
  within +0.15 % of hardware, FIFO scheduling charges no ISR latency, and a
  BigPEmu reference capture bounds seek latency to tens of ms. A reproducible
  Dragon's Lair scene-branch fixture ships in `test/fixtures/`.

## Testing

- **Three synthetic CD test suites** run in CI with zero commercial discs:
  HLE read delivery (#303), real-BIOS BUTCH/DSA/FIFO (#304), and CD-DA over
  the SSI on a multi-track chirp disc whose decoded audio identifies track,
  position, and channel (#309).
- **Silently-skipping checks eliminated** (#308): ten real-BIOS assertions
  and the Skyhammer clipping sentinel had been inert; the suite now prints a
  named skip ledger ("Skipped checks: none" on a full corpus) and
  `VJ_REQUIRE_ROMS=1` escalates skips to failures.
- **JagGD probe-ROM harness** (32 checks, synthetic) and non-1x clock-scale
  audio-contract rows run in CI.
- The netlink latency test no longer flakes under load (#310, #317) —
  absolute frame-ceiling assertions replaced a load-sensitive ratio.

## Stats

- 79 files changed, ~14,450 insertions, ~1,020 deletions across 30 merged
  PRs since v3.0.0 (`git diff --shortstat v3.0.0..develop`).

## Known issues

- Battle Morph (CD) runs too fast (#331) — most likely the unmodelled
  GPU/DSP pipeline hazards (#313); a reporter with real hardware has offered
  comparisons.
- Pipeline-hazard timing (#313) and dram_timing domain audit (#318) are
  tracked under the timing-accuracy epic (#319).
- See `docs/cd-known-issues.md` for the CD backlog.

## Downloads

Built for 16 platforms by CI on the `v3.1.0` tag.
