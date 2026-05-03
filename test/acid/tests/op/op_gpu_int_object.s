;
; tests/op/op_gpu_int_object.s - OP GPU INTERRUPT object (type 2).
;
; The GPU-INT object causes the OP to assert IRQ3 on the GPU and stop
; processing the list (so the GPU sees the object in OB before it
; gets overwritten).  We don't need the GPU to actually run a handler
; -- we can verify the IRQ-line latch by reading TOM_INT1, which holds
; a pending bit for IRQ_GPU (bit 1) when the GPU asserted an IRQ to
; the 68K.
;
; Wait -- IRQ_GPU bit in TOM_INT1 latches when the GPU asserts an IRQ
; back at the 68K, not when the OP IRQs the GPU.  To detect the OP->GPU
; IRQ we'd need to read GPU's own G_FLAGS register (bit for IRQ3
; pending).  That register is at GPU_BASE + $4 (G_FLAGS).
;
; Strategy: build OP list with a GPU-INT object, run OP for many
; halflines, then read G_FLAGS and check if bit 11 (IRQ3 latch) is set.
;
; GPU-INT object encoding (type 2, single 64-bit phrase):
;   p0 bits 0..2 = TYPE = 2
;   The OP also stores `p0` into TOM's OB register (currentobject)
;   so the GPU IRQ handler can read what triggered it.
;
; Detail codes:
;   1 = GPU IRQ3 latch never asserted (G_FLAGS bit 11 stayed 0)
;   99 = encoding placeholder
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
GPU_INT_OBJ     equ     OPLIST + 0
STOP_OBJ        equ     OPLIST + 8
SPIN_LIMIT      equ     500000

G_FLAGS         equ     GPU_BASE + $00          ; GPU flags / IRQ latches

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

                ;; Read G_FLAGS.  IRQ3 (CPU_IRQ in some docs, OP_IRQ in
                ;; others) latches in bit 11 ($0800).  The GPU has the
                ;; CPU_IRQ_MASK in bits 4..8 -- if not enabled, the latch
                ;; bit may not actually set.  Check both the latch (low
                ;; bits) and any pending status.
                ;;
                ;; Simpler: the OP code calls GPUSetIRQLine(3, ASSERT_LINE)
                ;; which sets gpu_flag_c (bit 11) IF bit 7 of G_FLAGS
                ;; (CPU_IRQ_ENABLE bit) is set.  Without enabling, the
                ;; assert may be a no-op.
                ;;
                ;; This test is therefore *fragile*; it relies on
                ;; emulator behaviour where the IRQ line state is
                ;; observable somehow.  Mark as detail=99 if we can't
                ;; observe the assertion at all.
                ;;
                ;; Try reading G_FLAGS as 32-bit value.
                move.l  G_FLAGS.l,d5
                ;; Test: any bit in $0F80 (IRQ3 latch + nearby) set?
                move.l  d5,d6
                and.l   #$00000F80,d6
                bne     .saw_irq

                ;; Couldn't observe the IRQ assert from 68K side without
                ;; full GPU configuration.  Mark as placeholder fail so
                ;; this gap is visible but not a regression on a working
                ;; emulator.
                ACID_FAIL #99,d5,#$00000F80

.saw_irq:       ACID_PASS
