;
; tests/blitter/copy_pix32.s - 2-pixel 32bpp blitter copy round-trip.
;
; pixsize=5 (32bpp), one phrase = 2 pixels (8 bytes).
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
                move.l  #$DEADBEEF,(a0)+
                move.l  #$CAFEBABE,(a0)+

                lea     DST.l,a0
                clr.l   (a0)+
                clr.l   (a0)+

                ;; A?_FLAGS for 32bpp (pixsize=5) phrase mode:
                ;; pixsize=5 -> bits 3..5 = 101 = $28
                ;; e=1 (2 phrase pixels) -> bits 11..14 = $0800
                ;; xadd=phrase=00 -> bits 16..17 = 0
                ;; result: $00000828
                move.l  #DST,B_A1_BASE
                move.l  #$00000828,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00000828,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010002,B_COUNT      ; inner=2 px, outer=1
                move.l  #$01800001,B_COMMAND

                ;; Blitter is synchronous in this emulator; no wait needed.

.done:
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
