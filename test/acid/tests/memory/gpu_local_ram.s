;
; tests/memory/gpu_local_ram.s - GPU local RAM RW round-trip.
;
; Writes a 32-bit pattern at the start, middle, and end of the GPU
; local RAM window ($F03000..$F03FFF), reads back, verifies.  GPU
; local RAM is a separate physical store from main RAM and goes
; through its own dispatch path, so it gets its own test.
;
; Detail codes (which slot tripped):
;   1 = $F03000 readback wrong
;   2 = $F03100 readback wrong
;   3 = $F03FFC readback wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

GPU_RAM_LO      equ     $F03000
GPU_RAM_MID     equ     $F03100
GPU_RAM_HI      equ     $F03FFC

PAT_LO          equ     $12345678
PAT_MID         equ     $A5A5A5A5
PAT_HI          equ     $DEADBEEF

                org     $802000
entry:
                ACID_INIT

                move.l  #PAT_LO,GPU_RAM_LO.l
                move.l  #PAT_MID,GPU_RAM_MID.l
                move.l  #PAT_HI,GPU_RAM_HI.l

                move.l  GPU_RAM_LO.l,d5
                cmp.l   #PAT_LO,d5
                bne     .bad_lo
                move.l  GPU_RAM_MID.l,d5
                cmp.l   #PAT_MID,d5
                bne     .bad_mid
                move.l  GPU_RAM_HI.l,d5
                cmp.l   #PAT_HI,d5
                bne     .bad_hi

                ACID_PASS

.bad_lo:        ACID_FAIL #1,d5,#PAT_LO
.bad_mid:       ACID_FAIL #2,d5,#PAT_MID
.bad_hi:        ACID_FAIL #3,d5,#PAT_HI
