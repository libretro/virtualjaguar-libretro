# Task 9 report — the THIRD wall: FMV streaming "stops" mid-playback (BrainDead 13)

Branch `feature/jaguar-cd-support`, base `0b88fa0`. Append-only phase log.

## Phase 0 — orientation + hypothesis triage (before any code)

Read: `braindead-device-trace-2.txt`, `task-8-second-wedge-report.md` (Phases 2/3/4/5),
`task-7-streaming-wall-report.md`, `src/cd/cdrom.c` BUTCHExec (lines 574-703).

### Hypothesis A (continuous streaming not modeled) — REJECTED before testing

Rejected on evidence already in hand, not on a new experiment:

- Task-8 headless measured **38915 drains to clean completion**; the device trace
  reports the **identical 38915**. The ~1 MB transfer completes identically in both.
  A starvation bug would not produce a byte-identical drain count on two different
  hosts, and would not "complete" at all.
- Task-8 loose-end #1 already names why drains stop: the game's GPU ISR **clears
  BUTCH bit 0** when `dest >= end` (transfer done). Drains stopping is the
  *completion signal*, not starvation.
- Task-8 Phase 3 polled the transfer state per-frame: `[$F03B10]` dest climbs
  `$45FFC -> $146000` (= limit) over frames ~55-368. The transfer **finishes**.

The device trace's "expects data to keep arriving" reading is the trace author's
inference; the disassembly + dest-pointer telemetry outrank it.

### Hypothesis B (engine waits on a different signal) — ADOPTED

The dispatch asked to distinguish A/B by disassembling `$F03270-$F0327A`.
**Task 8 already did this disasm** (report lines 129-138) and it answers B:

```
$F03260 movei #$F03A50,r31                            ; set stack
$F03266 movei #$F039D8,r0; moveq #1,r1; store r1,(r0) ; *($F039D8)=1  "GPU ready"
$F03270 movei #$F039DC,r0                             ; LOOP
$F03276 load (r0),r1; or r1,r1; jr z,$F03270          ; wait *($F039DC)!=0
```

This polls a **RAM mailbox ($F039DC in GPU RAM), not a FIFO/BUTCH status word**
-> hypothesis **B**, decisively. The GPU streaming engine is a healthy idle waiter.

### Correction to task-8's sign-off (important)

Task 8 closed BrainDead 13 as "next wait is the game's frame-kick mailbox
(**headless-fb class**)" — i.e. presumed a headless presentation artifact.
**The device trace falsifies that**: the same wait is a real black-screen wall on
real iOS hardware. This aftermath is a genuine blocker, not a harness artifact.

### The named gap (what Phase 1 must resolve)

Task-8 Phase 3 established the post-transfer wedge shape:
- 68K: `$00B2F0 move.l #1,(a1)` (a1=$F039DC, command GPU) -> `$00B2F6 stop #$2000`
  -> `$00B2FA tst.b ($0072CC)` -> `beq.s $00B2F6`. Device trace `final_pc=$00B2FA`
  confirms this exact loop on hardware.
- GPU: consumes $F039DC, dispatches on $F039E8 (=3 -> handler $F034EC), returns idle.
- `$0072CC` is **never set**; it is **not referenced by any GPU code** in the dump,
  nor is `$F02114` (G_CTRL / CPUINT) -> the GPU neither writes the flag nor
  interrupts the 68K. So the flag must be written by a **68K interrupt handler**.

UNDECIDED, and it names the fix layer: **which source is that handler waiting on?**
Two disasms task 8 did not do:
1. The **68K level-2 IRQ handler** — what does it read, and on what condition does
   it store to `$0072CC`? Whatever it reads IS the signal source.
2. **$F034EC's epilogue** — does the decode handler set any done-flag / clear the
   command / touch a 68K-visible location?

Candidate layer A: `cdrom.c:679-699` deliberately suppresses m68k IRQ2 (stale-vector
stack corruption in Primal Rage / Hover Strike). BrainDead 13 *does* install a 68K
handler and STOP-waits on it. BUT: the `shouldIRQ` gate (line 658/667) requires
BUTCH bit 0, and the game's ISR clears bit 0 at completion — if bit 0 is clear at
the wedge, the pending path cannot deliver anyway and cdrom.c is the wrong layer.
**Do not pre-write the conditional-IRQ2 fix.** Disasm #1 decides.

