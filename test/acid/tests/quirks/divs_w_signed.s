;
; tests/quirks/divs_w_signed.s - signed 16-bit DIVS.W with negative
; inputs.
;
; DIVS.W <ea>,Dn divides the 32-bit signed Dn by a 16-bit signed
; <ea>.  Result lands in Dn:
;     low word  = quotient  (signed)
;     high word = remainder (signed; sign follows DIVIDEND on 68000)
;
; Case A:  D0 = -10, DIVS.W #-3, D0
;          quotient  = -10 / -3 =  3  -> low  word = $0003
;          remainder = -10 - (3*-3) = -10 - (-9) = -1 -> hi word = $FFFF
;          expected D0 = $FFFF0003
;
; Case B:  D0 = -10, DIVS.W #3, D0
;          quotient  = -10 / 3  = -3  -> low  word = $FFFD
;          remainder = -10 - (-3*3) = -1 -> hi word = $FFFF
;          expected D0 = $FFFFFFFD
;
; Detail codes:
;   1 = case A divergence; observed = D0 result, expected = $FFFF0003
;   2 = case B divergence; observed = D0 result, expected = $FFFFFFFD
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

                org     $802000
entry:
                ACID_INIT

                ;; -------- case A: -10 / -3 --------
                move.l  #-10,d0
                divs.w  #-3,d0
                cmp.l   #$FFFF0003,d0
                bne     .bad_a

                ;; -------- case B: -10 / 3 --------
                move.l  #-10,d0
                divs.w  #3,d0
                cmp.l   #$FFFFFFFD,d0
                bne     .bad_b

                ACID_PASS

.bad_a:         ACID_FAIL #1,d0,#$FFFF0003
.bad_b:         ACID_FAIL #2,d0,#$FFFFFFFD
