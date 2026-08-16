# Jaguar CD — known issues and future work

Status as of the `feature/jaguar-cd-support` wrap-up (2026-07-27, post
v2.3.2 merge). Every title in the local library boots to game code in HLE
mode; this file lists what is deliberately left for a future point release.
The regression floor is `docs/cd-boot-matrix.md` — no change may move a row
backward.

## Deferred to a future point release

### 1. ~~Real-BIOS mode depth for FMV-heavy titles~~ -- RESOLVED (measurement artifact)

**Not a defect. Withdrawn 2026-07-29.** This item claimed BrainDead 13,
Dragon's Lair and Primal Rage "park at `BIOS_INTRO` in real-BIOS mode"
and that the BUTCH/FIFO/DSA path needed more fidelity for their handoff.
All three in fact hand off and play in bios mode; the `BIOS_INTRO` label
came from `cd_boot_matrix.sh`'s stage classifier, which treated the whole
`$004000`-`$007FFF` band as BIOS code.

The band is shared: the real CD BIOS does execute there before handoff
(`src/cd/jagcd_bios.c` hooks its GPU-auth path at `$005E40`), but games
link code there too -- Dragon's Lair's boot executable loads at `$004000`
and Myst's at `$005000`, and BrainDead 13 / Primal Rage load a
second-stage player there after their own exes land at `$124000` /
`$080000`. The rule's only cited provenance was a single ISO row
(`final_pc=$0059B0`), and ISO loads are now refused outright as
unbootable (see "Explicitly out of scope" below), so it rested on a
known-broken run.

The classifier now gates that band on the `[CD-BOOTSTUB] Injected`
handoff marker, and all three rows read `GAME_CODE`. Full diagnosis --
per-title disassembly of what each PC was actually executing, the
measured 30 Hz / 24 Hz FMV presentation clocks, and the screenshots --
is in the 2026-07-29 re-run notes in `docs/cd-boot-matrix.md`.

Consequence for `docs/cd-bizhawk-comparison.md`: its three ranked
candidates (achieved FIFO delivery rate, the discarded `$15nn` speed bit,
the non-hardware serialization constants) were all ranked to explain a
gap that does not exist. Its own prescribed first step -- disassemble the
loop rather than correlate against it -- is what settled it. The `$15nn`
speed-bit finding (§4.2) and the `m68ki_init_exception` supervisor-stack
bug (§4.1) remain genuine, separately-filed correctness bugs; neither is
needed for these titles.

Candidate 2 (the discarded `$15nn` Set Mode payload) is independently
falsified for these titles by a Set Mode census across 10 titles in both
modes: BrainDead 13 and Dragon's Lair emit only `$150A` and never leave
double speed at all, and Primal Rage's single-speed request (`$1501`)
comes only *after* its boot read completes. Baldies and Hover Strike do
exercise the manual's §2.6 double -> single -> double recovery flow, and
both already reach `GAME_CODE`.

Beware the wire encoding if you touch this: the `$15nn` payload is **not**
a bit field for speed. The retail BIOS at `$808978` does
`move.w d0,d2 / and.w #$1,d2 / add.w #$1,d2`, i.e. a **one-based** speed
code (1 = single, 2 = double), then `bset #3,d2` for data mode, then
`or.w #$1500,d2`. So `$150A` is double-speed + data, not single speed, and
`$1502` (double + audio) is unrepresentable under a bit-0 reading. The
expected reply is `$1700 | payload` (`bset #9`), which
`src/cd/cdrom.c:1641` already synthesizes correctly.

### 2. ~~FMV scene-jump schedule drift (Dragon's Lair / Space Ace / BD13)~~ -- RESOLVED (#297: every lead run to ground; seek model calibrated to reference)

**Closed 2026-08-05.** The investigation is in
[`fmv-drift-notes.md`](fmv-drift-notes.md) (§2–§11); the summary of where
each hypothesis ended:

