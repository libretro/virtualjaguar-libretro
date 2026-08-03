# BrainDead 13 / Primal Rage: the second CD-transfer wedge

Diagnosis written on `feature/jaguar-cd-support`, starting from `d2cd04b` on
top of `09a62d6` (FIFO word-phase fix; see cd-streaming-wall-diagnosis.md).

Goal: diagnose (and if bounded, fix) the wedge that stops CD games AFTER the first
boot-stub load succeeds — the continuous-FMV-streaming stall. User-confirmed on device:
BrainDead 13 boots BIOS, runs injected game code, shows logo, then sticks when
continuous FMV streaming should begin.

## Investigation log

### Orientation
- Prior work: the first wedge (streaming wall / FIFO longword grouping phase) is
  fixed and byte-exact (test_cd_fifo_stream GREEN, regression floor).
- Second-wedge signatures per dispatch:
  - Primal Rage (bios): 2 seeks complete, 12,857 fifo_drains, then cd_seek_wedge;
    gpu_pc=$F03168 gpu_run=1, dsp_pc=$F1B0AC dsp_run=1 (DSP now running — new).
  - BrainDead 13 (bios): reaches game code ($1243xx), then video_stall,
    gpu_pc=$F0327A gpu_run=1, dsp_pc=$F1B120 dsp_run=0.
  - Highlander: cd_seek_wedge fifo_drains=54; IS2: video_stall dsp_run=1.

### Re-baselining against the current matrix
- docs/cd-boot-matrix.md rows + the streaming-wall diagnosis. Key supersessions:
  Baldies bios now GAME_CODE ($060106); Highlander/IS2 bios = different pre-sentinel
  mechanism (drains freeze 54/0), separate branch; FIFO_DRAIN_READS starvation already
  fixed in 09a62d6 (FIFO port reads deliver while cdPlaying even after drain gate).
- Primary targets: Primal Rage bios 2nd transfer (gpu_pc=$F03168, DSARX-handler
  region — strengthens W-STOP-RESTART/DSA semantics) and BrainDead 13 bios
  post-logo FMV streaming (video_stall gpu_pc=$F0327A, dsp_run=0).

### Primal Rage BIOS-mode trace + BrainDead 13 device trace
- Rebuilt core TEST_EXPORTS=1 (tree had stale iOS .o files; make clean first).
- Regression floor confirmed GREEN: test_cd_fifo_stream PASS (ram_hit=$4000).
- Harness diag_second_wedge (scratchpad): Primal Rage bios, trace ring dumped at
  2nd SEEK_START and at drain-freeze; GPU RAM $F03000-$F03FFF dumped at wedge.
- Primal Rage 2nd-transfer facts:
  - DSA conversation before 2nd seek: $0200 STOP -> RX $0200 (x2, tick 291251 and
    315981 — 24,730 ticks apart!), then $1502/$170A(x2 RX), $101C/$1116/$1206 =
    seek block 127506, SEEK_DONE+RX $0100 100 ticks later. Handshake is CLEAN.
  - Transfer #2 streams blocks 127506->127512 (~6 sectors) then drains freeze at
    12857 (tick ~318580, frame ~607); wedge detected frame 667.
  - GPU PC histogram at wedge: $F03168/$F0316A/$F0316C only — 3-insn spin loop.
  - GPU state block $F03118 at wedge: +0=$980002C0 +4=$00048C01 +8=$B4019800
    +12=$32D800F0 +16=$980132E0 — NOT the task-7 layout (dest/end/counter/sentinel);
    the GPU program was REPLACED between transfer 1 and 2 (game/BIOS uploaded a new
    engine; r31=$F03368, r26=$F03E9C — code+stack extend to $F03E9C).
- BrainDead 13 device trace (`docs/cd-diagnosis/braindead-device-trace.txt`):
  FIRST game transfer (seek_starts=1): pre-seek $7001, $150A->$170A x3, goto
  MSF 05:04:06 block 22656 correct, SEEK_DONE, then FILL/DRAIN alternate forever,
  ~150 drains/sector, GPU pinned $F03276-$F0327A, dsp_run=0, 68K never resumes.
