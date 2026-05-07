;
; tests/blitter/lfu_passthrough_dst.s - LFU=$A (D); dest passes through.
;
; The LFU function evaluates to D unchanged, so a blit with garbage
; SRC and known DST must leave DST identical to its pre-blit value.
; This is the "no-op" LFU and is the inverse of LFU=$C (S).
;
; Needs DSTEN.  SRCEN omitted (linter requires LFUs that don't read
; S to NOT set SRCEN).
;
; Detail codes:
;   1 = DST long 0 changed (LFU=$A wrongly modified D)
;   2 = DST long 1 changed
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000

SRC_NOISE       equ     $DEADBEEF
DST_VAL         equ     $CAFEBABE
EXPECTED        equ     DST_VAL

                org     $802000
entry:
                ACID_INIT

                ;; Garbage SRC -- must NOT influence DST.
                move.l  #SRC_NOISE,SRC.l
                move.l  #SRC_NOISE,SRC+4.l
                move.l  #DST_VAL,DST.l
                move.l  #DST_VAL,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_PIXLINECOUNTER
                move.l  #DSTEN|LFU_FN_A,B_COMMAND

                move.l  DST.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#EXPECTED
.bad2:          ACID_FAIL #2,d5,#EXPECTED
