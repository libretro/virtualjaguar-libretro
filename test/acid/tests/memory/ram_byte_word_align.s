;
; tests/memory/ram_byte_word_align.s - mixed access widths at one address.
;
; Writes $12345678 as a long, then reads it back as 4 bytes
; ($12,$34,$56,$78) and 2 words ($1234,$5678).  Same value, different
; access widths.  Catches dispatch-path mismatches where byte / word
; reads don't agree with long writes in the byte-swap macros.
;
; Detail codes:
;   1 = high byte ($12) wrong
;   2 = byte $34 wrong
;   3 = byte $56 wrong
;   4 = low byte ($78) wrong
;   5 = high word ($1234) wrong
;   6 = low word ($5678) wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

BUF             equ     $00080000

                org     $802000
entry:
                ACID_INIT

                move.l  #$12345678,BUF.l

                ;; Byte reads.
                move.b  BUF.l,d5
                and.l   #$FF,d5
                cmp.l   #$12,d5
                bne     .b0_bad
                move.b  BUF+1.l,d5
                and.l   #$FF,d5
                cmp.l   #$34,d5
                bne     .b1_bad
                move.b  BUF+2.l,d5
                and.l   #$FF,d5
                cmp.l   #$56,d5
                bne     .b2_bad
                move.b  BUF+3.l,d5
                and.l   #$FF,d5
                cmp.l   #$78,d5
                bne     .b3_bad

                ;; Word reads.
                move.w  BUF.l,d5
                and.l   #$FFFF,d5
                cmp.l   #$1234,d5
                bne     .w0_bad
                move.w  BUF+2.l,d5
                and.l   #$FFFF,d5
                cmp.l   #$5678,d5
                bne     .w1_bad

                ACID_PASS

.b0_bad:        ACID_FAIL #1,d5,#$12
.b1_bad:        ACID_FAIL #2,d5,#$34
.b2_bad:        ACID_FAIL #3,d5,#$56
.b3_bad:        ACID_FAIL #4,d5,#$78
.w0_bad:        ACID_FAIL #5,d5,#$1234
.w1_bad:        ACID_FAIL #6,d5,#$5678
