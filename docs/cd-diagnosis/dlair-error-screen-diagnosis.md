# Dragon's Lair (bios) in-game "CD error" screen — diagnosis in progress

**State (2026-07-15):** root cause narrowed, not yet fixed. Follows from the
I2CNTRL status-bit fix (`f74ce09`), which unwedged the end-of-transfer FIFO
flush loop shared by all bios-mode titles.

## Symptom

With `f74ce09`, Dragon's Lair (bios boot) streams five further transfer
cycles (seeks 2 → 10) with pixel-perfect FMV, then at ~frame 2760 shows its
own error handler: *"AN ERROR HAS OCCURRED WHILE ATTEMPTING TO READ FROM THE
CD..."* over intact game artwork, and retries the same load forever
(cycle length ~335,768 BUTCH ticks; identical block trajectory
16043 → 16368 → 16449 each retry).

Repro: `make cd-visual CD_VISUAL_DISC="<DL cue>" CD_VISUAL_FLAGS="--frames
3600 --option virtualjaguar_cd_boot_mode=bios ..."` — motion dies at window
046 (frame 2760), nonblack pins at 77.9% (the dialog).

## Evidence chain

- Probe: `scratchpad/dlair_wedge.c` (session scratchpad; PC_BAND_LO/HI env,
  dumps GPU RAM + 2MB main RAM + CD trace at end). Run to `--frames 2820`.
- The failing load: game seeks MSF 3:40:52 → block 16402 → track-3 file
  sector 1309 (layout: T1 3245 sectors, +11400 inter-session gap, T2 448,
  T3 starts at LBA 15093, INDEX 01 at +149 where the byte-swapped
  "ATARI APPROVED DATA HEADER" sits — layout self-consistent).
- Transfer succeeds mechanically: 47 blocks (16402→16449) stream steadily,
  land contiguous + byte-swap-corrected at RAM `$3E2F0` (a0 in the game's
  validation code points there).
- **Buffer content is wrong at the head**: RAM = `4B4B4B4B` +
  `sector1309[2:]`. I.e. first stored longword is stale (matches raw bytes
  found at track-3 sector 1288, the abandoned pre-seek stream region), then
  the stream is delivered one 16-bit word short (the `cdBufPtr = 2` capture
  skew from `09a62d6`).
- The earlier *successful* long stream (sectors 981–1160+, RAM `$59BF2`) is
  **byte-0 aligned** — but that consumer is the FMV player, which self-syncs
  on stream marks; the failing consumer is a structured file load that
  validates the buffer head.

## Ruled out

- Data starvation / FIFO flow (steady fill/drain right up to the abort).
- DSA response loss (all commands answered; error handler's own
  STOP/$5000/$0300 dialogue works).
- The 68K flush loop's stale reads (they discard into d1, never stored).
- **Force-primed `fifoDataReady` in DS_DATA read paths** (queue-pop $0100 /
  Play echo / $12xx fallback): gating these on I2CNTRL bit 2 was implemented
  and tested — buffer head unchanged, error persists. Reverted (do not
  re-ship without independent justification).
- HLE comparison is unusable: DL in HLE mode black-screens with silent audio
  (separate pre-existing failure).

## Open question (next step)

Who writes the first longword (`$4B4B4B4B`) of the destination buffer, and
what does real BUTCH deliver in the first FIFO entry after a mid-session
seek? Candidates:

1. The CD BIOS GPU ISR's data loop stores one register of residue before the
   fresh stream (disassemble the data-mode ISR from the probe's
   `gpuram.bin`; the sentinel-scan loop is at `$F03234`, data path nearby).
   Check LOADP/HIDATA pairing on the FIFO ports.
2. The capture-skew model (`09a62d6`) may be incomplete for stream restarts:
   real hardware pairs the seeked sector's word 0 with prior residue in the
   first 32-bit entry instead of dropping it. If so the fix is to deliver
   `[residue, w0], [w1, w2]...` rather than dropping w0 — same sentinel
   alignment (scan is word-tolerant), but no 2-byte payload loss.
3. Find the game's validation routine (68K, buffer at a0=$3E2F0 in the
   final state of the 2820-frame run) and read what it compares — decides
   between (1) and (2).

## Tooling added this session

- CD trace ring `I2S_CTRL` event (data-enable edges) — in `f74ce09`.
- `VJ_HARNESS_LOG_INFO=1` lets INFO logs (CDTraceDump etc.) through the
  harness (`4504821`).
- `CDTraceDump` is NOT in `exports-test.list` patterns — probes can't call
  it; ring dumps only surface via the cd_seek_wedge watchdog. Consider
  adding `_CDTrace*` to the export list.
- Deep-ring diagnosis builds: temporarily set `CD_TRACE_SIZE` to 8192/16384
  in cdrom.c (default 256), build, copy dylib to scratchpad, `git checkout`
  the file. Never rebuild in-tree while `cd_boot_matrix.sh` is running —
  the build-identity guard kills every subsequent matrix row.
