;
; tests/stress/deep_call_chain.s - 16-deep BSR/RTS nest.
;
; Calls level1 -> ... -> level16, each setting a unique bit in d6,
; then unwinds.  Strict assertion (tightened from "16 flags only"):
;
;   1. all 16 low bits of d6 set ($0000FFFF)
;   2. SP after the unwind exactly equals SP before the first BSR
;      (no leaked words)
;   3. SR (T/S/IPL) after the unwind matches what it was before
;
; Detail codes:
;   1 = flag bitmap mismatch
;   2 = SP shifted (stack leak in BSR/RTS path)
;   3 = SR T/S/IPL changed across the call chain
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

EXPECTED        equ     $0000FFFF
SR_MASK         equ     $E700           ; T1|T0|S|IPL

                org     $802000
entry:
                ACID_INIT

                ;; Snapshot SP and SR (architectural bits only) BEFORE
                ;; the call chain.
                move.l  a7,d4                   ; d4 = saved SP
                move.w  sr,d3
                and.l   #SR_MASK,d3             ; d3 = saved SR bits

                moveq   #0,d6
                bsr     .l1

                ;; Check 1: flag bitmap.
                cmp.l   #EXPECTED,d6
                bne.s   .badflags

                ;; Check 2: SP intact.
                move.l  a7,d5
                cmp.l   d4,d5
                bne.s   .badsp

                ;; Check 3: SR intact.
                move.w  sr,d5
                and.l   #SR_MASK,d5
                cmp.l   d3,d5
                bne.s   .badsr

                ACID_PASS

.badflags:      ACID_FAIL #1,d6,#EXPECTED
.badsp:         ACID_FAIL #2,d5,d4
.badsr:         ACID_FAIL #3,d5,d3

.l1:    bset #0,d6
        bsr.s   .l2
        rts
.l2:    bset #1,d6
        bsr.s   .l3
        rts
.l3:    bset #2,d6
        bsr.s   .l4
        rts
.l4:    bset #3,d6
        bsr.s   .l5
        rts
.l5:    bset #4,d6
        bsr.s   .l6
        rts
.l6:    bset #5,d6
        bsr.s   .l7
        rts
.l7:    bset #6,d6
        bsr.s   .l8
        rts
.l8:    bset #7,d6
        bsr.s   .l9
        rts
.l9:    bset #8,d6
        bsr.s   .l10
        rts
.l10:   bset #9,d6
        bsr.s   .l11
        rts
.l11:   bset #10,d6
        bsr.s   .l12
        rts
.l12:   bset #11,d6
        bsr.s   .l13
        rts
.l13:   bset #12,d6
        bsr.s   .l14
        rts
.l14:   bset #13,d6
        bsr.s   .l15
        rts
.l15:   bset #14,d6
        bsr.s   .l16
        rts
.l16:   bset #15,d6
        rts
