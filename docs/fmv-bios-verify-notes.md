# FMV CD titles — real-BIOS boot mode verification (evidence)

Date: 2026-08-07.  Core build: `v3.1.0 be95df3` (develop).  Harness:
`test/tools/cd_visual_verify` (headless; motion timeline + audio RMS +
periodic screenshots).  Host: macOS arm64.

## Why this sweep exists

Users report FMV titles broken in our core but fine in BigPEmu.  A prior
3000-frame sweep passed all 5 titles — but it ran **HLE** boot mode every
time: `cd_visual_verify --bios` only sets `virtualjaguar_bios` (cart boot
ROM), while for CD content the boot path is chosen by the core option
`virtualjaguar_cd_boot_mode` (`hle`/`auto`/`bios`, default `hle`), which
overrides it.  Real-BIOS mode was therefore never actually tested.

This sweep runs the real-BIOS path explicitly via
`--option virtualjaguar_cd_boot_mode=bios` and confirms per-run from the
core's own INFO log (`[BOOT] CD game, mode=BIOS (BIOS image staged)` +
`[CD] Boot path: REAL BIOS at $802000`) that the BIOS path was active.
All bios-mode runs used the embedded retail CD BIOS.

## Verdict summary

**Real-BIOS mode plays all 5 FMV titles correctly.**  Every title boots
through the CD BIOS animation, extracts its boot stub, reaches game code,
and shows sustained recognizable FMV imagery with audio.  No late
failures in 15000-frame (≈4 min) probes of Dragon's Lair and Space Ace in
either mode.  The 4 damaged V2 CDI rips fail identically in a
well-diagnosed way (missing boot header in the rip — see baseline below),
unchanged from the known `docs`/memory state.

## Main sweep — 3000 frames, `virtualjaguar_cd_boot_mode=bios`

Motion% = frames with >0.5% pixels changed / measured frames, whole run
(includes BIOS animation, loads, and static menu screens — FMV titles
idle on static cards between clips, so 100% is not expected).
RMS is the harness average over its recorded audio-frame window.

| Title | Mode | Verdict | Motion % | Peak nonblack | Audio RMS (nonsilent/total samples) | Watchdog lines | Evidence |
|---|---|---|---|---|---|---|---|
| Dragon's Lair | bios | **PASS** — FMV plays (Dirk + Singe scenes) | 64.1% | 79.5% | 1230 (1.72M/2.4M) | none | `shots/dragons_lair_bios_3000/frame_01500.png`, `_02700.png` |
| Space Ace | bios | **PASS** — FMV plays (Borf close-up) | 63.9% | 79.5% | 1753 (1.88M/2.4M) | none | `shots/space_ace_bios_3000/frame_02100.png` |
| BrainDead 13 | bios | **PASS** — reaches title/menu (castle-on-moon art), attract resumes | 20.5% | 78.5% | 2650 (2.05M/2.4M) | `cd_seek_wedge` f1107, `video_stall` f1529, `cd_seek_wedge` f2372 — all benign, see below | `shots/brain_dead_13_bios_3000/frame_01800.png`, `_02700.png` |
| Blue Lightning | bios | **PASS** — FMV plays (aircraft over desert) | 27.7% | 94.9% | 1258 (2.12M/2.4M) | `cd_seek_wedge` f986 — benign | `shots/blue_lightening_bios_3000/frame_02700.png` |
| Highlander | bios | **PASS** — intro FMV + title card ("HIGHLANDER (c)1994 Gaumont") | 25.5% | 98.2% | 1518 (1.85M/2.4M) | `cd_seek_wedge` f1755, f2716 — benign | `shots/highlander_bios_3000/frame_02700.png` |

Screenshot paths are relative to the sweep scratchpad
(`.../scratchpad/shots/`); PPM originals sit beside the PNGs, logs in
`.../scratchpad/logs/<tag>.log`.  Screenshots were visually inspected —
all show correct, recognizable FMV/title imagery, not garbage or black.

### Why the watchdog lines are benign

Every `cd_seek_wedge` line above reports `seek_starts == seek_dones`
(no in-flight transfer; the drain counter is parked at the end of a
completed read) — the known benign "CD idle >5 s after a finished
transfer" class documented in CLAUDE.md (Myst intro case).  Corroborated
against the motion timeline in each case:

- **BrainDead 13**: fires during its static title screen (67.6% nonblack,
  screenshot `frame_01800.png` shows the castle title card).  Motion
  resumes at win 036 (menu cursor blink) and win 044+ (attract).  The
  `video_stall` is the same static title screen — a real frontend shows a
  stable title card there, not a black screen.
- **Blue Lightning / Highlander**: fire between FMV clips while motion
  continues in later windows (Blue Lightning ends at 94.9% nonblack with
  steady 15/60 motion; Highlander at 98.2%).

No `gpu_pc_escape`, `dsp_pc_escape`, `gpu_wedge`, or `dsp_wedge` lines in
any run.

## Long-run probe — 15000 frames (~4 min 10 s emulated), both modes

