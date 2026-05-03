;
; tests/irq/irq_mask_suppresses.s - masked IRQ must not fire.
;
; With TOM_INT mask=0 (all sources disabled), VBlank should NOT
; reach the 68K even though the underlying TOM event still happens.
; If the counter still ticks, our mask logic is broken.
;
; Companion to vblank_delivery.s which checks the unmasked path.
;
; Detail codes:
;   1 = IRQ fired despite mask=0
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_INT1        equ     $F000E0
IRQ_FIRED       equ     $00000800
HW_IRQ_VECTOR   equ     $00000100
SPIN_LIMIT      equ     2000000

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d0
                move.l  d0,IRQ_FIRED.l
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear any pending then disable ALL sources.
                move.w  #$1F00,TOM_INT1         ; CLR_ALL
                move.w  #$0000,TOM_INT1         ; mask=0

                ;; Allow IPL=2 in 68K SR (so if IRQ DID slip through,
                ;; we'd see it).
                move.w  #$2000,sr

                move.l  #SPIN_LIMIT,d2
.wait:          tst.l   IRQ_FIRED.l
                bne.s   .leak
                subq.l  #1,d2
                bne.s   .wait

                ACID_PASS                       ; never fired -> good

.leak:          ACID_FAIL #1,IRQ_FIRED.l,#0

irq_handler:
                addq.l  #1,IRQ_FIRED.l
                rte
