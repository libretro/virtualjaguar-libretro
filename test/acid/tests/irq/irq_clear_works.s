;
; tests/irq/irq_clear_works.s - explicit IRQ clear should remove
; pending state.
;
; Without enabling delivery, raise the conditions for an IRQ (poll
; until a vblank cycle), then write the CLEAR bit to TOM_INT1 and
; verify the pending bit is gone.
;
; Detail codes:
;   1 = IRQ pending bit still set after CLEAR
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_INT1        equ     $F000E0
SPIN_LIMIT      equ     2000000

                org     $802000
entry:
                ACID_INIT

                ;; Mask all IRQs (IRQ pending stays internal but
                ;; doesn't reach 68K).
                move.w  #$1F00,TOM_INT1         ; clear all
                move.w  #$0000,TOM_INT1         ; mask=0

                ;; Spin a bit so any pending video event accrues.
                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Now write clear-all and verify mask bits readback as
                ;; we left them (low byte of TOM_INT1 is read as
                ;; pending status).
                move.w  #$1F00,TOM_INT1
                move.w  TOM_INT1,d5

                ;; Lower byte should be zero (no pending after clear);
                ;; upper byte we just set to $1F (clear-all).  Spec
                ;; varies on what the readback shows, but the LOW byte
                ;; (pending) is the part that matters.
                and.w   #$001F,d5
                tst.w   d5
                bne.s   .still_pending

                ACID_PASS

.still_pending: and.l   #$FFFF,d5
                ACID_FAIL #1,d5,#0
