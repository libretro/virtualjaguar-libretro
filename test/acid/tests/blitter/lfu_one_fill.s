;
; tests/blitter/lfu_one_fill.s - LFU=$F (always 1) fills with all-ones.
;
; LFU function $F outputs all-ones regardless of source/dest.  Mirror
; of lfu_zero_fill but for the opposite constant.
;
; Command bits:
;   SRCEN=1 (bit 0)  (LFU still wants the source read)
;   LFU = $F -> 1111 in bits 21..24 -> $01E00000
; -> $01E00001
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

                move.l  #$00000000,SRC.l
                move.l  #$00000000,SRC+4.l
                move.l  #$00000000,DST.l
                move.l  #$00000000,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                move.l  #$01E00001,B_COMMAND    ; SRCEN | LFU=$F (always 1)

                move.l  DST.l,d5
                cmp.l   #$FFFFFFFF,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #$FFFFFFFF,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#$FFFFFFFF
.bad2:          ACID_FAIL #2,d5,#$FFFFFFFF
