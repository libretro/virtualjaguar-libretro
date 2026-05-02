;
; tests/blitter/pattern_fill.s - PATDSEL fills destination from B_PATD.
;
; Programs the blitter without SRCEN, with PATDSEL set, and a known
; pattern in B_PATD.  Each phrase write should land the pattern.
;
; Detail codes:
;   1 = blitter never finished
;   N = first mismatched longword (1-based, 1..2)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

B_BASE          equ     $F02200
B_A1_BASE       equ     B_BASE + $00
B_A1_FLAGS      equ     B_BASE + $04
B_A1_PIXEL      equ     B_BASE + $0C
B_PATD_HI       equ     B_BASE + $50
B_PATD_LO       equ     B_BASE + $54
B_COMMAND       equ     B_BASE + $38
B_COUNT         equ     B_BASE + $3C

DST             equ     $00090000
PAT_HI          equ     $DEADBEEF
PAT_LO          equ     $CAFEBABE
SPIN_LIMIT      equ     1000000

                org     $802000
entry:
                ACID_INIT

                lea     DST.l,a0
                clr.l   (a0)+
                clr.l   (a0)+

                ;; Load pattern into B_PATD (64-bit; hi long then lo long).
                move.l  #PAT_HI,B_PATD_HI
                move.l  #PAT_LO,B_PATD_LO

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS   ; 16bpp phrase
                move.l  #0,B_A1_PIXEL

                move.l  #$00010004,B_COUNT      ; 4 px = 1 phrase
                ;; Command:
                ;;   PATDSEL = bit 16 = $00010000
                ;;   No SRCEN (we're filling from pattern).
                move.l  #$00010000,B_COMMAND

                ;; Blitter is synchronous in this emulator; no wait needed.

.done:
                ;; Compare DST against pattern.
                move.l  DST.l,d5
                cmp.l   #PAT_HI,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #PAT_LO,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#PAT_HI
.bad2:          ACID_FAIL #2,d5,#PAT_LO
