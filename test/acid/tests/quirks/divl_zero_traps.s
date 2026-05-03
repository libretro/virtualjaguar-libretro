;
; tests/quirks/divl_zero_traps.s - DIVS.L by zero traps to vector 5.
;
; The 68020-style 32-bit DIVS.L is one of the opcodes our 68K core
; emulates via IllegalOpcode (PR #119).  When the divisor is zero,
; the emulation must dispatch a "zero divide" trap to vector 5
; ($00000014), just like the native 68000 DIV.W behaviour.
;
; Approach: install a tiny trap handler at vector 5 that sets d6=1,
; then execute `divs.l #0,d2` (inline-encoded as $4C3C,$0800).
; If the trap fires, d6 becomes 1 and the test passes.
;
; Detail codes:
;   1 = zero-divide handler never fired (d6 still 0)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

V_ZERODIV       equ     $00000014               ; vector 5

                org     $802000
entry:
                ACID_INIT

                ;; Install handler at vector 5.
                lea     .zdiv_handler,a0
                move.l  a0,V_ZERODIV.l

                moveq   #0,d6                   ; flag = 0
                move.l  #12345,d2

                ;; divs.l #0,d2  => $4C3C,$2800,$00000000
                ;; ($4C3C = DIV[?].L #imm; ext word fields:
                ;;   bit11 sg=1 (signed), bit10 sz=0 (32-bit),
                ;;   bits14-12 Dl=2, bits2-0 Dh=0 -> $2800)
                dc.w    $4C3C,$2800
                dc.l    $00000000

                tst.l   d6
                beq.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d6,#1

.zdiv_handler:
                moveq   #1,d6
                rte
