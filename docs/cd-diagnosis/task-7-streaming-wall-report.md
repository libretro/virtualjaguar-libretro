# Task 7 report — the streaming wall: FIFO longword grouping phase (+ drain starvation)

Branch `feature/jaguar-cd-support`, main checkout.
Fix commit: `09a62d6 fix(cd): start FIFO word stream one 16-bit word into the sector`
(committed by the user from my in-flight tree; content verified == intended fix, no stray
instrumentation). Follow-up commit (this report + refreshed matrix rows): see below.

## Outcome

**The wall is broken for the sentinel-scan class.** Primal Rage (bios) completes its
first boot-stub load — byte-exact game code at $4000 — and advances to its *second*
CD_read; Baldies (bios) reaches **GAME_CODE** (final_pc=$060106, in its loaded region).
Highlander / IS2 (bios) did not move — their stall is a *different, earlier* mechanism
(FIFO drains freeze at 54/0 before any sentinel scan; see Concerns).

## Which hypothesis held

**None of H-LBA / H-SWAP / H-FRAME as stated — the failing transform was one level
deeper: the 32-bit FIFO entry *grouping phase* over a correct, correctly-swapped,
correctly-positioned word stream.** (Closest in spirit to H-FRAME.)

Eliminations, in evidence order:

1. **H-LBA rejected.** Both HLE (oracle, boots to GAME_CODE) and BIOS mode compute the
   *identical* first seek: Primal Rage `$10 1A / $11 04 / $12 41` → MSF 26:04:65 →
   block **117215** (trace: `SEEK_START value=$1241 block=117215` vs HLE
   `HLE_READ block=117215`). Baldies likewise (block 20948 both modes).
2. **H-SWAP rejected.** Instrumented FIFO word deliveries at the sync sector,
   reassembled in pop order, read `"-Track-Maker-Developed-Distinctive-…"` then
   `$4444 $4C39 ×16` — the exact I2S-unswapped disc bytes. The word stream is right.
3. **The delta:** the CD BIOS GPU ISR (dumped from GPU RAM, disassembled with a
   purpose-written RISC disassembler) scans FIFO *longwords* for **16 consecutive
   entries equal to the D1 sentinel** ($F03240 `load (r27),r28; cmp r28,r24;`
   mismatch → `moveq #16,r26` reset; $F03248 `subq #1,r26` → 0 starts the DMA), with
   the counter persisted in the state block at $F03118 (+0 dest-4=$3FFC, +4 end=$5FC00,
   +12 counter, +16 sentinel=$44444C39='DDL9').
   Brute-forcing the transform space over the real BIN bytes at the sync mark:

   | byteswap | phase | order | max consecutive sentinel longwords |
   |---|---|---|---|
   | I2S-unswapped | word 0 | straight | 0 |
   | I2S-unswapped | word 0 | reversed | 15 |
   | **I2S-unswapped** | **word 1** | **straight** | **16 — exactly the ISR's requirement** |
   | I2S-unswapped | word 1 | reversed | 0 |

   The sync mark sits at byte offset ≡ 2 (mod 4) on every checked disc
   (Primal Rage DDL9 @ LBA 117224 byte 42; Baldies CINE=$43494E45 @ LBA 20958 byte 46),
   and the game payload starts on a phase-1 entry boundary (word 53 / 55, both odd).
   Discs are *mastered* for a one-word capture skew in BUTCH's I2S→FIFO assembly
   (MiSTer butch.v lines 1744-49 assemble entries across an faddr word-parity;
   line 485: I2SDAT1/I2SDAT2 are content-identical ports). Our emulation grouped
   from word 0 → every entry read $4C39xxxx → the ISR scanned the disc forever
   (the observed 19 MB / 2.1 M fifoReads with no progress).

4. **Second, masked defect (drain starvation):** the scan handler reads **9 longwords
   = 18 word-reads per invocation** (`moveq #9,r22`, ports alternated via
   `xor #$0C,r27` → $DFFF28/$DFFF24), but `FIFO_DRAIN_READS=16` cut data delivery
   after 16 word-reads — reads 17-18 returned $0000, resetting the 16-run counter
   *every invocation*, so even the correct phase could never accumulate 16 matches.

## The fix (src/cd/cdrom.c, both in 09a62d6)

1. `cdBufPtr = 2` (was 0) at non-redundant $12xx seek start — grouping starts one
   16-bit word into the stream; stream stays linear from byte 2, parity persists
   across sector rollovers (1176 words/sector).
2. FIFO port reads deliver data while `cdPlaying` even after the drain gate clears
   `fifoDataReady` (drain/refill IRQ pacing unchanged) — kills the 17th/18th-read
   zero-injection. Real BUTCH returns stale FIFO content past the fill level, never
   zeros.

Plus a diag accessor `CDROMDiagGetFirstSeekBlock()` (cdrom.c/h; reset in
`CDROMReset`) used by the new contract test. **No savestate changes.**

## Byte-compare evidence (the definitive test the dispatch asked for)

