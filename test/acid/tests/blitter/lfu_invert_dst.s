;
; tests/blitter/lfu_invert_dst.s - LFU=$5 (~D); S is irrelevant.
;
; DST=$CCCC3333 -> ~DST = $3333CCCC.  SRC contents must NOT affect
; the result since LFU $5 ignores S; we plant a noisy SRC pattern
; ($DEADBEEF) to verify SRC really is irrelevant.
;
; Needs DSTEN.  SRCEN technically not required, but linter requires
; LFUs that don't use S to omit SRCEN, so we do.
;
; Detail codes:
;   1 = DST long 0 wrong
;   2 = DST long 1 wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000

SRC_NOISE       equ     $DEADBEEF
DST_VAL         equ     $CCCC3333
EXPECTED        equ     $3333CCCC

                org     $802000
entry:
                ACID_INIT

                ;; Noisy SRC -- result must be independent of these bits.
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
                ;; LFU=$5 (~D) doesn't use S, so SRCEN is omitted to
                ;; keep the linter happy.
                move.l  #DSTEN|LFU_FN_5,B_COMMAND

                move.l  DST.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #EXPECTED,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#EXPECTED
.bad2:          ACID_FAIL #2,d5,#EXPECTED
