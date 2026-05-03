;
; tests/blitter/lfu_xnor.s - LFU=$9 (~(S^D) = XNOR).
;
; SRC=$AAAA5555, DST=$CCCC3333:
;   Upper nybbles: ~(A^C) = ~(1010^1100) = ~0110 = 1001 -> 9
;   Lower nybbles: ~(5^3) = ~(0101^0011) = ~0110 = 1001 -> 9
;   -> result = $99999999
;
; Needs SRCEN+DSTEN.
;
; Detail codes:
;   1 = DST long 0 wrong
;   2 = DST long 1 wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000

SRC_VAL         equ     $AAAA5555
DST_VAL         equ     $CCCC3333
EXPECTED        equ     $99999999

                org     $802000
entry:
                ACID_INIT

                move.l  #SRC_VAL,SRC.l
                move.l  #SRC_VAL,SRC+4.l
                move.l  #DST_VAL,DST.l
                move.l  #DST_VAL,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_PIXLINECOUNTER
                move.l  #SRCEN|DSTEN|LFU_FN_9,B_COMMAND

                move.l  DST.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#EXPECTED
.bad2:          ACID_FAIL #2,d5,#EXPECTED
