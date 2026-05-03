;
; tests/timing/halfline_period_us.s - two consecutive HC=0 events
; should be ~63.5 us apart (NTSC scanline period).
;
; HC alternates between 0 and (0x0400 | HP/2) every halfline (per
; src/tom/tom.c:792-801).  Two consecutive HC==0 samples therefore
; bracket exactly one full scanline (= two halflines).  The NTSC
; scanline is 63.5 us.
;
; We count 68K loop iterations between two HC=0 events.  Each
; iteration is calibrated at ~CYCLES_PER_ITER 68K cycles.
; Expected cycle count for one scanline:
;   63.5 us * 13.295 MHz = ~844 68K cycles
; Tolerance window [60, 70] us = [798, 930] cycles.
;
; The assertion is necessarily loose because we can't measure
; cycles directly from inside the 68K -- we sample wall time via
; HC transitions and count loop iterations.  But it's still
; strict enough to catch order-of-magnitude drift.
;
; Detail codes:
;   1 = observed cycle estimate outside [798, 930]
;       observed = estimated cycles, expected = 844
;   2 = never saw HC = 0 (HC stuck non-zero)
;   3 = never saw HC transition from 0 to non-zero (HC stuck at 0)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; The inner spin loop body is:
;;   move.w TOM_HC,d3   ; ~12 cycles MMIO read
;;   tst.w  d3          ; 4 cycles
;;   beq.s  .got_zero   ; 8/10 cycles
;;   addq.l #1,d2       ; 8 cycles
;;   bra.s  .spin_loop  ; 10 cycles
;; Approx 42 cycles per iteration of the not-taken path.
CYCLES_PER_ITER equ     42

;; Expected cycle window for a single NTSC scanline = 63.5 us
;; at 13.295 MHz = 844 cycles.  Accept [60, 70] us = [798, 930].
EXPECT_CYCLES   equ     844
LO_CYCLES       equ     798
HI_CYCLES       equ     930

SPIN_LIMIT      equ     1000000

                org     $802000
entry:
                ACID_INIT

                ;; -------- step 1: wait for HC == 0 (start of scanline) --------
                move.l  #SPIN_LIMIT,d4
.wait_zero1:    move.w  TOM_HC,d3
                tst.w   d3
                beq.s   .got_zero1
                subq.l  #1,d4
                bne.s   .wait_zero1
                ACID_FAIL #2,d3,#0
.got_zero1:

                ;; -------- step 2: wait for HC != 0 (mid-scanline) --------
                move.l  #SPIN_LIMIT,d4
.wait_nz:       move.w  TOM_HC,d3
                tst.w   d3
                bne.s   .got_nz
                subq.l  #1,d4
                bne.s   .wait_nz
                ACID_FAIL #3,d3,#1
.got_nz:

                ;; -------- step 3: count iterations until next HC == 0 --------
                ;; Now we're inside a scanline.  Spin counting iterations
                ;; until HC returns to 0 (next scanline boundary).
                ;; We must FIRST wait for a non-zero -> non-zero transition
                ;; to skip the half we're currently in.  Simpler: just
                ;; wait for the next zero, then start the actual count.
                move.l  #SPIN_LIMIT,d4
.wait_zero2:    move.w  TOM_HC,d3
                tst.w   d3
                beq.s   .got_zero2
                subq.l  #1,d4
                bne.s   .wait_zero2
                ACID_FAIL #2,d3,#0
.got_zero2:

                ;; Now spin counting until we get a *full* scanline (two
                ;; halflines) -- need to see non-zero AGAIN, then zero AGAIN.
                ;; First skip past the current zero phase.
                move.l  #SPIN_LIMIT,d4
.skip_zero:     move.w  TOM_HC,d3
                tst.w   d3
                bne.s   .skip_done
                subq.l  #1,d4
                bne.s   .skip_zero
                ACID_FAIL #3,d3,#1
.skip_done:

                ;; -------- step 4: counted loop until next HC == 0 --------
                moveq   #0,d2                   ; iteration counter
                move.l  #SPIN_LIMIT,d4
.spin_loop:     move.w  TOM_HC,d3
                tst.w   d3
                beq.s   .scanline_end
                addq.l  #1,d2
                subq.l  #1,d4
                bne.s   .spin_loop
                ;; Spin budget exhausted before HC returned to zero.
                ACID_FAIL #2,d2,#EXPECT_CYCLES
.scanline_end:

                ;; d2 = iterations.  Convert to estimated 68K cycles:
                ;;     cycles = iters * CYCLES_PER_ITER
                ;; Use mulu.w (16x16 -> 32) since both fit easily.
                move.l  d2,d5
                mulu.w  #CYCLES_PER_ITER,d5     ; d5 = estimated cycles

                ;; Assert d5 in [LO_CYCLES, HI_CYCLES].
                cmp.l   #LO_CYCLES,d5
                blt     .out_of_range
                cmp.l   #HI_CYCLES,d5
                bgt     .out_of_range

                ACID_PASS

.out_of_range:
                ACID_FAIL #1,d5,#EXPECT_CYCLES
