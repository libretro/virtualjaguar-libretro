; test/microbench/benchgpu_branch_gpu.s -- GPU-side code for the GPU
; branch-heavy-loop microbenchmark (#536, task 3).
;
; Assembled separately with lyxass (the GPU RISC has its own ISA, not the
; 68000's), then the 12-byte BS94 header is stripped and the raw code is
; embedded into the 68K bootstrap (benchgpu_branch.s) via .incbin.  See
; README.md "For tasks 2-5: embedding a lyxass GPU/DSP blob".
;
; Assemble:
;   lyxass -o benchgpu_branch_gpu.o benchgpu_branch_gpu.s
;   tail -c +13 benchgpu_branch_gpu.o > benchgpu_branch_gpu.bin   ; strip BS94 header
;
; The 68K loader copies this blob to GPU local RAM at $F03000 and starts
; execution there, so `.org $F03000` here matches where it will actually
; run.
;
; --- workload shape -----------------------------------------------------
; Per #536's dispatch-vs-body framing (issue's explicit motivation: "test
; dispatch-vs-body hypotheses (#532) directly instead of inferring them
; from a census"): this ROM is the mirror image of benchgpu_arith_gpu.s
; (task 2) -- that one is 90% ALU / 10% control, this one is dominated by
; conditional-branch dispatch with only two arithmetic ops in the whole
; body.
;
; Body per iteration: one CMPQ (sets flags from a constant comparison,
; never changes -- shared by every padding branch below) + 8 conditional
; JR/NOP pairs that are never taken (each JR's target label is exactly the
; instruction after its own delay-slot NOP, so taken or not-taken produce
; byte-identical control flow -- correctness never depends on which way
; the condition actually resolves) + ADDQ (count++) + SUBQ (countdown--)
; + a conditional JUMP/NOP pair that closes the loop.  1 CMPQ + 16
; JR/NOP + 1 JUMP/NOP = 19 control-dispatch instructions vs 2 arithmetic
; (ADDQ, SUBQ) out of 21 total -- ~90% dispatch, mirroring task 2's ~90%
; arithmetic.
;
; JR is used for the 8 padding branches (each only needs to skip its own
; one-instruction delay slot -- well inside JR's signed relative-offset
; range). The loop-closing branch reuses task 2's proven JUMP (Rn)
; pattern instead of JR: JR's range can't span a body this long (measured
; in task 2 -- lyxass rejects it with "Relative distance to large").
;
; Registers r3-r10 mirror task 2's scratch set even though only r3 is
; read here (the CMPQ operand) -- keeping the same seeded register file
; shape across every microbench GPU program means a state dump/diff tool
; can compare them apples to apples; their values are never modified in
; this loop, so no arithmetic hides inside the "branch" instructions.

MB_SENT_BASE    .equ    $00010000
MB_SENT_START   .equ    MB_SENT_BASE+0
MB_SENT_DONE    .equ    MB_SENT_BASE+4
MB_SENT_COUNT   .equ    MB_SENT_BASE+8

MB_MAGIC_START  .equ    $C0DE57A7
MB_MAGIC_GPUBR  .equ    $C0DE0B02

; ITERATIONS: 2,000,000 passes of the 21-instruction body above -- same
; iteration count as task 2 (benchgpu_arith_gpu.s) so the two ROMs'
; completion frames are directly comparable per-instruction, not just
; per-ROM.  See README.md for the measured completion frame -- re-measure
; and update both this comment and the tool's budget block if this value
; changes.
ITERATIONS      .equ    2000000

        .gpu
        .org    $F03000
        .run
start:
        ; MB_SENT_START/MB_MAGIC_START are written by the 68K at cart boot
        ; (cartboot.inc convention, Task 1) -- do not write them again here.
        moveq   #0,r1                  ; iterations completed
        movei   #ITERATIONS,r2         ; countdown

        ; seed the scratch accumulators -- same shape as task 2, unused
        ; here except r3 (the CMPQ operand below)
        moveq   #1,r3
        moveq   #2,r4
        moveq   #3,r5
        moveq   #4,r6
        moveq   #5,r7
        moveq   #6,r8
        moveq   #7,r9
        moveq   #8,r10
        movei   #loop,r11              ; JUMP target for the loop closer
                                        ; (JR can't span a body this long
                                        ; -- see task 2, measured)

loop:
        ; r3 is seeded to 1 and never modified, so this comparison against
        ; 2 is never equal on any iteration -- ZNC flags are 100%
        ; deterministic across the whole run, and every padding branch
        ; below tests the exact same result.
        cmpq    #2,r3                  ; 1  (flags only, not arithmetic
                                        ;    body work -- feeds the eight
                                        ;    dispatch tests that follow)

        jr      eq,cont1               ; 2  never taken
        nop                             ; 3  delay slot
cont1:
        jr      eq,cont2               ; 4  never taken
        nop                             ; 5
cont2:
        jr      eq,cont3               ; 6  never taken
        nop                             ; 7
cont3:
        jr      eq,cont4               ; 8  never taken
        nop                             ; 9
cont4:
        jr      eq,cont5               ; 10 never taken
        nop                             ; 11
cont5:
        jr      eq,cont6               ; 12 never taken
        nop                             ; 13
cont6:
        jr      eq,cont7               ; 14 never taken
        nop                             ; 15
cont7:
        jr      eq,cont8               ; 16 never taken
        nop                             ; 17
cont8:
        addq    #1,r1                  ; 18 (count++)
        subq    #1,r2                  ; 19 (countdown--, sets the flags
                                        ;    the loop closer below tests)
        jump    ne,(r11)               ; 20 (control)
        nop                             ; 21 (delay slot -- control overhead)

        ; Count first, magic second: a reader that sees DONE is then
        ; guaranteed to see a settled count as well (same ordering rule
        ; as bench68k.s / cartboot.inc / benchgpu_arith_gpu.s).
        movei   #MB_SENT_COUNT,r14
        store   r1,(r14)
        movei   #MB_MAGIC_GPUBR,r0
        movei   #MB_SENT_DONE,r14
        store   r0,(r14)

halt:
        jr      halt
        nop
        .end
