# Jaguar CD — known issues and future work

Status as of the `feature/jaguar-cd-support` wrap-up (2026-07-27, post
v2.3.2 merge). Every title in the local library boots to game code in HLE
mode; this file lists what is deliberately left for a future point release.
The regression floor is `docs/cd-boot-matrix.md` — no change may move a row
backward.

## Deferred to a future point release

### 1. Real-BIOS mode depth for FMV-heavy titles

BrainDead 13, Dragon's Lair, and Primal Rage park at `BIOS_INTRO` in
real-BIOS mode (all three play in HLE mode). The real BUTCH/FIFO/DSA path
needs further fidelity for their boot handoff. Evidence and per-title logs
in the boot matrix; the BIOS RAM-driver disassembly technique that cracked
the Myst fixes (snapshot `$3000-$3DFF` from a live run) is the recommended
tool.

### 2. FMV scene-jump schedule drift (Dragon's Lair / Space Ace / BD13)

FMV titles occasionally jump scenes early/late: the games' own
delivery-clock counters drift relative to our transfer pacing (root-cause
notes: DL clock at `$129AD6/$129ADE` advances +7680/s; subcode registers
exonerated). Reproducible via RAM-snapshot counter comparison. Medium
effort; HLE-mode only polish.

### 3. Myst BIOS-mode audio parity listen

Myst plays fully in both modes (video + audio; headless RMS in envelope
both sides). A human RetroArch listen is the final sign-off that the
soundtrack is musically correct — headless stats cannot distinguish music
from structured noise at the right level.

## Explicitly out of scope for this branch

- **Per-title defaults DB and enhancement hooks** (BigPEmu-parity ideas):
  planned as a separate feature off `develop`.
- **BigPImage (.bpi) support**: closed format, no public spec. CDI is the
  interchange format (see `test/tools/cue2cdi` and its README).
- **CD read speeds above 2x on the real-BIOS path**: the
  `virtualjaguar_cd_read_speed` option is HLE-only by design; scaling the
  BIOS path's FIFO/DSA cadence re-opens the DSA-steal and FIFO-storm race
  classes those constants encode.

## Watchdog notes for triage

- `cd_seek_wedge` fires benignly when a title goes CD-idle >5s after a
  transfer (e.g. Myst's intro-movie black pause). Corroborate with
  `cd_visual_verify` before treating it as a hang (see CLAUDE.md).
- Boot-matrix rows are build-stamped; a row whose stamp does not match the
  current build is re-run, never trusted (stale-row resurrection produced
  the phantom "Battle Morph bios pc_escape flake").
