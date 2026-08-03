;
; tests/timing/vc_advance.s - VC must monotonically advance per halfline.
;
; Poll VC in a tight loop and watch every transition it makes.  Each
; change must be a small forward step (or a wrap back to the top of the
; field); VC must never stall for the whole sample and never run
; backwards mid-field.
;
; This deliberately does NOT time a gap.  It used to sample VC twice
; across a 10_000-NOP wait and bound the delta by DELTA_MAX = 524, a
; number its own comment admitted was empirical ("a 10K-NOP wait crosses
; ~500 halflines on the emulator").  That made the bound a statement
; about 68K instruction throughput, not about VC: these ROMs execute
; from cart ROM at $802000, which at the reset MEMCON1 ($1861,
; ROMSPEED=0) costs 10 system clocks per fetch, so the wait is longer
; than the datasheet cycle counts suggest.  With the
; virtualjaguar_dram_timing model enabled the gap stretched and the
; delta hit 2096, failing detail=2 with nothing wrong with VC.
;
; Watching transitions instead is immune to CPU speed by construction:
; a faster CPU simply samples the same ramp more finely.  It is also a
; stronger assertion than the old one -- the old test could not tell a
; smooth per-halfline ramp from VC lurching in bursts.
;
; Must pass with virtualjaguar_dram_timing both enabled and disabled.
;
; Detail codes on FAIL:
;   1 = VC never changed across the whole sample (timing dead -- frozen)
;   2 = a forward step was larger than STEP_MAX (VC lurching, not ramping)
;   3 = too few transitions seen before the poll budget ran out
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; VC's bit 11 is the field flag (src/tom/tom.c) -- mask it off, or a
;; field flip reads as a huge jump.
VC_LINE_MASK    equ     $07FF

;; How many VC transitions to observe before declaring success.
WANT_STEPS      equ     200

;; Largest forward step we accept between two consecutive samples.  The
;; poll loop is a handful of instructions against a ~31.8 us halfline,
;; so real hardware cadence is +1; 8 leaves room for a slow host or a
;; heavier CPU timing model without accepting a genuine lurch.
STEP_MAX        equ     8

;; Poll budget, so a frozen VC fails instead of hanging.  Generous: it
;; only has to be big enough that a live VC reaches WANT_STEPS first.
POLL_BUDGET     equ     4000000

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d3                   ; transitions seen
                move.l  #POLL_BUDGET,d6

                move.w  TOM_VC,d1
                and.w   #VC_LINE_MASK,d1

.poll:          move.w  TOM_VC,d0
                and.w   #VC_LINE_MASK,d0
                cmp.w   d1,d0
                beq.s   .next                   ; unchanged, keep polling

                ;; Changed.  Forward step, or a wrap to the top of field?
                move.w  d0,d2
                sub.w   d1,d2
                bcs.s   .wrapped                ; d0 < d1: wrap, don't bound it

                ;; Forward: must be a small step.
                cmp.w   #STEP_MAX,d2
                bhi     .lurched

.wrapped:       addq.l  #1,d3
                move.w  d0,d1

.next:          subq.l  #1,d6
                beq.s   .budget_out
                cmp.l   #WANT_STEPS,d3
                blt.s   .poll

                ACID_PASS

.budget_out:
                ;; Ran out of polls.  Distinguish "never moved at all"
                ;; from "moved, just not enough" -- the first is a dead
                ;; timing path, the second only a too-small budget.
                tst.l   d3
                beq     .frozen
                ACID_FAIL #3,d3,#WANT_STEPS

.frozen:        ACID_FAIL #1,d3,#WANT_STEPS

.lurched:       ACID_FAIL #2,d2,#STEP_MAX
