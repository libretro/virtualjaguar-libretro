;
; tests/dsp/dsp_irq_to_68k.s - DSP triggers JERRY DSP IRQ to the 68K.
;
; Sequence:
;   1. 68K enables JERRY IRQ2_DSP mask via J_INT ($F10020 low byte = $02).
;   2. 68K loads a tiny DSP program that writes CPUINT (=$0002) to its
;      own D_CTRL.  That asks JERRY to fire IRQ2_DSP.
;   3. 68K starts DSP, waits, stops DSP.
;   4. 68K reads J_INT.  The JERRY pending-IRQ register should now show
;      IRQ2_DSP=$0002 set.
;   5. 68K also installs an autovector-2 IRQ handler that writes a
;      marker; if 68K IRQs are unmasked the handler runs and the marker
;      is set in addition to the pending-bit check.
;
; PASS = IRQ2_DSP bit set in the pending register AND the IRQ marker
; was written by the handler.
;
; The IRQ marker check confirms the IRQ was actually delivered to the
; 68K (the pending-bit check alone only confirms JERRY queued it).
;
; Detail codes:
;   1 = J_INT pending didn't include IRQ2_DSP (DSP didn't trigger or
;       JERRY didn't latch it)
;   2 = IRQ marker not written (IRQ wasn't delivered to 68K)
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

IRQ_MARKER_ADDR equ     $00080010
IRQ_MARKER_VAL  equ     $C0FFEE01

VECTOR_AUTOIRQ2 equ     $00000068       ; autovector level 2 = vector 26 = address 0x68

                org     $802000
entry:
                ;; Run in supervisor with IPL=0 so level-2 IRQs unmask.
                move.w  #$2000,sr

                ACID_INIT

                ;; Init markers.
                move.l  #$00000000,IRQ_MARKER_ADDR.l

                ;; Install autovector-2 IRQ handler.
                lea     irq2_handler(pc),a1
                move.l  a1,VECTOR_AUTOIRQ2.l

                ;; Enable JERRY DSP IRQ mask (clear any pending too).
                move.w  #$FF02,J_INT.l   ; low byte mask=$02 (IRQ2_DSP);
                                         ; high byte $FF clears any
                                         ; stale pending bits.

                ;; Build DSP program: write CPUINT to D_CTRL via store.
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

                ;; Start DSP.
                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL

                ;; Spin so DSP gets cycles + 68K can take the IRQ.
                move.l  #200000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,D_CTRL

                ;; Check J_INT pending byte for IRQ2_DSP.
                ;; (Reading $F10020 returns jerryPendingInterrupt.)
                move.w  J_INT.l,d5
                move.w  d5,d4
                and.w   #$0002,d4        ; mask to IRQ2_DSP bit
                tst.w   d4
                beq.s   .no_pending

                ;; Check IRQ handler ran.
                move.l  IRQ_MARKER_ADDR.l,d6
                cmp.l   #IRQ_MARKER_VAL,d6
                bne.s   .no_handler

                ACID_PASS

.no_pending:    ACID_FAIL #1,d5,#$0002
.no_handler:    ACID_FAIL #2,d6,#IRQ_MARKER_VAL

;; -----------------------------------------------------------------
;; IRQ2 handler: write marker, ack JERRY DSP pending bit, RTE.
irq2_handler:
                move.l  #IRQ_MARKER_VAL,IRQ_MARKER_ADDR.l
                ;; Ack JERRY DSP IRQ: write high byte = $02 to clear pending,
                ;; keep mask = $02.
                move.w  #$0202,J_INT.l
                rte
