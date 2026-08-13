;
; tests/op/op_gpu_int_object_halted.s - a HALTED GPU must not capture
; the OP's GPU-INT interrupt.
;
; Counterpart to tests/op/op_gpu_int_object.s, which covers the same
; object with the GPU running.  Here GPUGO stays clear, so the OP's
; GPUSetIRQLine(3, ASSERT_LINE) must be dropped rather than latched:
; G_CTRL bit 9 (INT_LAT3) has to still read as 0 afterwards.
;
; Why this is asserted rather than the opposite:
;
;   The JTRM (Technical Reference rev 8, "GPU Control/Status Register")
;   documents bits 6-10 as INT_LAT0-4 and says the latches are cleared
;   by writing the INT_CLR bits, but it never states whether a latch
;   accumulates while GPUGO is clear.  Neither behaviour is spec'd, so
;   this test does not claim to encode the silicon -- it pins the
;   emulator's deliberate choice.
;
;   That choice is e92b675: latching while halted let a stale
;   DSA-response interrupt dispatch at GPU restart, before the new
;   program's r31 stack init, pushing a return address into GPU code
;   and deterministically locking up Hover Strike on a B-skip.  A
;   halted RISC core samples nothing, so the assert is dropped and the
;   source re-edges after restart.
;
;   Rejected alternative: keep the latch while halted but clear all
;   INT_LATs on the GPUGO 0->1 transition.  That would also dodge the
;   stale dispatch, but it invents a second undocumented behaviour and
;   revalidating it means re-running the whole CD boot matrix.
;
; If you intend to change the halted-GPU rule, change it here too --
; deliberately.  Do not "fix" this test to make a behaviour change pass.
;
; Detail codes:
;   1 = IRQ3 latch was set while the GPU was halted (e92b675 regressed)
;   2 = OP never reached the object at all (OB does not hold our
;       marker), so the run proves nothing -- without this the test
;       would pass vacuously if the OP simply never ran
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
GPU_INT_OBJ     equ     OPLIST + 0
STOP_OBJ        equ     OPLIST + 8
SPIN_LIMIT      equ     500000

G_CTRL          equ     GPU_BASE + $14          ; GPU control / IRQ latches

;; OB (current object) latch, TOM_BASE + $10..$17.  OB exposes the
;; latched phrase as four 16-bit registers, least significant word at
;; the LOWEST address, each register big-endian internally (jag_sim
;; netlists/tom/OB.NET:55-67, IODEC.NET:85-88):
;;
;;   OB0 $F00010 = phrase[15:0]     OB2 $F00014 = phrase[47:32]
;;   OB1 $F00012 = phrase[31:16]    OB3 $F00016 = phrase[63:48]
;;
;; Our p0 is $0BADF00D00000002, so a long read at TOM_OB+4 returns
;; OB2:OB3 = $F00D:$0BAD.
TOM_OB          equ     TOM_BASE + $10

OBJ_MARKER      equ     $0BADF00D
OBJ_MARKER_OB   equ     $F00D0BAD
IRQ3_LATCH      equ     $00000200

                org     $802000
entry:
                ACID_INIT

                ;; Make sure GPUGO is clear.  Note the G_CTRL write
                ;; handler preserves bits 6-10, so this cannot cheat by
                ;; wiping a latch that was already set -- per JTRM the
                ;; latches only clear via the INT_CLR bits in G_FLAGS.
                move.l  #0,G_CTRL

                ;; ---- GPU_INT object (type 2) ----
                move.l  #OBJ_MARKER,GPU_INT_OBJ
                move.l  #$00000002,GPU_INT_OBJ+4

                move.l  #$00000000,STOP_OBJ
                move.l  #$00000004,STOP_OBJ+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Let the OP walk the list over many halflines.
                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; The OP must actually have reached the object, or this
                ;; test proves nothing.  OPSetCurrentObject runs before
                ;; GPUSetIRQLine and is NOT gated on GPU run state, so OB
                ;; is a valid witness even though the IRQ was dropped.
                move.l  TOM_OB+4.l,d4
                cmp.l   #OBJ_MARKER_OB,d4
                bne     .no_object

                ;; The latch must NOT have been captured.
                move.l  G_CTRL.l,d5
                move.l  d5,d6
                and.l   #IRQ3_LATCH,d6
                bne     .latched

                ACID_PASS

.latched:       ACID_FAIL #1,d5,#0
.no_object:     ACID_FAIL #2,d4,#OBJ_MARKER_OB
