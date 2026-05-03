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
; *Sizing notes*: DSP local RAM is 8 KB (src/jerry/dsp.c
; dsp_ram_8[0x2000]) at $F1B000..$F1CFFF.  An earlier version of
; this test filled only the first 1024 NOPs (2 KB) and spun the
; 68K for 500 loop iterations (each ~18 68K cycles, so ~9000 68K
; cycles wall) -- the DSP walked clean off the 2 KB NOP slab into
; the trailing portion of dsp_ram_8 (DSPReset zero-fills this in
; HLE mode -- the acid harness's mode -- and randomizes only when
; vjs.useJaguarBIOS is set).  Zero opcodes decode as `add r0,r0`,
; not NOPs and not branches, so PC keeps advancing linearly until
; it falls off the end of dsp_ram_8 entirely and starts reading
; from the JERRY register window where reads can return arbitrary
; values that occasionally decode as JR / MOVEI-with-jump.
; Observed D_PC for that case: $00F1D1C8, well past DSP_RAM end.
; The fix is to (1) fill the entire 8 KB of DSP local RAM with
; real NOPs ($E400) so an overshoot is caught by N_MAX rather
; than masked by add-r0-r0 walking us off the end, and (2) shrink
; the 68K spin so the DSP is safely inside the slab when we sample
; D_PC.
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

;; Full 8 KB DSP local RAM (4096 NOP slots).
NOP_SLOTS       equ     4096
SPIN_COUNT      equ     50
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

                move.l  #SPIN_COUNT,d2
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