(Advisor consulted at this checkpoint; concurred A is dead, B is the mechanism,
and that the layer must be named by disasm #1 before any code.)

## Phase 1 — reproduced headlessly + the two missing disasms (EVIDENCE)

Harness: `scratchpad/diag_fmv.c` (BIOS mode via `virtualjaguar_cd_boot_mode=bios`).
**Repro is exact vs the device trace**: `starts=2 dones=2 drains=38915`, GPU pinned
$F03270-$F0327A, `68k_pc=$00B2FA` for **300/300** frames post-wedge.

### Harness gotcha (cost a cycle; recorded so the next agent doesn't repeat it)
`jaguarMainRAM` is a **`uint8_t *` pointer**, not an array (`src/core/vjag_memory.c:45`).
`dlsym` returns `&pointer` -> raw indexing reads ARM64 pointer bytes as "RAM"
(the bogus "vector table" `94 F1 DC 05 01 00 00 00 ...` was literally consecutive
pointers into `jagMemSpace`). Must deref: `p_mainram = *(uint8_t **)dlsym(...)`.
Also `M68K_REG_PC` = **16**, not 0 (D0..D7=0-7, A0..A7=8-15).

### Hypothesis A — REFUTED by direct measurement
`[$F03B10]` cd dest = **$0014603C** >= `[$F03B14]` limit = **$00146000**.
The ~1 MB FMV transfer **ran to completion**. Drains stop because the work is done.
Not starvation. A is dead, as predicted in Phase 0.

### The 68K wedge site, decoded from the RAM dump (confirms task-8 byte-for-byte)
```
$00B2DC 41F9 000072CC   lea     $000072CC,a0
$00B2E2 4250            clr.w   (a0)              ; done flag := 0
$00B2E4 43F9 00F037DC   lea     $00F037DC,a1
$00B2EA D3F9 000072F0   adda.l  ($000072F0),a1    ; [$72F0]=$200 -> a1 = $F039DC
$00B2F0 22BC 00000001   move.l  #1,(a1)           ; command GPU
$00B2F6 4E72 2000       stop    #$2000            ; halt until IRQ  (SR=$2000 -> IPL mask 0)
$00B2FA 4A10            tst.b   (a0)              ; <- reported PC while halted
$00B2FC 67F8            beq.s   $00B2F6
$00B2FE 4E75            rts
```

### DISASM #1 result — the GPU decode epilogue DOES raise CPUINT
**Task-8's claim "$F02114 is NOT referenced by any GPU code" is WRONG.** A byte
search of the live GPU RAM dump for the `movei #$00F02114` immediate (encoded
low16-then-high16 = `21 14 00 F0`) hits **$F03284** and **$F036C6**. Disassembled:

```
; --- decode-complete epilogue (THE wake signal) ---
$F036C2: 8C60  moveq #3,r0             ; 3 = GPUGO(bit0) | CPUINT(bit1)
$F036C4: 9801  movei #$00F02114,r1     ; G_CTRL
$F036CA: BC20  store r0,(r1)           ; *G_CTRL = 3  -> interrupt the 68000
$F036CC: 9800  movei #$00F03270,r0
$F036D2: D000  jump  (r0)              ; back to idle loop

; --- idle loop ($F03284 ref is a DIFFERENT, negative-command GPU-halt path) ---
$F03270: movei #$00F039DC,r0
$F03276: load  (r0),r1
$F03278: or    r1,r1
$F0327A: jr    z,$F03270               ; spin while cmd == 0
$F0327C: moveq #0,r1                   ; (delay slot)
$F0327E: jr    nn,$F0328E              ; cmd > 0 -> normal dispatch, SKIPS $F03282-8C
$F03280: store r1,(r0)                 ; (delay slot) consume cmd
$F03282: movei #$00F02114,r0           ; only for NEGATIVE cmd: G_CTRL := 0 (halt GPU)
$F03288: store r1,(r0)
$F0328E: movei #$00F039E8,r0           ; dispatch on opcode ($F039E8 = 3 -> $F034EC)
```
Per JTRM (`docs/jtrm-register-map.md`, G_CTRL bit layout): **bit 1 = CPUINT,
"Write 1 to interrupt 68000 from GPU. Always reads 0."** -> $F036CA is exactly
the documented GPU->68K wake. So the engine's design is:
68K sets mailbox -> `stop` -> GPU decodes -> sets done flag -> **CPUINT** -> 68K wakes.

### The signal source is NAMED: C_GPUENA, and it is enabled
`TOMReadWord($F000E0)` returns **pending bits only** (`TOMIRQControlReg`, tom.c:1363)
— my first read of `$0009` was *video+timer PENDING*, not enables. The real enable
byte is `tomRam8[$E1]`:

    RAW tomRam8[$E1] = $02  ->  VIDENA=0 GPUENA=1 OPENA=0 PITENA=0 JERENA=0

**The game enables ONLY the GPU interrupt.** So VBLANK/PIT cannot wake it — task-8's
"the 68K wakes from STOP every frame (VBLANK)" is **false**; video int pends 60x/60
frames and is masked off every time. The ONLY wake source is the GPU's CPUINT.
=> This is **not** the `cdrom.c` IRQ2-suppression layer. The advisor's trap is avoided:
the m68k IRQ2 suppression (`cdrom.c:679-699`) is irrelevant here (JERENA=0 anyway).

### IRQ-chain counters at the wedge (temporary core instrumentation, to be reverted)
```
TOMSetPendingVideoInt calls = 1384
TOMAssertEnabledIRQs calls  = 12196   fired(m68k_set_irq) = 647
m68k_set_irq calls = 1293   (while stopped = 0)   <-- NEVER called while halted
m68ki_exception_interrupt calls = 645
G_CTRL writes = 5213 (last data = $00000003)      <-- the CPUINT store DID execute
CPUINT requested (bit1 set) = 1
CPUINT delivered (TOMIRQEnabled(GPU) true) = 1
DELTA over 60 frames @wedge: videoPend=60 assertFired=0 setIrq=0 excIrq=0
                             gctrlW=0 cpuintReq=0
```
Reading: the GPU raised CPUINT **exactly once**, it **was** delivered, and
`m68k_set_irq` was **never** called while the 68K was halted. State at the wedge:
`[$0072CC]=$FFFF` (done flag ALREADY SET), `[$F039DC]=0` (cmd consumed),
`[$F039E8]=3`, GPU back in its idle loop, 68K halted in `stop`.

So both sides already did their jobs — the GPU finished and signalled, the 68K is
halted waiting for a signal **that already came and went**. This is a lost-wakeup
**race**, not a missing feature. Next: pin down the ordering (why the one CPUINT
landed while `stopped=0`), which names the layer.

## Phase 2 — MECHANISM NAMED: lost wakeup on `stop #$2000` (GPU CPUINT consumed pre-STOP)

### The level-2 IRQ handler (DISASM #2) — it is the done-flag writer
`irq_ack_handler()` (`src/core/jaguar.c:315-334`) returns **64** for level 2
("all IRQs to the 68K are routed thru TOM at level 2"), so the vector is
**$100**, NOT the autovector $68. (My earlier "autovector garbage" read was a
red herring — $64-$7C are genuinely unused PRNG fill on this title.)

    [$100] = $0000A12A

```
$00A12A 2F08               move.l  a0,-(sp)
$00A12C 41F9 000072CC      lea     $000072CC,a0
$00A132 30BC FFFF          move.w  #$FFFF,(a0)          ; *** SETS THE DONE FLAG ***
$00A136 33FC 0202 00F000E0 move.w  #$0202,$00F000E0     ; INT1: C_GPUCLR(bit9)+C_GPUENA(bit1)
$00A13E 33FC 0000 00F000E2 move.w  #$0000,$00F000E2     ; INT2: resume bus priorities
$00A146 205F               movea.l (sp)+,a0
$00A148 4E73               rte
```
The `move.w #$0202,$F000E0` independently corroborates the measured enable byte
`tomRam8[$E1]=$02` (GPUENA only). Task-8 was right that *GPU* code never writes
$0072CC — the **68K IRQ handler** does. Only 3 refs to $000072CC exist in main RAM:
`$A12E` (handler), `$B2DE` (the wait loop), `$1FFF98`.

### The engine's intended protocol (hardware)
1. 68K `clr.w ($0072CC)`  2. 68K `move.l #1,($F039DC)` (command GPU)
3. 68K `stop #$2000` (halt)  4. GPU decodes -> `store #3,(G_CTRL)` = GPUGO|CPUINT
5. 68K wakes -> level-2 IRQ -> $00A12A sets flag=$FFFF, acks, `rte`
6. 68K resumes past `stop` -> `tst.b` = $FF -> exits. 
On real silicon steps 4-5 CANNOT precede step 3: `move.l`->`stop` is ~6 cycles,
the GPU decode is thousands.

### What we actually do — measured, not inferred
```
CPUINT raised in m68k slice #1274819
68K first entered STOP in slice #1274821      (2 slices LATER)
--> CPUINT arrived BEFORE the 68K executed stop  => LOST WAKEUP
CPUINT requested = 1, delivered = 1, m68k_set_irq-while-stopped = 0, lostWakeup = 0
```
Scheduler (`JaguarExecuteNew`, jaguar.c:983-1015) runs
`m68k_execute(delta); GPUExec(delta); DSPExec(delta);` per slice. The GPU's CPUINT
landed while `regs.stopped == 0`, so `m68k_set_irq` took the **deferred** branch
(`IRQLevelToHandle=2; checkForIRQToHandle=1`, m68kinterface.c:186-190). The 68K then
serviced that IRQ **before** reaching `$00B2F6`, the handler set `$0072CC=$FFFF` and
cleared TOM's GPU-pending latch, `rte` — and only *then* did the 68K execute
`stop #$2000`. `stop` does not re-test the flag, and nothing ever raises CPUINT
again (GPU is idle at $F03270, `[$F039DC]=0`). Deadlock:

    68K halted in stop  |  flag $0072CC ALREADY $FFFF  |  GPU idle, no cmd  |  black screen

This is a **lost-wakeup race created by execution-interleaving granularity**, not a
missing CD/BUTCH feature and not FIFO starvation. `m68k_execute` returns immediately
when `regs.stopped` (m68kinterface.c:124-130), so nothing re-examines the condition.

Verdict on the dispatch's hypotheses: **A refuted** (transfer completes),
**B confirmed and now fully mechanised**, **C not implicated** (data is correct and
complete; drain rate is a red herring — the transfer finished byte-correct).

---
## BLAST-RADIUS ANALYSIS (controller, post-diagnosis — gates any fix)

The `stop #$2000`-wait idiom is NOT CD-only. Static scan of the local cart library
(STOP #$2000 = opcode 4E72 2000):

  10 / 30 ROMs contain it — Zero 5 (80!), Ruiner Pinball (8), Native Demo (8),
  Iron Soldier 2 (2), Skyhammer (2), Towers II (2), Battle Sphere / BS Gold (1),
  Val d'Isere (1), White Men Can't Jump (1).

Empirical: Zero 5, Ruiner Pinball, Battle Sphere, Iron Soldier 2 all run 2000 frames
today with NO hang (Ruiner's gpu_pc_escape @ $F1B12x is pre-existing + unrelated;
FPS healthy). So the current scheduler does not deadlock them.

WHY carts survive and BrainDead does not (the invariant any fix MUST preserve):
  - A lost wakeup only deadlocks PERMANENTLY when the awaited interrupt is ONE-SHOT.
    GPU CPUINT fires exactly once per command — lose it once, halted forever.
  - Carts predominantly `stop` waiting on RECURRING interrupts (VBlank, 60Hz). A lost
    wakeup there is invisible: the next VBlank (<=16ms) wakes them. Self-healing.
  => The bug is specific to one-shot-IRQ waits (the GPU-coprocessor handshake), which
     is rare in carts and universal in the CD FMV engines.

CONSEQUENCE FOR THE FIX: any change to GPU->68K CPUINT delivery timing is a GLOBAL
emulation change with no equivalence oracle (unlike the delay-slot fix, which had DSP
parity as ground truth). It must be justified by "the GPU physically cannot reach its
CPUINT store within the ~6 cycles between the 68K's GPUGO write and its stop", NOT by
a constant tuned until BrainDead advances (the PR #170 pattern).

RECOMMENDATION: do not auto-push with the CD-localized fixes. Standalone commit,
evaluated on cart-game impact, gated on maintainer real-hardware testing across
titles. Decision deferred to maintainer.
