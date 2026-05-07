;
; tests/gpu/gpu_reg_access.s - 68K can write GPU work RAM and read it back.
;
; The GPU's program/data RAM at $F03000..$F04000 must be writable
; from the 68K side, and reads must return what was written.  This
; is the basis for loading any GPU program from 68K.
;
; Detail codes:
;   1 = readback from $F03000 wrong
;   2 = readback from $F03100 wrong
;   3 = readback from $F03FFC wrong (last addressable word)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

GPU_RAM         equ     $00F03000

                org     $802000
entry:
                ACID_INIT

                move.l  #$DEADBEEF,GPU_RAM.l
                move.l  GPU_RAM.l,d5
                cmp.l   #$DEADBEEF,d5
                bne.s   .bad1

                move.l  #$CAFEBABE,GPU_RAM+$100.l
                move.l  GPU_RAM+$100.l,d5
                cmp.l   #$CAFEBABE,d5
                bne.s   .bad2

                move.l  #$11223344,GPU_RAM+$FFC.l
                move.l  GPU_RAM+$FFC.l,d5
                cmp.l   #$11223344,d5
                bne.s   .bad3

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#$DEADBEEF
.bad2:          ACID_FAIL #2,d5,#$CAFEBABE
.bad3:          ACID_FAIL #3,d5,#$11223344