- Cross-matrix: $F03270-$F0327A spin appears in BrainDead(bios), Dragon's Lair
  (bios+hle), Space Ace(bios+hle), Baldies(bios) — a COMMON post-boot streaming
  loop. Primal Rage wedges at a different loop ($F03168) of its second engine.

### The replaced engine decoded (Primal Rage dump, live at wedge)
GPU RAM disassembly of the SECOND engine (game-uploaded, dumped live at the wedge):
- Vector $F03010 -> JUMP $F03380 (second-stage CD ISR). Vector $F03000 (IRQ0/CPU)
  -> $F03040: writes OLP ($F00020), then if [$F032E4]!=0: clear it + STOREB 1 ->
  [$402C0] (68K mailbox). IRQ0 is the frame/kick handler.
- Foreground wedge loop: $F03156 sets [$F032E4]=1; $F03168-$F0316C spins while
  [$F032E4]!=0 — waits for the IRQ0 (CPU-kick) handler, NOT the CD ISR.
- CD ISR ($F03380): state block R23=$F0336C: +0 dest, +4 end, +8 spare,
  +12 sentinel-scan counter, +16 sentinel. DSARX branch: ack BUTCH, set I2CNTRL
  bit 2, read DS_DATA; bit10 set -> data path, else epilogue. FIFO branch:
  counter!=0 -> sentinel scan at $F03472 (9 longwords/invocation, alternating
  $DFFF24/$DFFF28, 16-consecutive rule — same contract as task-7); counter==0 ->
  DMA copy 8 longwords/IRQ at $F03418; dest>=end -> BCLR #0 BUTCH (disables
  global INT = transfer-complete signal). Epilogue re-arms JERRY_INT $0101.
- State at wedge: dest=$637DC end=$895E0 counter=$10 sentinel=$44444C35 "DDL5"
  mailboxes: [$F032D8]=0 [$F032E0]=4 [$F032E4]=1 [$F032E8]=$8C.
- MEANING: the ISR was still in SENTINEL-SCAN mode (counter=16, no run in
  progress) when FIFO drains froze at block 127512 — before reaching DDL5's
  sync sector (~127516). The foreground spin at $F03168 is a VICTIM: it waits
  for a 68K kick that never comes because the 68K CD_read service loop
  ($003616) never sees the transfer finish.
- OPEN: why did FIFO IRQ delivery stop mid-scan? fifoReady=1 i2sEn=1 cdPlaying=1
  at the wedge. Candidates: BUTCH enable bits changed (bit0/bit1), G_FLAGS
  INT_ENA1/IMASK state, JERRY latch not re-armed. Next: sample BUTCH enables +
  diag counters per-frame across the freeze.

