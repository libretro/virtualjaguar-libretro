;
; tests/dsp/dsp_basic_run.s - DSP starts and runs.
;
; Mirror of gpu_basic_run.s but for the DSP.  DSP shares the GPU RISC
; ISA; opcode 57 ($E400) is NOP for both.
;
; Strict assertion: D_PC must equal DSP_RAM + 2*N where N is the
; number of DSP instructions executed; require N in [N_MIN, N_MAX]
; so D_PC stays inside our NOP slab.
;
; Same MMIO-dispatch quirk as gpu_basic_run: long-aligned reads in
; the DSP control range may be intercepted as DSP register reads
; before the control-RAM dispatch, returning a register value
; rather than the actual D_PC.
;
; Detail codes:
;   1 = D_PC offset is not a multiple of 2 (instruction fetch broken)
;   2 = D_PC < DSP_RAM + 2*N_MIN (DSP under-ran or never started)
;   3 = D_PC > DSP_RAM + 2*N_MAX (DSP walked off the NOP slab)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

D_FLAGS         equ     DSP_BASE + $00
D_PC            equ     DSP_BASE + $10
D_CTRL          equ     DSP_BASE + $14          ; bit 0 = GO

GO              equ     $00000001
NOP_OP          equ     $E400

NOP_SLOTS       equ     1024
N_MIN           equ     1
N_MAX           equ     NOP_SLOTS
PC_MIN          equ     DSP_RAM + (N_MIN*2)
PC_MAX          equ     DSP_RAM + (N_MAX*2)

                org     $802000
entry:
                ACID_INIT

                lea     DSP_RAM.l,a0
                move.l  #NOP_SLOTS-1,d0
.fill:          move.w  #NOP_OP,(a0)+
                dbra    d0,.fill

                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL

                move.l  #500,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,D_CTRL
                move.l  D_PC,d5

                move.l  d5,d4
                sub.l   #DSP_RAM,d4
                btst    #0,d4
                bne.s   .notaligned
                cmp.l   #(N_MIN*2),d4
                blt.s   .underran
                cmp.l   #(N_MAX*2),d4
                bgt.s   .overran

                ACID_PASS

.notaligned:    ACID_FAIL #1,d5,#0
.underran:      ACID_FAIL #2,d5,#PC_MIN
.overran:       ACID_FAIL #3,d5,#PC_MAX
