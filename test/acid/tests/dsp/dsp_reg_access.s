;
; tests/dsp/dsp_reg_access.s - 68K can write DSP work RAM and read it back.
;
; Same shape as gpu/gpu_reg_access but for DSP at $F1B000..$F1CFFF
; (8 KB; src/jerry/dsp.c:296 -- dsp_ram_8[0x2000]).  The "high" probe
; must land at $F1B000+$1FFC so we exercise the upper half; a probe
; at $F1B000+$FFC would only cover the first 4 KB.
;
; Detail codes:
;   1 = $F1B000 readback wrong
;   2 = $F1B100 readback wrong
;   3 = $F1CFFC readback wrong  (last addressable long)
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

                move.l  #$11223344,DSP_RAM+$1FFC.l
                move.l  DSP_RAM+$1FFC.l,d5
                cmp.l   #$11223344,d5
                bne.s   .bad3

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#$DEADBEEF
.bad2:          ACID_FAIL #2,d5,#$CAFEBABE
.bad3:          ACID_FAIL #3,d5,#$11223344