### Root cause (byte-level evidence, two independent titles)
**Mechanism: GPU interrupt dispatch clobbered by the branch delay slot.**
The CD streaming ISR epilogue idiom (both engines, and the retail BIOS ISR) is:
`JUMP T,(Rret)` with `STORE Rflags,(G_FLAGS)` in the DELAY SLOT — the store
clears IMASK (BCLR #3) + acks INT_CLR1 (BSET #10). In gpu.c:
- `gpu_opcode_jump/jr` execute the delay-slot instruction INLINE, then
  unconditionally apply `gpu_pc = delayed_pc`.
- The G_FLAGS write handler (case 0x00) calls `GPUHandleIRQs()` synchronously
  when IMASK transitions 1->0 (`IMASKCleared`).
- If a FIFO IRQ is already LATCHED (BUTCHExec re-asserts every halfline tick
  while fifoDataReady; latch survives IMASK), that dispatch runs INSIDE the
  delay slot: sets IMASK=1, pushes gpu_pc-2 (= delay-slot addr — also wrong),
  sets gpu_pc = vector $F03010. Then gpu_opcode_jump OVERWRITES gpu_pc with
  delayed_pc. Result: IMASK=1 forever, ISR never entered again, no one clears
  IMASK (foreground flag writes have bit3=1 which PRESERVES current IMASK per
  the write handler), FIFO never drained again. 68K CD_read service loop and
  GPU foreground mailbox spin both starve -> video_stall / cd_seek_wedge.
Evidence:
- Primal Rage at wedge: G_FLAGS=$4038 (IMASK=1) with gpu_pc in FOREGROUND spin
  $F03168-$F0316C; fifoIRQs keep incrementing (~524/frame), fifoReads frozen;
  bank0 r31=$F03368 (one stale un-popped push), [$F03368]=$00F03470 = the
  epilogue DELAY SLOT address ($F0346E JUMP / $F03470 STORE R29,(R30)).
- BrainDead 13 at wedge: G_FLAGS=$40F9 (IMASK=1), gpu_pc spin $F03270-$F0327A
  (waits [$F039DC] set by its ISR); stale push [$F03FFC]=$00F03BE2 = its
  epilogue delay-slot addr ($F03BE0 JUMP T,(R28) / $F03BE2 STORE R29,(R30)).
  Engine differs, same idiom, same clobber.
- Why transfer #2 (PR) / post-logo (BD): probabilistic race — needs a latched
  IRQ pending at the exact IMASK-clearing epilogue; streaming volume makes it
  near-certain within seconds. Nothing about seek phase/DSA/STOP is wrong (2nd
  DSA conversation verified clean: STOP $0200, $1502/$150A->$170A, goto ->
  block 127506 correct, SEEK_DONE $0100 delivered).
- W-PHASE-PERSIST / W-STOP-RESTART / W-BACKPRESSURE: rejected as the wedge
  cause (handshake + stream + refill all healthy up to the clobber). W-DSP:
  dsp.c is IMMUNE to this exact bug — it defers IMASK-clear dispatch via the
  `IMASKCleared` flag checked at the top of DSPExec's loop, after delayed_pc
  is applied. GPU dispatches synchronously inside the store — the defect.
Fix: make GPUHandleIRQs delay-slot-aware — when dispatch happens during a
delay slot, push the BRANCH TARGET (-2) as the return address and tell the
jump opcode NOT to overwrite gpu_pc. Transient flags, no savestate impact.

### BrainDead 13 wedge fully disassembled
Reproduced the device trace exactly (local, bios mode, BrainDead 13): after the
first game CD_read (seek block 22656) the FIFO fills/drains forever, GPU pinned
$F03270-$F0327A, 68K cycling, fb frozen. GPU RAM dumped (scratchpad
bd13_gpuram_wedge.bin) + disassembled (riscdis.py).

**GPU spin loop $F03270 is a HEALTHY mailbox waiter, not the bug:**
```
$F03260 movei #$F03A50,r31        ; set stack
$F03266 movei #$F039D8,r0; moveq #1,r1; store r1,(r0)   ; *($F039D8)=1  "GPU ready"
$F03270 movei #$F039DC,r0         ; LOOP
$F03276 load (r0),r1; or r1,r1; jr z,$F03270            ; wait *($F039DC)!=0
```
$F039DC/$F039D8 are NOT referenced by any GPU code in the dump -> the mailbox is
written by the 68K. GPU engine is idle-waiting for a 68K command that the game
loop hasn't sent because it's still stuck in CD_read.

**68K side (PC histogram $124342/$124346/$12434C <-> $003610-$003620):**
```
$124342 jsr $303C      ; CD_read (BIOS jumptable idx2 -> $003624)
$124346 jsr $304E      ; CD poll (jumptable idx5 -> $003610): a0 = [[$3074]]
$12434A cmpa.l #$00080000,a0
$124350 ble.s $124346  ; loops on $304E (poll), NOT re-calling CD_read
```
[$3074] = **$00F03B10** (CD transfer state lives in GPU RAM). a0 = *($F03B10) =
current dest, **FROZEN at $050BDC** across the whole wedge (never nears $80000).
So: CD_read sets up dest once; the loop polls *($F03B10) waiting for the GPU
CD-ISR to advance it past $80000; the ISR never advances it.

**GPU CD-ISR $F03B1C (vector $F03010 -> $F03B1C):**
- r23 = $F03B10 (state block: +0 dest, +4 limit), r24 = $DFFF00 (BUTCH).
- reads BUTCH ICR (*$DFFF00). `btst #13` (DSARX): if set -> DSA path
  ($F03B44): clear enable b5, set enable b1, set I2CNTRL($DFFF10) bit2 (I2S
  data enable), consume $DFFF04 + DS_DATA($DFFF0A), jump epilogue (NO transfer).
- else `btst #14`; else FIFO path $F03B78: load dest/limit, `cmp`,
  read 8 longwords alternating $DFFF28/$DFFF24, store to dest, dest+=32,
  `store r26,(r23)` (advances [$F03B10]), epilogue re-arms J_INT=$0101.

Since [$F03B10] is frozen, the ISR is NOT executing the FIFO transfer store path,
yet FIFO drains DO advance (~150 drains/sector). Prime hypothesis: the ISR keeps
taking the **DSARX (bit13) path** every invocation because our BUTCH status
keeps bit13 (dsaResponseReady) asserted, so it never reaches the FIFO transfer
that advances dest. Next: instrument BUTCH-status reads (value + gpu_pc) and
FIFO-port reads to confirm which ISR path runs, then fix at the cdrom.c status
layer. (2x drains/sector vs the 73.5 expected also points at a status/pathing
mismatch, not wrong data.)

### Bit-13 hypothesis falsified; the real wedge is post-transfer signaling
Review caught it: drains prove the ISR FIFO path runs. Per-frame poll of the
transfer state ([$F03B10] dest / [$F03B14] limit) shows the transfer **completes
normally**: dest climbs $45FFC -> $146000 (limit) over frames ~55-368 (~1 MB),
data lands. The wedge is AFTER the transfer.

Post-transfer the 68K parks forever in a STOP-and-poll loop (disasm from RAM):
```
$00B2DC lea $0072CC,a0            ; a0 = poll byte
$00B2E2 clr.w (a0)                ; *($0072CC) = 0
$00B2E4 lea $F037DC,a1
$00B2EA adda.l $0072F0,a1         ; [$72F0]=$200 -> a1 = $F039DC
$00B2F0 move.l #1,(a1)            ; *($F039DC) = 1   (command GPU)
$00B2F6 stop #$2000               ; halt until IRQ
$00B2FA tst.b (a0)                ; wait *($0072CC) != 0
$00B2FC beq.s $00B2F6
```
So: 68K commands the GPU via mailbox $F039DC=1, STOPs, waits for the done byte
$0072CC. The GPU idle loop ($F03270) exits on $F039DC!=0, clears it, dispatches
on $F039E8 (=3 -> handler $F034EC, an RLE/decode jump-table), returns to idle.
At the wedge F039DC=0 (consumed), F039E8=3, GPU back at $F03270, [$0072CC] never
set -> 68K STOPs forever, video frozen.

**$0072CC is NOT referenced by any GPU code in the dump** (nor $F02114 as a
main-RAM signal). So the done-flag is written by a **68K interrupt handler**, not
the GPU. The 68K wakes from STOP every frame (VBLANK) but never sets $0072CC ->
the completion IRQ that should set it never reaches the 68K. BUTCHExec (cdrom.c
~680-699) *deliberately* suppresses m68k IRQ2 (stale-vector stack corruption in
Primal Rage/Hover Strike). BrainDead 13 DOES install a 68K CD/GPU handler and
STOP-waits on it -> the suppressed interrupt is exactly what it needs.

CD DATA PATH IS CORRECT. Remaining gap = GPU-completion / CD-done interrupt
delivery to the 68K + the GPU-FMV decode engine's per-frame signaling. Evaluating
bounded-fix vs structural next.

### The fix, gates and commit
- Fix committed: 7c98e16 fix(gpu): don't clobber IRQ dispatch raised in a
  branch delay slot. gpu.c: GPUHandleIRQs pushes the branch target (-2) and
  flags the in-flight gpu_opcode_jump/jr to not overwrite gpu_pc when the
  dispatch was triggered by the delay-slot instruction. Transient flags reset
  in GPUReset; NO savestate changes. Makefile: new test wired into deps +
  "built but not run" note (same class as fifo_stream).
- TDD: test/test_cd_second_transfer.c — liveness contract: FAIL if 120
  consecutive frames of (IMASK set + GPU running + fifo drains frozen) after
  the first seek. RED at d2cd04b (Primal Rage wedges frame ~727,
  G_FLAGS=$4038); GREEN at 7c98e16.
- Gates: c89-lint gpu.c PASS; make TEST_EXPORTS=1 test exit 0 / 0 FAILs;
  audio pair verified directly (clipping self-test PASS; IS1 presence RMS
  1175.7 in [200,25000]); test_cd_fifo_stream GREEN (byte-exact first load).
- Post-fix behavior (diag harness):
  - Primal Rage bios: transfer #2 runs to completion — drains 12441->17996,
    ISR clears BUTCH bit0 (its completion signal), IMASK clear, foreground
    exits both spins to $F031F4-$F031F8 waiting on OP int (INT_ENA3) — the
    SAME state as the HLE-mode control row (video_stall gpu_pc=$F031F6),
    i.e. bios mode now reaches the HLE oracle's state.
  - BrainDead 13 bios: transfer #1 completes (6145 drains vs 95 frozen);
    game issues SECOND seek (frame 655) and streams ~1 MB FMV (38915 drains)
    to clean completion, IMASK clear. The user-reported logo wedge mechanism
    is gone; next wait is the game's frame-kick mailbox (headless-fb class).
- Temp instrumentation: exports-test.list _CDTrace* addition reverted before
  commit (harness works without it; ring dumps were session-only). Diag
  harness + disasm wrapper live in scratchpad only.

### Matrix re-run (7 rows, 3000 frames, core at 7c98e16)
| Title/mode | BEFORE (d2cd04b) | AFTER (7c98e16) | verdict |
|---|---|---|---|
| BrainDead 13 bios | video_stall f=739 final_pc=$00361E dsp_run=0 (device-confirmed logo wedge) | logo load + SECOND ~1MB FMV read complete (38915 drains), final_pc=$00B2FA, unique 23->43, dsp_run=1 | **ADVANCED** |
| Primal Rage bios | cd_seek_wedge drains frozen 12857 mid-scan gpu=$F03168 | transfer #2 completes (17996 drains, BUTCH bit0 completion signal), gpu waits $F031F6 = SAME state as hle oracle | **ADVANCED to HLE-oracle state** |
| Highlander bios | cd_seek_wedge drains frozen at 54 | no seek wedge; gpu active $F031C2, dsp_run=1, final_pc=$0036BE | **ADVANCED** (its 54-drain freeze WAS the same clobber) |
| Dragon's Lair bios (control, but wedge-class) | BIOS_INTRO $0060D8 | CD service band $0036B8, unique 38->51 | moved FORWARD |
| Iron Soldier 2 bios | video_stall $0036B8 | byte-identical | unchanged (different, earlier mechanism) |
| Primal Rage hle / IS2 hle (controls) | — | byte-identical | ✓ |
Notes section added to docs/cd-boot-matrix.md ("Task 8 fix").

### Loose ends / follow-ups (documented, not blocking)
1. cd_seek_wedge watchdog fires on LEGIT transfer completion (drains stop
   because the engine's ISR clears BUTCH bit 0 when dest>=end). Refinement
   candidate: suppress when BUTCH bit 0 is clear. Noted in matrix doc.
2. The $15xx pre-seek DSA command (device trace lead #2): $1502/$150A = Set
   Mode; our handler echoes $17nn Mode Status — the BrainDead trace shows
   exactly $150A TX -> $170A RX, i.e. handled correctly; NOT implicated.
3. IS2 bios unchanged — its stall is a separate pre-existing mechanism
   (next diagnosis target). Baldies bios row (video_stall $F03270, dsp_run=1)
   now likely the same headless-fb / frame-kick class as BrainDead.
4. DSP is immune to this clobber by construction (deferred IMASKCleared);
   no dsp.c change made or needed for this class.

### Final status
- Commits: 7c98e16 (fix + contract test + Makefile wiring), 0b88fa0 (matrix
  rows + notes).
- Gates all green: c89-lint gpu.c, make TEST_EXPORTS=1 test exit 0,
  test_cd_fifo_stream (floor) GREEN, test_cd_second_transfer RED->GREEN,
  audio pair verified (IS1 RMS 1175.7; clipping self-test PASS).
- REMAINING: verify BrainDead 13 on an iOS device / in RetroArch —
  headless says the logo wedge mechanism is dead and FMV streaming completes;
  whether video presents correctly needs a real frontend (headless-fb caveat).
