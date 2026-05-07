;
; tests/blitter/copy_pix8.s - 8-pixel 8bpp blitter copy round-trip.
;
; pixsize=3 (8bpp), one phrase = 8 pixels.
;
; Detail codes:
;   1 = blitter never finished
;   N = first mismatched longword index (1-based, 1..2)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

B_BASE          equ     $F02200
B_A1_BASE       equ     B_BASE + $00
B_A1_FLAGS      equ     B_BASE + $04
B_A1_PIXEL      equ     B_BASE + $0C
B_A2_BASE       equ     B_BASE + $24
B_A2_FLAGS      equ     B_BASE + $28
B_A2_PIXEL      equ     B_BASE + $30
B_COMMAND       equ     B_BASE + $38
B_COUNT         equ     B_BASE + $3C

SRC             equ     $00080000
DST             equ     $00090000
SPIN_LIMIT      equ     1000000

                org     $802000
entry:
                ACID_INIT

                lea     SRC.l,a0
                move.l  #$01020304,(a0)+
                move.l  #$05060708,(a0)+

                lea     DST.l,a0
                clr.l   (a0)+
                clr.l   (a0)+

                ;; A?_FLAGS for 8bpp (pixsize=3) phrase mode:
                ;; pixsize=3 -> bits 3..5 = 011 = $18
                ;; e=3 (8 phrase pixels) -> bits 11..14 = $1800
                ;; xadd=phrase=00 -> bits 16..17 = 0
                ;; result: $00001818
                move.l  #DST,B_A1_BASE
                move.l  #$00001818,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001818,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010008,B_COUNT      ; inner=8 px, outer=1
                move.l  #$01800001,B_COMMAND

                ;; Blitter is synchronous in this emulator; no wait needed.

.done:
                ;; Compare 2 longwords (8 bytes = 8 pixels at 8bpp).
                lea     SRC.l,a0
                lea     DST.l,a1
                moveq   #1,d2
                moveq   #1,d3
.cmp:           move.l  (a0)+,d4
                move.l  (a1)+,d5
                cmp.l   d4,d5
                bne.s   .bad
                addq.l  #1,d3
                dbra    d2,.cmp
                ACID_PASS

.bad:           ACID_FAIL d3,d5,d4