- **"Schedule drift" is structurally impossible.** Dragon's Lair's
  presentation counter is at `$562E/$5630` (not the `$129AD6/$129ADE`
  this note originally cited) and it is **video-field-locked**: +32760
  per field, zero jitter, identical histogram at 1x/2x/4x read speed
  (§2).
- **Transfer rate is exonerated.** Both paths deliver within +0.15% of
  the 352,800 B/s hardware 2x rate; the earlier "-1.11% slow /
  `fifoFillDelay` re-arm" claim was refuted by direct measurement — the
  fill→drain latency it blamed is zero for 99.997% of drains (§9, #305).
- **A real stream-corruption bug was found and fixed on this path**: a
  redundant `$12xx` seek re-framed the in-flight FIFO/SSI stream,
  replaying up to ~13 ms of CD-DA (#306, fixed #307). It may well have
  been the visible "glitchy transition" in the original report.
- **Seek latency — the last open lead — is now modeled and calibrated.**
  A BigPEmu reference capture of Dragon's Lair's death branch (558 ms ≈
  33.4 fields of black vs our 29–30) showed the missing term is ~60 ms
  on a near-full-stroke seek — tens of ms, **not** the unsourced
  "30–315 ms multi-tier" range this file used to cite (its ~300 ms top
  end would have shown as ~+18 fields; the capture shows +4).
  `CDROMSeekDistanceTicks()` (src/cd/cdrom.c) now charges 1 halfline
  tick per 72 sectors of head travel in **both** boot modes; the
  measured branch gap lands at 33 fields in both, matching the
  reference within a field (§11).

The reproducible branch fixture (`test/tools/run_dl_branch_fixture.sh`,
#311) plus long-run cycle checks pin the behaviour: the die/retry loop
repeats with a constant period and identical LBA sequence over 6+
consecutive cycles in each mode. Standing caveat: the seek constant is
**reference parity with BigPEmu, not silicon ground truth** — if a
hardware capture ever materializes, recalibrate `SEEK_SECTORS_PER_TICK`
against it (one number, linear model, no tiers).

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
- **`.iso` support (removed)**: a bare 2048-byte-sector ISO cannot
  represent a Jaguar CD — the format requires the multi-session layout
  (session 1 audio warning, session 2 data recorded as audio-type
  2352-byte tracks, byte-swapped) and the track lead-in offsets, none of
  which survive an ISO rip. No retail game can ever start from one
  (BigPEmu declines ISOs for the same reason), so the loader refuses
  `.iso` outright with an explanatory error instead of raising false
  hopes with a BIOS screen that goes nowhere. Use CUE/BIN, CDI, or a
  CHD created with a chdman that writes CHSE session tags (see
  `docs/jagcd-chd.md`).
- **Old / internet CHD rips**: CHD is supported, but files made before
  MAME PR 15886 (no `CHSE` session metadata) flattened Jaguar CD's two
  sessions and are refused at load. Reconvert from CUE/BIN with
  `tools/jagcd`. Virtual-pregap CHDs warn and still load (HLE is fine).
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


## Known bad CDI dumps

Some DiscJuggler CDI V2 rips in the wild lack a valid
`ATARI APPROVED DATA HEADER ATRI ` magic at session-2 track `+0x42`
(after the I2S word-swap). The CDI walk/offset math is correct; the header
data is simply wrong or absent. Load is **refused** with an actionable
`[CD-BOOTSTUB]` log line (warn-and-refuse — we do not tolerate truncated
ATARI magic, which would risk false-positive boots).

Measured against the local 14-CDI corpus (10 load, 4 fail):

| Local image | Defect at `+0x42` (word-swapped) | Log signature |
|---|---|---|
| vidgrid | **zero-filled** (32/32 zeros) | `Boot header region is zero-filled` |
| ironsoldier2 | garbage / title residue (`matched 1/32`) | `matched N/32 bytes` |
| mystdemo | non-magic garbage (`matched 0/32`) | `matched 0/32 bytes` |
| worldtourracing | non-magic garbage (`matched 0/32`) | `matched 0/32 bytes` |

Re-dump from a known-good source (or use CUE/BIN) rather than filing this as
an unsupported-format bug. See issue #269.
