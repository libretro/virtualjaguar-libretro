;
; tests/blitter/dsta2_swap.s - DSTA2 bit swaps roles of A1/A2.
;
; Normally A1 = dest, A2 = source.  When DSTA2 (command bit 11 = $0800)
; is set, A2 becomes the destination and A1 becomes the source.
; Performs a plain LFU=S copy with the registers swapped to verify
; the data still flows correctly with the role-swap.
;
; Command bits:
;   SRCEN=1   (bit 0)
;   DSTA2=1   (bit 11) -> $00000800
;   ity=$C000 (LFU=S short-form)
; -> $0001C801
;
; Detail codes:
;   1 = DST hi long mismatch
;   2 = DST lo long mismatch
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

                move.l  #$CAFEBABE,SRC.l
                move.l  #$DEADBEEF,SRC+4.l
                move.l  #$AAAAAAAA,DST.l
                move.l  #$AAAAAAAA,DST+4.l

                ;; With DSTA2, A2 = dest, A1 = source.  Wire the
                ;; addresses accordingly.
                move.l  #SRC,B_A1_BASE          ; A1 = source
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #DST,B_A2_BASE          ; A2 = dest
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                move.l  #$01800801,B_COMMAND    ; SRCEN | DSTA2 | LFU=S

                move.l  DST.l,d5
                cmp.l   #$CAFEBABE,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #$DEADBEEF,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#$CAFEBABE
.bad2:          ACID_FAIL #2,d5,#$DEADBEEF
