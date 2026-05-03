;
; tests/quirks/abcd_nbcd.s - BCD arithmetic (ABCD / NBCD).
;
; Both ABCD and NBCD include the X bit of CCR in their operation:
;     ABCD Dy,Dx :  Dx.b = (Dx.b + Dy.b + X)   in BCD
;     NBCD Dn    :  Dn.b = (0   - Dn.b - X)    in BCD
;
; We clear X first via `move #0,ccr` so the results are deterministic
; and match the simple 25+37=62 / 100-50=50 expectations.
;
; Detail codes:
;   1 = ABCD result wrong; observed = D1.b, expected = $62
;   2 = NBCD result wrong; observed = D2.b, expected = $50
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

                org     $802000
entry:
                ACID_INIT

                ;; Clear CCR (X = 0).
                move    #0,ccr

                ;; -------- ABCD case: 25 + 37 = 62 --------
                ;; Pre-load high bits with sentinels so a wrong-size
                ;; write (e.g. .w / .l instead of .b) is detectable.
                move.l  #$11111125,d0
                move.l  #$22222237,d1
                abcd    d0,d1
                ;; Expect D1 = $222222 62 (low byte updated, others
                ;; unchanged).
                cmp.l   #$22222262,d1
                bne     .bad_abcd

                ;; -------- NBCD case: 0 - 50 = 50 (BCD 10s complement) --------
                ;; Re-clear X in case ABCD set it.
                move    #0,ccr
                move.l  #$33333350,d2
                nbcd    d2
                cmp.l   #$33333350,d2
                bne     .bad_nbcd

                ACID_PASS

.bad_abcd:      ACID_FAIL #1,d1,#$22222262
.bad_nbcd:      ACID_FAIL #2,d2,#$33333350
