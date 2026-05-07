;
; tests/blitter/lfu_zero_fill.s - LFU=0 must zero the destination.
;
; LFU function 0 outputs zero regardless of source/dest.  Combined
; with PATDSEL/no-write-source, this is the fast clear path many
; games use to wipe a buffer.
;
; Command bits: SRCEN=1 (read source for the LFU), LFU bits =
; (cmd >> 21) & 0xF = 0.  ity bits at >>14 = 0.
; -> $00000001
;
; Detail codes:
;   1 = dest not zero after LFU=0 blit
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

                ;; Pre-fill src + dest with non-zero so we can see
                ;; the zero overwrite.
                move.l  #$DEADBEEF,SRC.l
                move.l  #$CAFEBABE,SRC+4.l
                move.l  #$AAAAAAAA,DST.l
                move.l  #$BBBBBBBB,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #$00010004,B_COUNT
                move.l  #$00000001,B_COMMAND    ; SRCEN, LFU=0

                ;; Verify dest is zero.
                move.l  DST.l,d5
                tst.l   d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#0
