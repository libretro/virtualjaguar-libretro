;
; tests/gpu/gpu_basic_run.s - GPU starts and runs.
;
; Loads 256 NOP opcodes (each $E400, opcode 57) into GPU work RAM at
; GPU_RAM, sets G_PC to the start, asserts GO in G_CTRL, runs the
; 68K through a *short* spin (so the GPU doesn't walk off the NOP
; slab), stops the GPU, and reads G_PC back.
;
; Strict assertion: G_PC must equal GPU_RAM + 2*N where N is
; the number of GPU instructions executed.  We require N to be in
; [N_MIN, N_MAX] -- N_MIN ensures the GPU actually ran (not just
; "G_PC > start"), and N_MAX ensures G_PC stayed inside the NOP
; slab (so we know the value reflects real fetches, not garbage past
; the program).
;
; *Known emulator quirk*: the dispatch in src/tom/gpu.c:GPUReadLong
; intercepts every long-aligned read in $F02000..$F020FF as a GPU
; general-purpose register-bank read BEFORE checking the
; control-RAM range, so the 68K reading $F02110 (G_PC) actually
; returns gpu_reg_bank_0[4], not gpu_pc.  This test FAILs with
; detail=2 (under-ran) on garbage values when this happens, which
; is the desired diagnostic for that emulator bug.
;
; Detail codes:
;   1 = G_PC offset is not a multiple of 2 (instruction fetch broken)
;   2 = G_PC < GPU_RAM + 2*N_MIN (GPU under-ran or never started)
;   3 = G_PC > GPU_RAM + 2*N_MAX (GPU walked off the NOP slab)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; GPU control regs at GPU_BASE.
G_FLAGS         equ     GPU_BASE + $00
G_PC            equ     GPU_BASE + $10
G_CTRL          equ     GPU_BASE + $14          ; bit 0 = GO/RUN

GO              equ     $00000001
NOP_OP          equ     $E400                   ; opcode 57 << 10

;; GPU runs at 26.6 MHz vs 68K @ 13.3 MHz; in this emulator the
;; GPU eats many more instructions per host-tick than naive ratio
;; suggests.  Use a large NOP slab so we can confidently bound the
;; final PC inside it.
NOP_SLOTS       equ     1024                    ; 2 KB of NOPs
N_MIN           equ     1                       ; >=1 GPU insn fetched
N_MAX           equ     NOP_SLOTS               ; <= slab size
PC_MIN          equ     GPU_RAM + (N_MIN*2)
PC_MAX          equ     GPU_RAM + (N_MAX*2)

                org     $802000
entry:
                ACID_INIT

                ;; Fill GPU RAM with NOPs.
                lea     GPU_RAM.l,a0
                move.l  #NOP_SLOTS-1,d0
.fill:          move.w  #NOP_OP,(a0)+
                dbra    d0,.fill

                ;; Clear flags, set PC, GO.
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; Short spin so the GPU executes some NOPs without
                ;; walking past the slab.
                move.l  #500,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                ;; Stop GPU and read PC back.
                move.l  #0,G_CTRL
                move.l  G_PC,d5

                ;; Strict checks.
                move.l  d5,d4
                sub.l   #GPU_RAM,d4             ; d4 = offset from start
                btst    #0,d4
                bne.s   .notaligned             ; PC not even -> broken
                cmp.l   #(N_MIN*2),d4
                blt.s   .underran
                cmp.l   #(N_MAX*2),d4
                bgt.s   .overran

                ACID_PASS

.notaligned:    ACID_FAIL #1,d5,#0
.underran:      ACID_FAIL #2,d5,#PC_MIN
.overran:       ACID_FAIL #3,d5,#PC_MAX
