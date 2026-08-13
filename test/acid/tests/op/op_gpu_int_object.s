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
; Per JTRM Technical Reference rev 8, G_CTRL bits 6-10 are INT_LAT0-4;
; asserting IRQ line N sets bit (0x0040 << N).  The OP's GPU-INT path
; calls GPUSetIRQLine(3, ASSERT_LINE), so the bit we expect to see
; latched is 0x0040 << 3 = 0x00000200 (bit 9).
;
; IMPORTANT -- the GPU must be RUNNING for this to be observable.
; A halted GPU (G_CTRL GPUGO=0) drops the assert instead of latching
; it; see src/tom/gpu.c:GPUSetIRQLine and the halted-GPU counterpart
; test, tests/op/op_gpu_int_object_halted.s, which pins that
; behaviour.  This test used to start no GPU at all and assert the
; latch anyway -- it had been FAILing since e92b675 (the Hover Strike
; B-skip fix) landed.
;
; So we park the GPU in a two-instruction self-loop first.  Every
; interrupt enable in G_FLAGS is left clear on purpose: GPUHandleIRQs
; needs an enable to *dispatch*, but the latch itself is set
; regardless, so this observes the latch without running an ISR (and
; without the OP burning its inline OBF-release budget, which is
; gated on GPUOPInterruptEnabled()).
;
; Strategy: park GPU in self-loop, build OP list with a GPU-INT
; object, run OP for many halflines, then read OB and gpu_control.
;
; GPU-INT object encoding (type 2, single 64-bit phrase):
;   p0 bits 0..2 = TYPE = 2
;   The OP also stores `p0` into TOM's OB register (currentobject)
;   so the GPU IRQ handler can read what triggered it.
;
; Detail codes:
;   1 = GPU IRQ3 latch never asserted (gpu_control bit 9 stayed 0)
;   2 = OP never reached the object at all (OB does not hold our
;       marker) -- the list/OLP wiring is broken, not the IRQ path,
;       so detail=1 would have been misleading
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
GPU_INT_OBJ     equ     OPLIST + 0
STOP_OBJ        equ     OPLIST + 8
SPIN_LIMIT      equ     500000

G_FLAGS         equ     GPU_BASE + $00
G_PC            equ     GPU_BASE + $10
G_CTRL          equ     GPU_BASE + $14          ; GPU control / IRQ latches
GO              equ     $00000001

;; OB (current object) latch, TOM_BASE + $10..$17.  The OP writes the
;; whole first phrase here before raising IRQ3.  OB exposes it as four
;; 16-bit registers, least significant word at the LOWEST address, each
;; register big-endian internally (jag_sim netlists/tom/OB.NET:55-67,
;; IODEC.NET:85-88):
;;
;;   OB0 $F00010 = phrase[15:0]     OB2 $F00014 = phrase[47:32]
;;   OB1 $F00012 = phrase[31:16]    OB3 $F00016 = phrase[63:48]
;;
;; Our p0 is $0BADF00D00000002, so a long read at TOM_OB+4 returns
;; OB2:OB3 = $F00D:$0BAD.  The marker stays asymmetric on purpose --
;; it discriminates the word order rather than hiding it.
TOM_OB          equ     TOM_BASE + $10

OBJ_MARKER      equ     $0BADF00D
OBJ_MARKER_OB   equ     $F00D0BAD
IRQ3_LATCH      equ     $00000200

                org     $802000
entry:
                ACID_INIT

                ;; ---- Park the GPU in a self-loop so it is RUNNING ----
                ;; `jr T,-1` ($D7E0) + delay-slot `nop` ($E400): a
                ;; two-instruction infinite loop.  Same idiom as
                ;; tests/gpu/gpu_op_jump.s.  Using a loop rather than a
                ;; NOP slab matters -- a slab lets the GPU walk off into
                ;; the randomised tail of gpu_ram_8, where stray bytes
                ;; decode as jumps and could clear GPUGO mid-test.
                lea     GPU_RAM.l,a0
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                ;; Interrupt enables stay clear: latch yes, dispatch no.
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; ---- GPU_INT object (type 2) ----
                ;; Just need TYPE = 2 in low 3 bits.  The upper bits hold
                ;; a recognisable marker so we can confirm via OB that
                ;; the OP really reached this object.
                move.l  #OBJ_MARKER,GPU_INT_OBJ
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

                ;; Did the OP reach the object at all?  OPSetCurrentObject
                ;; runs before GPUSetIRQLine and is not gated on GPU state,
                ;; so this separates "IRQ path broken" from "OP never got
                ;; there".
                move.l  TOM_OB+4.l,d4
                cmp.l   #OBJ_MARKER_OB,d4
                bne     .no_object

                ;; Read gpu_control (GPU_BASE+$14) and check the IRQ3 latch.
                move.l  G_CTRL.l,d5
                move.l  d5,d6
                and.l   #IRQ3_LATCH,d6
                bne     .saw_irq

                ;; IRQ3 latch never set -- OP did not fire GPU-INT, or
                ;; the OP->GPU IRQ wiring is broken.
                ACID_FAIL #1,d5,#IRQ3_LATCH

.no_object:     ACID_FAIL #2,d4,#OBJ_MARKER_OB

.saw_irq:       ACID_PASS
