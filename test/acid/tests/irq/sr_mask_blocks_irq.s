;
; tests/irq/sr_mask_blocks_irq.s - 68K SR I=7 must block all IRQs.
;
; Enable VBlank in TOM but leave the 68K SR with IPL=7 (mask all).
; Even though TOM raises IRQs (PERF counter timing_vblank_irqs ticks),
; the 68K must NOT take them.
;
; Companion to irq_mask_suppresses (TOM mask) -- this exercises the
; 68K side of the IRQ gate.
;
; Detail codes:
;   1 = handler fired despite SR I=7
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_INT1        equ     $F000E0
TOM_VI          equ     $F0004E
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

                ;; Configure TOM to fire VBlank.
                move.w  #$1F00,TOM_INT1         ; clear pending
                move.w  #2,TOM_VI               ; fire on halfline 2
                move.w  #$0100,TOM_INT1         ; enable VIDEO

                ;; Keep 68K SR with IPL=7 (block everything).
                move.w  #$2700,sr

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
