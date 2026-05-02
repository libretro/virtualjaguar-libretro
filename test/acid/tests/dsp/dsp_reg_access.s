;
; tests/dsp/dsp_reg_access.s - 68K can write DSP work RAM and read it back.
;
; Same shape as gpu/gpu_reg_access but for DSP at $F1B000..$F1D000.
;
; Detail codes:
;   1 = $F1B000 readback wrong
;   2 = $F1B100 readback wrong
;   3 = $F1BFFC readback wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

DSP_RAM         equ     $00F1B000

                org     $802000
entry:
                ACID_INIT

                move.l  #$DEADBEEF,DSP_RAM.l
                move.l  DSP_RAM.l,d5
                cmp.l   #$DEADBEEF,d5
                bne.s   .bad1

                move.l  #$CAFEBABE,DSP_RAM+$100.l
                move.l  DSP_RAM+$100.l,d5
                cmp.l   #$CAFEBABE,d5
                bne.s   .bad2

                move.l  #$11223344,DSP_RAM+$FFC.l
                move.l  DSP_RAM+$FFC.l,d5
                cmp.l   #$11223344,d5
                bne.s   .bad3

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#$DEADBEEF
.bad2:          ACID_FAIL #2,d5,#$CAFEBABE
.bad3:          ACID_FAIL #3,d5,#$11223344
