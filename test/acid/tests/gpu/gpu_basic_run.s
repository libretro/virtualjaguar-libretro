;
; tests/gpu/gpu_basic_run.s - GPU starts and runs.
;
; Loads 16 NOP opcodes (each $E400, opcode 57) into GPU work RAM at
; $F03000, sets G_PC to the start, asserts GO in G_CTRL, and after a
; brief spin reads G_PC back -- it must have advanced.
;
; If G_PC stayed equal to the initial value, the GPU never ran.
;
; Detail codes:
;   1 = G_PC didn't advance after starting GPU
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;; GPU control regs at $F02100..$F02120
G_FLAGS         equ     $F02100
G_MTXC          equ     $F02104
G_PC            equ     $F02110
G_CTRL          equ     $F02114                 ; bit 0 = GO/RUN

;; GPU work RAM
GPU_RAM         equ     $F03000

GO              equ     $00000001
NOP_OP          equ     $E400                   ; opcode 57 << 10

                org     $802000
entry:
                ACID_INIT

                ;; Fill GPU RAM with NOPs (32 bytes = 16 instructions).
                lea     GPU_RAM.l,a0
                moveq   #15,d0
.fill:          move.w  #NOP_OP,(a0)
                addq.l  #2,a0
                dbra    d0,.fill

                ;; Set G_FLAGS=0 (clear flags), G_PC=$F03000, then GO.
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; Burn ~100k 68K instructions so the GPU gets cycles.
                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                ;; Stop GPU and read back PC.
                move.l  #0,G_CTRL
                move.l  G_PC,d5

                ;; G_PC should have advanced past GPU_RAM.
                cmp.l   #GPU_RAM,d5
                bls.s   .stuck

                ACID_PASS

.stuck:         ACID_FAIL #1,d5,#GPU_RAM
