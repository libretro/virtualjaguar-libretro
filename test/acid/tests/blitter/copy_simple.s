;
; tests/blitter/copy_simple.s - 4-pixel 16bpp blitter copy round-trip.
;
; Detail codes:
;   1 = blitter never finished (BUSY stayed set)
;   N = first mismatched longword index (1-based)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;; Blitter register file lives at TOM_BASE + $2200.
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
                move.l  #$AABBCCDD,(a0)+
                move.l  #$11223344,(a0)+
                move.l  #$DEADBEEF,(a0)+
                move.l  #$CAFEBABE,(a0)+
                move.l  #$0BADF00D,(a0)+
                move.l  #$FACEFEED,(a0)+
                move.l  #$F00DBEEF,(a0)+
                move.l  #$DEADC0DE,(a0)+

                lea     DST.l,a0
                moveq   #7,d0
.zerodest:      clr.l   (a0)+
                dbra    d0,.zerodest

                ;; A?_FLAGS: pixsize=4(16bpp), xadd=phrase=00, e=2 (4-px phrase)
                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                move.l  #$01800001,B_COMMAND    ; SRCEN | LFU=src

                ;; Blitter is synchronous in this emulator; no wait needed.

.blit_done:
                lea     SRC.l,a0
                lea     DST.l,a1
                moveq   #7,d2
                moveq   #1,d3
.compare:       move.l  (a0)+,d4
                move.l  (a1)+,d5
                cmp.l   d4,d5
                bne.s   .mismatch
                addq.l  #1,d3
                dbra    d2,.compare
                ACID_PASS

.mismatch:      ACID_FAIL d3,d5,d4
