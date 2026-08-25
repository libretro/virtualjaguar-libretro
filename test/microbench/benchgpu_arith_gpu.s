; test/microbench/benchgpu_arith_gpu.s -- GPU-side code for the GPU
; arithmetic-loop microbenchmark (#536, task 2).
;
; Assembled separately with lyxass (the GPU RISC has its own ISA, not the
; 68000's), then the 12-byte BS94 header is stripped and the raw code is
; embedded into the 68K bootstrap (benchgpu_arith.s) via .incbin.  See
; README.md "For tasks 2-5: embedding a lyxass GPU/DSP blob".
;
; Assemble:
;   lyxass -o benchgpu_arith_gpu.o benchgpu_arith_gpu.s
;   tail -c +13 benchgpu_arith_gpu.o > benchgpu_arith_gpu.bin   ; strip BS94 header
;
; The 68K loader copies this blob to GPU local RAM at $F03000 and starts
; execution there, so `.org $F03000` here matches where it will actually
; run (register-relative code, but jr targets are PC-relative so this
; mostly just documents intent).
;
; --- workload shape -----------------------------------------------------
; Per #536's dispatch-vs-body framing: this ROM isolates GPU *arithmetic*
; throughput -- the loop body is almost entirely ALU ops (ADD/SUB/AND/OR
; class), with exactly one taken branch (JR) closing the loop.  Task 3
; (benchgpu_branch) is the mirror image: branch-heavy, ALU-light.
;
; Body per iteration: 16 ALU ops (add/sub/and/or x4) + ADDQ (count++) +
; SUBQ (countdown--) = 18 arithmetic instructions, then a conditional
; JUMP (Rn) + its mandatory delay-slot NOP = 2 control instructions.
; 18/20 = 90% arithmetic.  JUMP rather than JR: the loop body is 20
; instructions long and JR's signed relative offset can't span that (see
; the `movei #loop,r11` comment below -- measured, not assumed).
;
; Registers r3-r10 are scratch accumulators fed into each other so the
; assembler/optimizer (there isn't one here, but for clarity) can't elide
; the work; their final values are never read back -- only the iteration
; count in r1 and the fixed magics matter to the harness.

MB_SENT_BASE    .equ    $00010000
MB_SENT_START   .equ    MB_SENT_BASE+0
MB_SENT_DONE    .equ    MB_SENT_BASE+4
MB_SENT_COUNT   .equ    MB_SENT_BASE+8

MB_MAGIC_START  .equ    $C0DE57A7
MB_MAGIC_GPUAR  .equ    $C0DE0A01

; ITERATIONS: 2,000,000 passes of the 20-instruction body above.  Nominal
; cost per pass (1 cycle/ALU op, ignoring load/store and branch-taken
; pipeline effects the JTRM documents but which don't apply here since
; there are no LOAD/STORE in the hot loop): ~20 cycles, so ~40M cycles at
; the GPU's ~26.6 MHz full-system clock ~= 1.5s of emulated time.  See
; README.md for the measured completion frame -- re-measure and update
; both this comment and the tool's budget block if this value changes.
ITERATIONS      .equ    2000000

        .gpu
        .org    $F03000
        .run
start:
        movei   #MB_SENT_START,r14
        movei   #MB_MAGIC_START,r0
        store   r0,(r14)

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
                                        ; measured: lyxass rejects it
                                        ; ("Relative distance to large").
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
        movei   #MB_MAGIC_GPUAR,r0
        movei   #MB_SENT_DONE,r14
        store   r0,(r14)

halt:
        jr      halt
        nop
        .end
