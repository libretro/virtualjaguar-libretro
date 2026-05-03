;
; tests/blitter/copy_pix8_pixel.s - 8bpp pixel-mode (xadd=PIX) copy.
;
; Pair to copy_pix8.s.  Phrase mode there, here we test xadd=01
; (XADDPIX = add pixsize per pixel).  64 pixels (= 8 phrases) of 8bpp
; data are copied SRC->DST one pixel at a time; final memory image
; must be byte-identical to the source.
;
; FLAGS encoding for A1 (and A2):
;   pixsize=3 (8bpp):  bits 3..5 = 011  -> $00000018
;   width 64 px (m=0, e=4): bits 11..14 = 0100 -> $00002000
;   xadd=PIX (1):      bits 16..17 = 01  -> $00010000
;   pitch=0 (1):       bits 0..1 = 00
;   ----------------------------------------------- $00012018
;
; Detail codes:
;   N (1..16) = first mismatched longword index
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
N_LONGS         equ     16              ; 64 bytes = 8 phrases = 64 px @ 8bpp

FLAGS_PIX       equ     $00012018
COUNT_VAL       equ     $00010040       ; outer=1, inner=64 px

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC with a known recognizable pattern.
                lea     SRC.l,a0
                move.l  #N_LONGS-1,d0
                move.l  #$01020304,d1
.fill:          move.l  d1,(a0)+
                addq.l  #1,d1
                dbra    d0,.fill

                ;; Pre-fill DST with sentinel ($AA...) so a partial
                ;; copy is visible.
                lea     DST.l,a0
                move.l  #N_LONGS-1,d0
.zero:          move.l  #$AAAAAAAA,(a0)+
                dbra    d0,.zero

                ;; Configure blitter: SRC->DST 8bpp pixel mode.
                move.l  #DST,B_A1_BASE
                move.l  #FLAGS_PIX,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #FLAGS_PIX,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #COUNT_VAL,B_PIXLINECOUNTER
                move.l  #SRCEN|LFU_FN_C,B_COMMAND       ; SRCEN | LFU=S

                ;; Compare SRC vs DST byte-for-byte.
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