Looking for late failures a 50-second run misses (wedges, black-outs,
motion stopping).  Screenshots every 1000 frames.

| Title | Mode | Verdict | Motion % | Watchdog lines | Notes |
|---|---|---|---|---|---|
| Dragon's Lair | hle | **PASS** | 61.5% | none | attract loops cleanly; recurring 2–3 s static card every ~3000 frames (same 30.9% nonblack each pass = attract-loop title card, periodic, not a stall) |
| Dragon's Lair | bios | **PASS** | 63.3% | none | frame 14000 screenshot = Dirk on the burning rope; FMV correct to the end |
| Space Ace | hle | **PASS** | 62.9% | none | clean to frame 15000 |
| Space Ace | bios | **PASS** | 63.0% | none | frame 14000 screenshot = Borf FMV; clean to frame 15000, RMS 1753 |

No late failure was found in any of the four long runs.

## V2 CDI baseline — 600 frames, both modes ("before" evidence for the rip-repair track)

These 4 CDIs are known-damaged V2 rips (boot header region missing from
the rip itself — see `project_cdi_v2_root_cause`): the exact current
failure signatures, for comparison after any fix.

| Title | Mode | Verdict | Exact signature |
|---|---|---|---|
| Iron Soldier 2 | hle | **FAIL — load rejected** (`retro_load_game` returns false, exit 1) | `[CD-BOOTSTUB] Magic mismatch at +0x42 of session-2 track BIN (matched 1/32 bytes)` → `[CD-HLE] Boot stub extraction failed` → `[CD-HLE] Parked 68K on halt loop at $00000400` → `[Virtual Jaguar] unsupported or invalid content format` |
| Iron Soldier 2 | bios | **FAIL — stuck in BIOS** (loads; BIOS animation + jingle only) | `[CD-BOOTSTUB] Magic mismatch at +0x42 ... (matched 1/32 bytes)` → `[CD-BOOTSTUB] CDIntfExtractBootStub failed` at ~frame 420; run ends parked on the red JAGUAR logo (`shots/ironsoldier2_bios_600/frame_00450.png`), peak nonblack 21.1%, audio = BIOS jingle (RMS 1960, 240573 nonsilent — byte-identical across all four titles) |
| Myst demo | hle | **FAIL — load rejected** | same chain, `matched 0/32 bytes` |
| Myst demo | bios | **FAIL — stuck in BIOS** | `CDIntfExtractBootStub failed`; BIOS animation still cycling at frame 600 (motion 78.3%), RMS 1960 |
| Vid Grid | hle | **FAIL — load rejected** | distinct message: `[CD-BOOTSTUB] Boot header region is zero-filled at +0x42 - this image is an incomplete / bad rip, not an unsupported format` → same failure chain |
| Vid Grid | bios | **FAIL — stuck in BIOS** | zero-filled-header message → `CDIntfExtractBootStub failed`; parked on logo, RMS 1960 |
| World Tour Racing | hle | **FAIL — load rejected** | `Magic mismatch at +0x42 ... (matched 0/32 bytes)` → same chain |
| World Tour Racing | bios | **FAIL — stuck in BIOS** | `CDIntfExtractBootStub failed`; parked on logo, RMS 1960 |

The identical audio stats (240573/480000 nonsilent, RMS 1960) across all
four bios runs are the CD BIOS boot jingle — direct evidence none of
them ever leaves the BIOS.

## Reproduction

```bash
ln -sfn "$JAGUAR_ROMS_PRIVATE" test/roms/private
DEVELOPER_DIR=/Library/Developer/CommandLineTools make -j8
cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
   -o test/tools/cd_visual_verify test/tools/cd_visual_verify.c \
   test/harness/harness.c -ldl -lm
VJ_HARNESS_LOG_INFO=1 ./test/tools/cd_visual_verify \
   ./virtualjaguar_libretro.dylib \
   "test/roms/private/Jaguar CD/CDI/dragons_lair.cdi" \
   --frames 3000 --outdir /tmp/shots --shot-every 300 \
   --option virtualjaguar_cd_boot_mode=bios
# confirm in the log: [BOOT] CD game, mode=BIOS (BIOS image staged)
```

## Conclusions

1. **Real-BIOS CD boot mode is not the cause of the user-reported FMV
   breakage** — all 5 FMV titles verify clean under it, including
   4-minute probes.  The prior HLE-only sweep gap is closed.
2. If users see FMV titles broken with **good rips**, the difference is
   elsewhere (frontend, platform build, image variant) — the next
   discriminator to collect from reports is the RetroArch log's
   `[CD-BOOTSTUB]` lines and crash-watchdog signatures.
3. The 4 damaged V2 CDIs fail identically in both modes with loud,
   specific log signatures (above).  Users with those rips get either an
   immediate load failure (HLE) or a BIOS logo that never advances
   (bios) — plausibly the actual source of "FMV titles broken, fine in
   BigPEmu" reports, since BigPEmu reads CDI headers differently.
