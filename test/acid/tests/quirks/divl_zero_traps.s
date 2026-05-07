;
; tests/quirks/divl_zero_traps.s - DIVS.L by zero traps to vector 5.
;
; The 68020-style 32-bit DIVS.L is one of the opcodes our 68K core
; emulates via IllegalOpcode (PR #119).  When the divisor is zero,
; the emulation must dispatch a "zero divide" trap to vector 5
; ($00000014), just like the native 68000 DIV.W behaviour.
;
; Approach: install a tiny trap handler at vector 5 that sets d6=1,
; then execute `divs.l d4,d3` with d4=0.  Encoded as $4C44,$3800
; (DIVL base $4C40 vs MULL base $4C00 in illegal_opcode_traps.s).
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
                move.l  #12345,d3              ; dividend
                moveq   #0,d4                   ; divisor = 0

                ;; divs.l d4,d3  =>  $4C44,$3800
                ;; opcode $4C44: base $4C40 (DIVL), mode 0 (Dn), reg 4 (d4 src)
                ;; ext   $3800:
                ;;   bits14-12 Dl=3 (quotient/dividend in d3)
                ;;   bit  11   sg=1 (signed)
                ;;   bit  10   sz=0 (32-bit, no Dh)
                ;;   bits  2-0 Dh=0 (don't-care)
                dc.w    $4C44,$3800

                tst.l   d6
                beq.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d6,#1

.zdiv_handler:
                moveq   #1,d6
                rte
