;
; tests/blitter/copy_pix2_phrase.s - 2bpp phrase-mode copy.
;
; 4 phrases (32 bytes = 128 px @ 2bpp).
;
; FLAGS:
;   pixsize=1 (2bpp):  bits 3..5 = 001 -> $00000008
;   width 128 (m=0,e=4): bits 11..14 = 0100 -> $00002000
;   xadd=PHR (0):      bits 16..17 = 00  -> $00000000
;   pitch=0 (1):       bits 0..1 = 00
;   ----------------------------- $00002008
;
; Detail codes:
;   N (1..8) = first mismatched longword index
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
N_LONGS         equ     8               ; 32 bytes = 4 phrases = 128 px @ 2bpp

FLAGS           equ     $00002008
COUNT_VAL       equ     $00010080       ; outer=1, inner=128

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC with a known recognizable pattern.
                lea     SRC.l,a0
                move.l  #N_LONGS-1,d0
                move.l  #$AABBCCDD,d1
.fill:          move.l  d1,(a0)+
                add.l   #$01010101,d1
                dbra    d0,.fill

                ;; Pre-fill DST with sentinel.
                lea     DST.l,a0
                move.l  #N_LONGS-1,d0
.sent:          move.l  #$55555555,(a0)+
                dbra    d0,.sent

                ;; Configure blitter: SRC->DST 2bpp phrase mode.
                move.l  #DST,B_A1_BASE
                move.l  #FLAGS,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #FLAGS,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #COUNT_VAL,B_PIXLINECOUNTER
                move.l  #SRCEN|LFU_FN_C,B_COMMAND

                ;; Compare SRC vs DST longword-by-longword.
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
