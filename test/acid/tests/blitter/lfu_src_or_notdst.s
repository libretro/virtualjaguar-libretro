;
; tests/blitter/lfu_src_or_notdst.s - LFU=$D (S | ~D).
;
; SRC=$AAAA5555, DST=$CCCC3333:
;   Upper nybbles: A | ~C = 1010 | 0011 = 1011 -> B
;   Lower nybbles: 5 | ~3 = 0101 | 1100 = 1101 -> D
;   -> result = $BBBBDDDD
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
EXPECTED        equ     $BBBBDDDD

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
                move.l  #SRCEN|DSTEN|LFU_FN_D,B_COMMAND

                move.l  DST.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#EXPECTED
.bad2:          ACID_FAIL #2,d5,#EXPECTED
