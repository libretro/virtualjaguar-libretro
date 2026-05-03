;
; tests/dsp/dsp_basic_run.s - DSP starts and runs.
;
; Mirror of gpu_basic_run.s but for the DSP at $F1A100.  DSP uses the
; same RISC ISA as the GPU; opcode 57 ($E400) is NOP for both.
;
; Detail codes:
;   1 = D_PC didn't advance after starting DSP
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

D_FLAGS         equ     $F1A100
D_PC            equ     $F1A110
D_CTRL          equ     $F1A114                 ; bit 0 = GO

DSP_RAM         equ     $F1B000

GO              equ     $00000001
NOP_OP          equ     $E400

                org     $802000
entry:
                ACID_INIT

                ;; Fill DSP RAM with NOPs.
                lea     DSP_RAM.l,a0
                moveq   #15,d0
.fill:          move.w  #NOP_OP,(a0)
                addq.l  #2,a0
                dbra    d0,.fill

                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL

                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,D_CTRL
                move.l  D_PC,d5

                cmp.l   #DSP_RAM,d5
                bls.s   .stuck

                ACID_PASS

.stuck:         ACID_FAIL #1,d5,#DSP_RAM
