# Dragon's Lair (bios) in-game "CD error" screen — RESOLVED

**State (2026-07-15, late):** root-caused and FIXED — the FIFO refill pacing
delivered ~201 KB/s vs the real double-speed drive's 352,800 B/s, and the
ReadySoft engine (Dragon's Lair, Space Ace — same abort signature device-
traced on both) dead-reckons the disc schedule against a GPU-timer clock,
declaring a read error when the stream slips behind. Fix: error-diffused
2.85-tick refill period (`FIFO_REFILL_PERIOD_X100`) matching the hardware
rate. Post-fix both titles stream FMV continuously through 4200-frame runs,
no error dialog, no watchdogs; full suite green; matrix re-run below.

The rest of this document is the diagnosis trail, kept for the method and
the decoded game/BIOS internals.

Follows from the
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

- Probe: a local `dlair_wedge.c` harness (PC_BAND_LO/HI env, dumps GPU RAM +
  2MB main RAM + CD trace at end). Run to `--frames 2820`.
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

## CD BIOS GPU ISR — data path decoded (from gpuram.bin)

IRQ1 vector `$F03010` → ISR body `$F03B1C`. Key facts:

- Mailbox in GPU RAM at `$F03B10`: `[0]=dest ptr, [4]=end ptr, [8]=saved
  initial dest`. Completion = dest past end → clears BUTCH ICR bit 0.
- DSARX branch (`$F03B3E`): consumes DS_DATA, **re-enables I2CNTRL bit 2**
  (matches the CD_read flow comments in cdrom.c).
- Data branch (`$F03B88`): reads **I2SDAT2 ($DFFF28) first, then FIFO_DATA
  ($DFFF24), alternating — 4 pairs = 8 longwords per invocation**; stores
  with pre-increment (`addq #4, r26; store`), so the first byte of caller
  data lands at mailbox[0]+4. The stale longword observed at `$3E2F0` is
  therefore *outside* the buffer — buffer-head corruption is RULED OUT.
  Delivered payload (`sector[2:]` at `$3E2F4`) matches the same capture-skew
  model that boots Primal Rage byte-exact.

## Transfer timeline of the failure (mailbox sampled per run endpoint)

The failing load requests ~1MB (end=`$13E000`). An earlier pass reached
dest=`$F6FB0` (~70%) before the game abandoned it and re-sought — so
segments *were* completing. The fatal cycle: seek 10 to MSF 3:40:52
(block 16402), stream runs at full FIFO pace (~200 KB/s, ≈1x; note real
drive is 2x = 300 KB/s), and after exactly 47 blocks (~110 KB, ~33 frames)
the game itself turns I2S off, sends STOP, interrogates disc status, and
shows the error dialog. The stream was healthy to the last tick. Post-error
the game retries the whole load forever (cycle ~335,768 ticks, identical
block trajectory each time).

So the game aborts a *flowing* transfer on some verification WE fail, not
on starvation and not on buffer content at the head.

## Open question (next step)

What check does the game run ~2 seconds into the seek-10 segment that makes
it declare a read error while data is flowing? Leading suspects, in order:

1. **Subcode Q position verification.** We do not emulate subcode at all —
   SBCNTRL/SUBDATA/SUBDATB/SB_TIME ($DFFF14/18/1C/20) are inert cdRam
   bytes, and BUTCH bits 2/3 (subcode frame / time-match interrupts) never
   fire. A game validating head position via subcode during long reads
   would read static garbage and conclude the drive is lost. Instrument:
   temp fprintf on CDROMReadWord/WriteWord for offsets $14-$20 + BUTCH
   enable-bit writes with bits 2/3 set, re-run to frame 2760.
2. **Transfer rate** — we deliver ~200 KB/s vs the real drive's 2x
   300 KB/s; a rate-based watchdog in the game could fire early. Check
   FIFO_FILL cadence constants in cdrom.c (FIFO_FILL_TICKS/
   FIFO_REFILL_TICKS) against real 2x timing before touching anything.
3. Find the 68K abort decision: the code that wrote I2CNTRL=$0001 + STOP
   at tick 1440021 (68K PC band $36xx is BIOS service code; the *caller*
   made the decision). Break on the I2S off-edge (I2S_CTRL trace) and dump
   68K PC/stack at that moment — one probe run.

### Progress so far on suspect 3 (the abort decision — decoded)

Subcode (suspect 1) is RULED OUT: instrumented run shows zero accesses to
SBCNTRL/SUBDATA/SUBDATB/SB_TIME and no BUTCH subcode-interrupt enables.

The game's transfer-wait/abort logic at 68K `$4C0C` (from mainram.bin):

    $4C0C: move.l $562E,d0     ; transfer progress counter
    $4C12: cmp.l  d0,d2        ; d2 = requested target
    $4C14: bgt.s  $4C0C        ; wait until progress >= target
    $4C16: tst.w  $565E / bne $4C30
    $4C1E: sub.l  d2,d0        ; overshoot = progress - target
    $4C20: cmp.l  $5660,d0     ; vs threshold
    $4C26: bmi.s  $4C4E        ; small overshoot -> OK
    $4C28: move.w #1,$565E ... ; else flag + bsr $93E2 (error path)

So the game errors on progress OVERSHOOT (or the error flag $565E), not on
a timeout. Readers of `$562E`: $4A7E (stability re-read loop + clr.l at
$4A8C), $4C0A (wait loop), $4D12 (progress-delta calc). Watchpoint run on
m68k_write_memory_32 + JaguarWriteLong ($562C-$562E) caught ONLY the clr.l
(pc $4A94) — the incrementer writes through an unhooked path. **Next hook:
m68k_write_memory_16 (word writes to $562C/$562E/$5630) and the blitter's
direct-RAM path; find who advances the counter and from what source
(GPU mailbox dest? block position?), then compare its update law against
what the game expects of a real 2x drive.**

Supporting instrument for any of these: a control-events-only trace mode
(e.g. `VJ_CD_TRACE=2` skipping FILL/DRAIN pushes) so one 256-entry ring
covers a whole retry cycle; fills/drains currently flood 16K-deep rings in
~2 cycles.

## Tooling added alongside this diagnosis

- CD trace ring `I2S_CTRL` event (data-enable edges) — in `f74ce09`.
- `VJ_HARNESS_LOG_INFO=1` lets INFO logs (CDTraceDump etc.) through the
  harness (`4504821`).
- `CDTraceDump` is NOT in `exports-test.list` patterns — probes can't call
  it; ring dumps only surface via the cd_seek_wedge watchdog. Consider
  adding `_CDTrace*` to the export list.
- Deep-ring diagnosis builds: temporarily set `CD_TRACE_SIZE` to 8192/16384
  in cdrom.c (default 256), build, copy the dylib aside, `git checkout`
  the file. Never rebuild in-tree while `cd_boot_matrix.sh` is running —
  the build-identity guard kills every subsequent matrix row.
