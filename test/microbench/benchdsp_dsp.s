; test/microbench/benchdsp_dsp.s -- DSP-side code for the DSP
; arithmetic-loop microbenchmark (#536, task 4).
;
; Assembled separately with lyxass (the DSP shares the GPU's RISC ISA per
; CLAUDE.md's hardware-model note, but is a physically distinct core with
; its own local RAM and control registers -- see benchdsp.s for those),
; then the 12-byte BS94 header is stripped and the raw code is embedded
; into the 68K bootstrap (benchdsp.s) via .incbin.  See README.md "For
; tasks 2-5: embedding a lyxass GPU/DSP blob".
;
; Assemble:
;   lyxass -o benchdsp_dsp.o benchdsp_dsp.s
;   tail -c +13 benchdsp_dsp.o > benchdsp_dsp.bin   ; strip BS94 header
;
; The 68K loader copies this blob to DSP local RAM at $F1B000 and starts
; execution there, so `.org $F1B000` here matches where it will actually
; run.
;
; --- workload shape -----------------------------------------------------
; Same shape as Task 2's benchgpu_arith_gpu.s (arithmetic-dominated, one
; taken branch closing the loop) -- this ROM isolates the DSP interpreter's
; cost in general, not a specific dispatch hypothesis (#536 task 4 scope).
;
; Body per iteration: 16 ALU ops (add/sub/and/or x4) + ADDQ (count++) +
; SUBQ (countdown--) = 18 arithmetic instructions, then a conditional
; JUMP (Rn) + its mandatory delay-slot NOP = 2 control instructions.
; 18/20 = 90% arithmetic -- identical body to benchgpu_arith_gpu.s so the
; two ROMs are directly comparable core-for-core.  JUMP rather than JR:
; same reason as Task 2 -- the loop body is 20 instructions long and JR's
; signed relative offset can't span that (lyxass rejects it, "Relative
; distance to large").
;
; Registers r3-r10 are scratch accumulators fed into each other so the
; work can't be elided; their final values are never read back -- only
; the iteration count in r1 and the fixed magics matter to the harness.
;
; NOTE on the DSP's local-RAM interrupt vectors (docs/jtrm-register-map.md):
; $F1B000 is also DSP Int 0's (CPU interrupt) vector slot. This program
; never enables D_CPUENA (or any D_FLAGS interrupt bit), so nothing ever
; fetches through that vector while this code runs -- `start:` at $F1B000
; is safe to treat as a plain code entry point, matching how benchgpu_-
; arith_gpu.s treats GPU local RAM's own Int 0 vector at $F03000 the same
; way (see docs/jtrm-register-map.md's GPU interrupt vector table).

MB_SENT_BASE    .equ    $00010000
MB_SENT_START   .equ    MB_SENT_BASE+0
MB_SENT_DONE    .equ    MB_SENT_BASE+4
MB_SENT_COUNT   .equ    MB_SENT_BASE+8

MB_MAGIC_START  .equ    $C0DE57A7
MB_MAGIC_DSP    .equ    $C0DE0D53

; ITERATIONS: 2,000,000 passes of the 20-instruction body above, same
; count as benchgpu_arith_gpu.s for direct comparability. See README.md
; for the measured completion frame -- re-measure and update both this
; comment and the tool's budget block if this value changes.
ITERATIONS      .equ    2000000

        .dsp
        .org    $F1B000
        .run
start:
        ; MB_SENT_START/MB_MAGIC_START are written by the 68K at cart boot
        ; (cartboot.inc convention, Task 1) -- do not write them again here.
        moveq   #0,r1                  ; iterations completed
        movei   #ITERATIONS,r2         ; countdown

        ; seed the scratch accumulators so the ALU ops have real operands
        moveq   #1,r3
        moveq   #2,r4
        moveq   #3,r5
        moveq   #4,r6
        moveq   #5,r7
        moveq   #6,r8
        moveq   #7,r9
        moveq   #8,r10
        movei   #loop,r11              ; JR's signed offset can't reach
                                        ; across a 20-instruction body --
                                        ; same lyxass rejection as Task 2.
                                        ; JUMP (Rn) has no such range limit.

loop:
        add     r3,r4                  ; 1
        sub     r5,r6                  ; 2
        and     r7,r8                  ; 3
        or      r9,r10                 ; 4
        add     r4,r3                  ; 5
        sub     r6,r5                  ; 6
        and     r8,r7                  ; 7
        or      r10,r9                 ; 8
        add     r3,r4                  ; 9
        sub     r5,r6                  ; 10
        and     r7,r8                  ; 11
        or      r9,r10                 ; 12
        add     r4,r3                  ; 13
        sub     r6,r5                  ; 14
        and     r8,r7                  ; 15
        or      r10,r9                 ; 16
        addq    #1,r1                  ; 17 (count++)
        subq    #1,r2                  ; 18 (countdown--)
        jump    ne,(r11)               ; 19 (control)
        nop                            ; 20 (delay slot -- control overhead)

        ; Count first, magic second: a reader that sees DONE is then
        ; guaranteed to see a settled count as well (same ordering rule
        ; as bench68k.s / cartboot.inc).
        movei   #MB_SENT_COUNT,r14
        store   r1,(r14)
        movei   #MB_MAGIC_DSP,r0
        movei   #MB_SENT_DONE,r14
        store   r0,(r14)

halt:
        jr      halt
        nop
        .end
