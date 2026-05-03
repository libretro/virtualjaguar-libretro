;
; tests/memory/dsp_local_ram.s - DSP local RAM RW round-trip.
;
; Writes a 32-bit pattern at the start, middle, and end of the DSP
; local RAM window ($F1B000..$F1CFFF -- 8 KB; src/jerry/dsp.c:296
; allocates dsp_ram_8[0x2000] above DSP_WORK_RAM_BASE=$F1B000).  DSP
; local RAM lives behind a separate dispatch path from main RAM, so
; it gets its own RW smoke test; HI must land in the upper half so
; we'd notice if the dispatcher silently truncated to 4 KB.
;
; Detail codes (which slot tripped):
;   1 = $F1B000 readback wrong
;   2 = $F1B800 readback wrong  (mid -- second 4 KB page)
;   3 = $F1CFFC readback wrong  (last addressable long)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

DSP_RAM_LO      equ     $F1B000
DSP_RAM_MID     equ     $F1B800
DSP_RAM_HI      equ     $F1CFFC

PAT_LO          equ     $12345678
PAT_MID         equ     $5A5A5A5A
PAT_HI          equ     $CAFEBABE

                org     $802000
entry:
                ACID_INIT

                move.l  #PAT_LO,DSP_RAM_LO.l
                move.l  #PAT_MID,DSP_RAM_MID.l
                move.l  #PAT_HI,DSP_RAM_HI.l

                move.l  DSP_RAM_LO.l,d5
                cmp.l   #PAT_LO,d5
                bne     .bad_lo
                move.l  DSP_RAM_MID.l,d5
                cmp.l   #PAT_MID,d5
                bne     .bad_mid
                move.l  DSP_RAM_HI.l,d5
                cmp.l   #PAT_HI,d5
                bne     .bad_hi

                ACID_PASS

.bad_lo:        ACID_FAIL #1,d5,#PAT_LO
.bad_mid:       ACID_FAIL #2,d5,#PAT_MID
.bad_hi:        ACID_FAIL #3,d5,#PAT_HI