Expected: BIN track 31, LBA 117224, post-sync byte 106 (I2S-unswapped):
`4E F9 00 00 40 18 00 7B …` (JMP $4018 — matches the HLE oracle's $4000 dump exactly).
- **Before fix:** these bytes appear **nowhere** in the 2 MB of main RAM (probe scan);
  $4000 still holds boot-time PRNG fill; 68K parked at $0803AC/$003610 poll bands.
- **After fix:** RAM $4000.. = `4E F9 00 00 40 18 00 7B …` byte-identical; 68K
  final_pc in game code.

## TDD — RED → GREEN

New `test/test_cd_fifo_stream.c` (wired into Makefile; run-gated on `VJ_FIFO_DISC`,
skips cleanly in CI): boots the disc in bios mode, reads the first seek LBA via the
new diag accessor and the live sentinel from the GPU state block ($F03118+16), finds
the ≥16-repeat sync mark on the disc image, and asserts the 64 post-sync payload
bytes appear contiguously in main RAM.

- **RED** (fix knobs reverted, accessor kept): `ram_hit=-1 … FAIL … streaming wall`.
- **GREEN** (fix): `sync mark 44444C39 x16 at LBA 117224 byte 42 … ram_hit=16384
  ($4000) … PASS`.
- Baldies SKIPs (its engine doesn't populate the $F03118 layout — different GPU
  upload; its progress is covered by the matrix row instead).

## Gates

- `make TEST_EXPORTS=1 -j` clean build (fresh, post-user-build).
- `scripts/c89-lint.sh` PASS on `src/cd/cdrom.c` and `src/cd/cdrom.h`.
- `make TEST_EXPORTS=1 test` → **exit 0** (all sections 0 failed; audio pair included,
  no audio path touched).

## Matrix (deleted + re-run, 3000 frames, final core; only mover vs committed rows: Baldies)

| Title | mode | BEFORE (wall) | AFTER (this fix) | verdict |
|---|---|---|---|---|
| Primal Rage | bios | BOOT_STUB $0803AC, seeks=1, sentinel scan forever | first load done, game code ran, **second** CD_read issued (seek_starts=2), wedges in transfer #2 (fifo_drains frozen 12857, gpu_pc=$F03168), final_pc=$003616 | **ADVANCED — wall broken; next blocker is the 2nd-load transfer** |
| Baldies | bios | BIOS poll band $003610 | **GAME_CODE final_pc=$060106** (payload 11957B, uniq=38) | **ADVANCED to GAME_CODE** |
| Highlander | bios | $003610, fifo_drains=54 frozen | unchanged (byte-identical row) | not this mechanism — drains freeze pre-sentinel |
| Iron Soldier 2 | bios | $0036B8 video_stall | unchanged | not this mechanism |
| BrainDead 13 | bios | GAME_CODE $00361E | unchanged | control ✓ |
| Primal Rage | hle | GAME_CODE $00419E | unchanged | control ✓ |
| Iron Soldier 2 | hle | FAIL $007416 (pre-existing) | unchanged | control ✓ |

## Self-review

- Every claim traced to primary evidence: seek math from the trace ring both modes;
  word stream from FIFO pop instrumentation (since reverted); ISR semantics from the
  GPU RAM dump + disassembly (scan loop $F0321E-$F03264, transfer $F031C4-$F031E4,
  state block $F03118); the phase contract brute-forced against the real BINs of two
  independent titles; MiSTer butch.v consulted for FIFO width/port semantics.
- Fix is 2 behavioral lines + comments at the layer the evidence names (cdrom.c FIFO
  model); HLE path untouched; cart path untouched; no savestate fields.
- Temp instrumentation (TDIAG stderr taps, exports-test.list `_CDTrace*`) all
  reverted before commit; `git show 09a62d6` reviewed clean.
- Matrix honest: 2 of 4 targets moved, 2 recorded unchanged with their real
  (different) blocker named; 3 controls byte-identical.

## Concerns / next blockers

1. **Primal Rage 2nd load wedges** (new frontier): after the first load succeeds and
   game code runs, the second CD_read (DDL5, seek 127506) transfer freezes with
   gpu_pc=$F03168 (DSARX-handler region) and fifo_drains frozen. Likely a
   DSA-response / I2S-re-enable state issue on *repeat* reads, not phase (127516's
   sync is also byte≡2 mod 4). Deserves its own task.
2. **Highlander / IS2 (bios)** stall *before* any sentinel scan (drains freeze at
   54/0, gpu_pc $F0307E/$F03068) — a different engine or an earlier handshake;
   unaffected by this fix, needs separate diagnosis.
3. The one-word skew is implemented at seek start (deterministic). If a future title
   seeks mid-stream expecting even-phase grouping, the model may need the skew tied
   to I2S-enable instead; no such title observed.
4. `test_cd_fifo_stream` depends on the retail-BIOS GPU state-block layout
   ($F03118+16) for the sentinel; dev-BIOS or non-$303C engines SKIP rather than
   fail (Baldies does).
5. User committed 09a62d6 mid-session from my tree (message accurate); the follow-up
   docs commit refreshes the 7 matrix rows against the final core (only Baldies'
   row changed — its mid-session run had hit a stale build) and adds this report.
