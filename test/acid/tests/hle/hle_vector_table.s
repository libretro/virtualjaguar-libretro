;
; tests/hle/hle_vector_table.s - 68K vector table is filled (no PRNG garbage).
;
; HLE init writes RTE stubs to vectors 4..255 ($10..$3FC).  Verify
; they're at least non-garbage by checking the IRQ vector at $100
; (vector 64) and a couple of high vectors.
;
; A wrong value here is exactly what bit us in the first acid bringup
; (signature originally lived at $100 and got overwritten by HLE
; stubs).  This test gates that the stubs ARE in place.
;
; Detail codes:
;   1 = vector 64 ($100) is zero (HLE init didn't fill it)
;   2 = vector 100 ($190) is zero
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

V_64            equ     $100
V_100           equ     $190

                org     $802000
entry:
                ACID_INIT

                move.l  V_64.l,d5
                tst.l   d5
                beq.s   .bad1

                move.l  V_100.l,d5
                tst.l   d5
                beq.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,#0,#1
.bad2:          ACID_FAIL #2,#0,#1
