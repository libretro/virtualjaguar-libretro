;
; tests/quirks/bsr_long_61ff.s - BSR.W control / sanity test.
;
; Originally drafted as a placeholder for the BSR.L $61FF quirk before
; the real test (`bsr_l_61ff_real.s`, in this same directory) existed.
;
; Now repurposed as a BSR.W *sanity* gate -- if even a normal short-
; branch BSR doesn't round-trip, the bsr_l_61ff_real test is
; meaningless because we couldn't tell the failure was about the quirk
; vs about call/return at all.
;
; The actual $61FF Atari aln quirk coverage lives in
; `tests/quirks/bsr_l_61ff_real.s`, which emits the raw opcode
; bytes and the absolute target.
;
; Detail codes:
;   1 = BSR.W didn't return / target didn't run
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d6                   ; flag = "didn't return"
                bsr.w   .target                 ; standard BSR.W
                tst.l   d6
                beq.s   .no_return

                ACID_PASS

.no_return:     ACID_FAIL #1,d6,#1

.target:
                moveq   #1,d6
                rts
