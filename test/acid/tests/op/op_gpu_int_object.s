;
; tests/op/op_gpu_int_object.s - OP GPU INTERRUPT object (type 2).
;
; The GPU-INT object causes the OP to assert IRQ3 on the GPU and stop
; processing the list (so the GPU sees the object in OB before it
; gets overwritten).  We verify by reading the GPU's IRQ-pending
; latch directly from the 68K side.
;
; The IRQ latches live in `gpu_control` at GPU_BASE + $14 (NOT
; gpu_flags at +$00 -- that register holds Z/N/C condition codes).
; Per src/tom/gpu.c::GPUSetIRQLine, asserting IRQ line N sets bit
; (0x0040 << N) in gpu_control.  The OP's GPU-INT path calls
; GPUSetIRQLine(3, ASSERT_LINE) (src/tom/op.c:463), so the bit we
; expect to see latched is 0x0040 << 3 = 0x00000200 (bit 9).
;
; Strategy: build OP list with a GPU-INT object, run OP for many
; halflines, then read gpu_control and check if bit 9 is set.
;
; GPU-INT object encoding (type 2, single 64-bit phrase):
;   p0 bits 0..2 = TYPE = 2
;   The OP also stores `p0` into TOM's OB register (currentobject)
;   so the GPU IRQ handler can read what triggered it.
;
; Detail codes:
;   1 = GPU IRQ3 latch never asserted (gpu_control bit 9 stayed 0)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
GPU_INT_OBJ     equ     OPLIST + 0
STOP_OBJ        equ     OPLIST + 8
SPIN_LIMIT      equ     500000

G_CTRL          equ     GPU_BASE + $14          ; GPU control / IRQ latches

                org     $802000
entry:
                ACID_INIT

                ;; ---- GPU_INT object (type 2) ----
                ;; Just need TYPE = 2 in low 3 bits.  Stash a recognisable
                ;; value in the upper bits so we can also see OB if we want.
                move.l  #$0BADF00D,GPU_INT_OBJ
                move.l  #$00000002,GPU_INT_OBJ+4

                ;; STOP after (the OP stops on its own at type 2, but for
                ;; sanity put a STOP next so any fall-through still bails).
                move.l  #$00000000,STOP_OBJ
                move.l  #$00000004,STOP_OBJ+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Spin so OP gets to process the list at least once
                ;; per halfline for many halflines.
                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Read gpu_control (GPU_BASE+$14).  Per
                ;; src/tom/op.c:463 the GPU-INT object calls
                ;; GPUSetIRQLine(3, ASSERT_LINE), and per
                ;; src/tom/gpu.c::GPUSetIRQLine that sets bit
                ;; (0x0040 << 3) = $00000200 (bit 9) in gpu_control.
                ;; That latch is unconditional -- no enable bit gates
                ;; the latch itself; the enable mask only gates whether
                ;; the GPU CPU vectors to its ISR.  So reading bit 9
                ;; from 68K side is a reliable observation.
                move.l  G_CTRL.l,d5
                move.l  d5,d6
                and.l   #$00000200,d6
                bne     .saw_irq

                ;; IRQ3 latch never set -- OP did not fire GPU-INT, or
                ;; the OP->GPU IRQ wiring is broken.
                ACID_FAIL #1,d5,#$00000200

.saw_irq:       ACID_PASS
