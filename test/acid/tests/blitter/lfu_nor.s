;
; tests/blitter/lfu_nor.s - LFU=$1 (~S & ~D = ~(S|D) = NOR).
;
; Truth-table eval per nybble:
;   Upper nybbles: S=A(1010), D=C(1100) -> ~S & ~D = 0101 & 0011 = 0001
;   Lower nybbles: S=5(0101), D=3(0011) -> ~S & ~D = 1010 & 1100 = 1000
;   So with SRC=$AAAA5555, DST=$CCCC3333 -> result = $11118888.
;
; Needs SRCEN+DSTEN: LFU=$1 reads both operands.
;
; Detail codes:
;   1 = DST hi long (long 0) wrong
;   2 = DST lo long (long 1) wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000

SRC_VAL         equ     $AAAA5555
DST_VAL         equ     $CCCC3333
EXPECTED        equ     $11118888

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
                move.l  #SRCEN|DSTEN|LFU_FN_1,B_COMMAND

                move.l  DST.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#EXPECTED
.bad2:          ACID_FAIL #2,d5,#EXPECTED
