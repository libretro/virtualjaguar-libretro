;
; tests/blitter/lfu_or.s - LFU=$E (S | D).
;
; Pre-set DST=$F0F0F0F0_F0F0F0F0, SRC=$0F0F0F0F_0F0F0F0F.  Result
; must be $FFFFFFFF_FFFFFFFF.  Requires both SRCEN and DSTEN so the
; blitter reads the existing destination as the D operand.
;
; Command bits:
;   SRCEN=1 (bit 0)
;   DSTEN=1 (bit 5) -> $00000020
;   LFU = $E -> 1110 in bits 21..24 -> $01C00000
; -> $01C00021
;
; Detail codes:
;   1 = DST hi long not all-ones
;   2 = DST lo long not all-ones
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

                org     $802000
entry:
                ACID_INIT

                move.l  #$0F0F0F0F,SRC.l
                move.l  #$0F0F0F0F,SRC+4.l
                move.l  #$F0F0F0F0,DST.l
                move.l  #$F0F0F0F0,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                move.l  #$01C00021,B_COMMAND    ; SRCEN | DSTEN | LFU=$E (S|D)

                move.l  DST.l,d5
                cmp.l   #$FFFFFFFF,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #$FFFFFFFF,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#$FFFFFFFF
.bad2:          ACID_FAIL #2,d5,#$FFFFFFFF
