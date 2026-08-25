;
; tests/dsp/dsp_irq_tom_gate.s - DSP CPUINT is gated by TOM C_JERENA,
; and the withheld request is delivered when C_JERENA is later set.
;
; JERRY has no wire of its own to the 68K.  Every JERRY interrupt
; source -- DSP (J_DSPENA), the two PITs, the UART, the external
; input -- merges onto a single DINT line that enters TOM as INT1
; bit 4 (C_JERENA, mask $0010).  With C_JERENA clear, a DSP CPUINT
; must raise NOTHING on the 68K, no matter what JINTCTRL says.
; (docs/jtrm-jerry.md "JERRY-to-TOM Interrupt Routing"; the INT1 bit
; layout is docs/jtrm-register-map.md "TOM Interrupt Registers".)
;
; The latch is a separate thing from the gate: JERRY still records the
; pending bit while C_JERENA is clear, so a later write that sets
; C_JERENA hands the still-asserted DINT to the 68K.  Withheld, not
; lost.  This test pins both halves, because a "fix" that drops the
; interrupt entirely passes half of it.
;
; Sequence:
;   1. 68K installs the hardware IRQ handler at vector 64 ($100).
;   2. TOM INT1 = $0000: every TOM source disabled, C_JERENA clear.
;      (Video too, so the only thing that can reach the handler in
;      phase 1 is the DSP.)
;   3. JERRY JINTCTRL mask = $02 (IRQ2_DSP), pending cleared.
;   4. 68K runs at IPL 0, so a leaked level-2 WOULD be taken.
;   5. DSP writes CPUINT to its own D_CTRL, then spins.
;   6. Phase 1 assert: the marker must still be zero.
;   7. 68K writes TOM INT1 = $0010 (C_JERENA on).
;   8. Phase 2 assert: the marker must now appear -- the pending
;      JERRY request was withheld, not discarded.
;
; Detail codes:
;   1 = IRQ leaked to the 68K with C_JERENA clear (missing gate)
;   2 = enabling C_JERENA did not deliver the withheld IRQ
;       (gate applied to the latch instead of to delivery)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

D_FLAGS         equ     DSP_BASE+$00
D_PC            equ     DSP_BASE+$10
D_CTRL          equ     DSP_BASE+$14

CPUINT          equ     $00000002
GO              equ     $00000001

J_INT           equ     $00F10020
TOM_INT1_W      equ     $00F000E0

;; TOM INT1 low byte, bit 4 -- see IRQ_DSP in include/jaguar_regs.s.
TOM_INT_DSP_EN  equ     $0010

IRQ_MARKER_ADDR equ     $00080010
IRQ_MARKER_VAL  equ     $C0FFEE02

;; Jaguar HW does not assert VPA on IACK -- TOM/JERRY return user
;; vector 64 ($100) for all hardware IRQs (see irq_ack_handler in
;; src/core/jaguar.c), so the handler lives at $100 and not at the
;; 68K-architectural level-2 autovector slot at $68.
HW_IRQ_VECTOR   equ     $00000100

PHASE1_SPIN     equ     200000
PHASE2_SPIN     equ     200000

                org     $802000
entry:
                ;; Supervisor, IPL=0: a leaked level-2 would be taken.
                move.w  #$2000,sr

                ACID_INIT

                move.l  #$00000000,IRQ_MARKER_ADDR.l

                lea     irq2_handler(pc),a1
                move.l  a1,HW_IRQ_VECTOR.l

                ;; Clear every TOM pending latch, then disable every
                ;; TOM source -- C_JERENA included.
                move.w  #$1F00,TOM_INT1_W
                move.w  #$0000,TOM_INT1_W

                ;; JERRY: clear stale pending, enable IRQ2_DSP only.
                move.w  #$FF02,J_INT.l

                ;; Build the DSP program: store CPUINT into D_CTRL,
                ;; then spin.
                lea     DSP_RAM.l,a0
                ;; movei #CPUINT, r0
                move.w  #$9800,(a0)+
                move.w  #(CPUINT&$FFFF),(a0)+
                move.w  #((CPUINT>>16)&$FFFF),(a0)+
                ;; movei #D_CTRL, r1
                move.w  #$9801,(a0)+
                move.w  #(D_CTRL&$FFFF),(a0)+
                move.w  #((D_CTRL>>16)&$FFFF),(a0)+
                ;; store r0,(r1)   (RN=r0=value, RM=r1=addr) -> $BC20
                move.w  #$BC20,(a0)+
                ;; jr T,-1 / nop spin
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                ;; Start the DSP.
                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL

                ;; Phase 1: give the DSP cycles and the 68K plenty of
                ;; instruction boundaries at which a leaked request
                ;; would be dispatched.
                move.l  #PHASE1_SPIN,d2
.spin1:         nop
                subq.l  #1,d2
                bne.s   .spin1

                move.l  #0,D_CTRL

                move.l  IRQ_MARKER_ADDR.l,d6
                tst.l   d6
                bne.s   .leaked

                ;; Phase 2: open C_JERENA.  JERRY is still driving
                ;; DINT (its pending bit was latched and never acked),
                ;; so TOM must hand the request over now.
                move.w  #TOM_INT_DSP_EN,TOM_INT1_W

                move.l  #PHASE2_SPIN,d2
.spin2:         move.l  IRQ_MARKER_ADDR.l,d6
                cmp.l   #IRQ_MARKER_VAL,d6
                beq.s   .delivered
                subq.l  #1,d2
                bne.s   .spin2

                ACID_FAIL #2,d6,#IRQ_MARKER_VAL

.delivered:     ACID_PASS

.leaked:        ACID_FAIL #1,d6,#0

;; -----------------------------------------------------------------
;; IRQ2 handler: mark, ack JERRY's pending DSP bit (high byte $02),
;; ack TOM's INT1 JERRY pending bit while keeping C_JERENA enabled.
irq2_handler:
                move.l  #IRQ_MARKER_VAL,IRQ_MARKER_ADDR.l
                move.w  #$0202,J_INT.l
                move.w  #$1010,TOM_INT1_W
                rte
