;
; tests/bus/cpu_blitter_concurrent.s - 68K and blitter access RAM together.
;
; Issues a blitter copy and IMMEDIATELY (without waiting for it to
; finish) reads BOTH the source and the destination from 68K.  On real
; hardware bus arbitration would interleave; in our emulator the
; blitter is synchronous and runs to completion before the next 68K
; instruction resumes, so the read always succeeds.
;
; Strict assertion (tightened from "post-blit src correct"):
;   - SRC longwords match the original pre-blit pattern (blitter
;     didn't trash source)
;   - DST longwords match SRC bit-for-bit (blit actually completed
;     before the 68K read)
;
; Detail codes:
;   1 = post-blit SRC[0] differs from original
;   2 = post-blit SRC[1] differs from original
;   3 = DST[0] != SRC[0] (blit didn't run, or ran wrong)
;   4 = DST[1] != SRC[1]
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
SRC_VAL_0       equ     $DEADBEEF
SRC_VAL_1       equ     $CAFEBABE

                org     $802000
entry:
                ACID_INIT

                move.l  #SRC_VAL_0,SRC.l
                move.l  #SRC_VAL_1,SRC+4.l
                move.l  #$00000000,DST.l
                move.l  #$00000000,DST+4.l

                ;; A1=DST, A2=SRC, 16bpp phrase, 4 px = 1 phrase.
                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #$00010004,B_PIXLINECOUNTER
                ;; SRCEN | LFU=$C (S) -> $01000001
                move.l  #SRCEN|LFU_FN_C,B_COMMAND

                ;; Read SRC immediately -- on async hardware this
                ;; would race; here it should just succeed.
                move.l  SRC.l,d5
                cmp.l   #SRC_VAL_0,d5
                bne     .badSrc0
                move.l  SRC+4.l,d5
                cmp.l   #SRC_VAL_1,d5
                bne     .badSrc1

                ;; Now check DST got what we asked for.
                move.l  DST.l,d5
                cmp.l   #SRC_VAL_0,d5
                bne     .badDst0
                move.l  DST+4.l,d5
                cmp.l   #SRC_VAL_1,d5
                bne     .badDst1

                ACID_PASS

.badSrc0:       ACID_FAIL #1,d5,#SRC_VAL_0
.badSrc1:       ACID_FAIL #2,d5,#SRC_VAL_1
.badDst0:       ACID_FAIL #3,d5,#SRC_VAL_0
.badDst1:       ACID_FAIL #4,d5,#SRC_VAL_1
