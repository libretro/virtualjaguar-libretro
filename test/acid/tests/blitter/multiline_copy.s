;
; tests/blitter/multiline_copy.s - copy 4 lines of 1 phrase each.
;
; Programs the blitter to do a 4-line × 1-phrase 16bpp copy with
; A1/A2 pitch=0 (contiguous).  Catches off-by-one in outer-loop
; line counting.
;
; Detail codes:
;   1 = blitter never finished
;   N = first mismatched longword (1-based, 1..8)
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

                ;; 4 lines × 4 px @ 16bpp = 4 longs total per side.
                lea     SRC.l,a0
                move.l  #$AAAAAAAA,(a0)+
                move.l  #$BBBBBBBB,(a0)+
                move.l  #$CCCCCCCC,(a0)+
                move.l  #$DDDDDDDD,(a0)+
                move.l  #$11111111,(a0)+
                move.l  #$22222222,(a0)+
                move.l  #$33333333,(a0)+
                move.l  #$44444444,(a0)+

                lea     DST.l,a0
                moveq   #7,d0
.zero:          clr.l   (a0)+
                dbra    d0,.zero

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00040004,B_COUNT      ; inner=4px, outer=4 lines
                move.l  #$01800001,B_COMMAND

                ;; Blitter is synchronous in this emulator; no wait needed.

.done:
                lea     SRC.l,a0
                lea     DST.l,a1
                moveq   #7,d2
                moveq   #1,d3
.cmp:           move.l  (a0)+,d4
                move.l  (a1)+,d5
                cmp.l   d4,d5
                bne.s   .bad
                addq.l  #1,d3
                dbra    d2,.cmp
                ACID_PASS

.bad:           ACID_FAIL d3,d5,d4
