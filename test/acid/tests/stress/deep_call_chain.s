;
; tests/stress/deep_call_chain.s - 16-deep BSR/RTS nest.
;
; Calls level1 -> level2 -> ... -> level16, each setting a unique
; bit in d6, then unwinds.  After all returns, d6 should have all
; 16 low bits set ($0000FFFF).  Verifies stack push/pop survives a
; 16-deep call chain.
;
; Detail codes:
;   1 = some level's bit was not set after unwind
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

EXPECTED        equ     $0000FFFF

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d6
                bsr.s   .l1

                cmp.l   #EXPECTED,d6
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d6,#EXPECTED

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
