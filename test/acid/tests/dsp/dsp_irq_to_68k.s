;
; tests/dsp/dsp_irq_to_68k.s - DSP triggers JERRY DSP IRQ to the 68K.
;
; Sequence:
;   1. 68K installs a hardware IRQ handler at vector 64 ($100).
;      (Jaguar TOM/JERRY return user vector 64 on IACK -- they don't
;      assert VPA -- so even a level-2 IRQ goes to $100, not the
;      autovector slot at $68.)
;   2. 68K enables JERRY IRQ2_DSP mask via J_INT ($F10020 low byte = $02).
;   3. 68K loads a tiny DSP program that writes CPUINT (=$0002) to its
;      own D_CTRL.  That asks JERRY to fire IRQ2_DSP.
;   4. 68K runs in supervisor with IPL=0 so level-2 IRQs unmask.
;   5. 68K starts DSP, spins, stops DSP.
;   6. The IRQ handler runs during the spin, writes a known marker.
;
; PASS = IRQ marker written by the handler.
;
; (We don't separately check the J_INT pending bit because the
; handler acks it via the high-byte clear.  Reading pending after
; the handler runs would always return 0, which would make a
; pending-bit assertion internally contradictory with the handler
; the test installs.  Marker presence proves the full chain:
; DSP CPUINT write -> JERRY pending latch -> m68k_set_irq(2) ->
; CPU IACK -> vector 64 fetch -> handler entry.)
;
; Detail codes:
;   1 = IRQ marker not written (IRQ wasn't delivered to 68K --
;       chain broken at DSP CPUINT, JERRY latch, m68k_set_irq, or
;       vector dispatch).
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

;; Jaguar HW does NOT assert VPA on IACK -- TOM/JERRY return user
;; vector 64 ($100) for all hardware IRQs (see irq_ack_handler in
;; src/core/jaguar.c).  So even though the 68K SR uses level-2
;; (autovector) interrupts, the actual exception vector is 64, not
;; the 68K-architectural autovector slot at $68.  This matches the
;; convention used by the passing tests/irq/vblank_delivery.s.
HW_IRQ_VECTOR   equ     $00000100

                org     $802000
entry:
                ;; Run in supervisor with IPL=0 so level-2 IRQs unmask.
                move.w  #$2000,sr

                ACID_INIT

                ;; Init markers.
                move.l  #$00000000,IRQ_MARKER_ADDR.l

                ;; Install hardware IRQ handler at vector 64 ($100).
                ;; (The CPU takes a level-2 autovector interrupt, but
                ;; Jaguar HW returns user vector 64 on IACK rather than
                ;; asserting VPA -- so the handler lives at $100, not
                ;; the 68K-architectural autovector slot at $68.)
                lea     irq2_handler(pc),a1
                move.l  a1,HW_IRQ_VECTOR.l

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

                ;; Check IRQ handler ran.  The handler acks the JERRY
                ;; pending bit via the high-byte clear, so reading
                ;; jerryPendingInterrupt here would always be 0 --
                ;; marker presence is the load-bearing evidence.
                move.l  IRQ_MARKER_ADDR.l,d6
                cmp.l   #IRQ_MARKER_VAL,d6
                bne.s   .no_handler

                ACID_PASS

.no_handler:    ACID_FAIL #1,d6,#IRQ_MARKER_VAL

;; -----------------------------------------------------------------
;; IRQ2 handler: write marker, ack JERRY DSP pending bit, RTE.
irq2_handler:
                move.l  #IRQ_MARKER_VAL,IRQ_MARKER_ADDR.l
                ;; Ack JERRY DSP IRQ: write high byte = $02 to clear pending,
                ;; keep mask = $02.
                move.w  #$0202,J_INT.l
                rte
