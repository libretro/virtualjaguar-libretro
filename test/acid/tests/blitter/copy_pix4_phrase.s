;
; tests/blitter/copy_pix4_phrase.s - 4bpp phrase-mode copy.
;
; 8 phrases (64 bytes = 128 px @ 4bpp).
;
; FLAGS:
;   pixsize=2 (4bpp):  bits 3..5 = 010 -> $00000010
;   width 128 (m=0,e=4): bits 11..14 = 0100 -> $00002000
;   xadd=PHR (0):      bits 16..17 = 00  -> $00000000
;   pitch=0 (1):       bits 0..1 = 00
;   ----------------------------- $00002010
;
; Detail codes:
;   N (1..16) = first mismatched longword index
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
N_LONGS         equ     16              ; 64 bytes = 8 phrases = 128 px @ 4bpp

FLAGS           equ     $00002010
COUNT_VAL       equ     $00010080       ; outer=1, inner=128

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC with a known recognizable pattern.
                lea     SRC.l,a0
                move.l  #N_LONGS-1,d0
                move.l  #$12345678,d1
.fill:          move.l  d1,(a0)+
                add.l   #$11111111,d1
                dbra    d0,.fill

                ;; Pre-fill DST with sentinel so a partial copy is visible.
                lea     DST.l,a0
                move.l  #N_LONGS-1,d0
.sent:          move.l  #$A5A55A5A,(a0)+
                dbra    d0,.sent

                ;; Configure blitter: SRC->DST 4bpp phrase mode.
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
