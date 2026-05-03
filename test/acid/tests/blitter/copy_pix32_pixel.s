;
; tests/blitter/copy_pix32_pixel.s - 32bpp pixel-mode copy.
;
; Pair to copy_pix32.s.  16 px @ 32bpp via xadd=PIX.
;
; FLAGS:
;   pixsize=5 (32bpp): bits 3..5 = 101 -> $28
;   width 16 (m=0,e=2): bits 11..14 = 0010 -> $1000
;   xadd=PIX (1):      $00010000
;   ----------------------------- $00011028
;
; Detail codes:
;   N (1..16) = first mismatched longword index
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
N_LONGS         equ     16

FLAGS           equ     $00011028
COUNT_VAL       equ     $00010010       ; outer=1, inner=16 px

                org     $802000
entry:
                ACID_INIT

                lea     SRC.l,a0
                move.l  #N_LONGS-1,d0
                move.l  #$DEADBEEF,d1
.fill:          move.l  d1,(a0)+
                add.l   #$11223344,d1
                dbra    d0,.fill

                lea     DST.l,a0
                move.l  #N_LONGS-1,d0
.sent:          move.l  #$A5A55A5A,(a0)+
                dbra    d0,.sent

                move.l  #DST,B_A1_BASE
                move.l  #FLAGS,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #FLAGS,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #COUNT_VAL,B_PIXLINECOUNTER
                move.l  #SRCEN|LFU_FN_C,B_COMMAND

                lea     SRC.l,a0
                lea     DST.l,a1
                move.l  #N_LONGS-1,d2
                moveq   #1,d3
.cmp:           move.l  (a0)+,d4
                move.l  (a1)+,d5
                cmp.l   d4,d5
                bne     .bad
                addq.l  #1,d3
                dbra    d2,.cmp

                ACID_PASS

.bad:           ACID_FAIL d3,d5,d4
