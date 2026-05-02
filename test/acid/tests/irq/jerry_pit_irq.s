;
; tests/irq/jerry_pit_irq.s - JERRY PIT timer 1 must reach 68K.
;
; Configures JERRY PIT timer 1 with a small divider so it fires
; quickly, enables the IRQ in TOM (because JERRY IRQs route through
; TOM IRQ_DSP), enables IRQ2_TIMER1 in JERRY, and waits for the
; handler to bump a counter.
;
; This is the path that timing_jerry_irqs PERF counter watches.
; Test passes if the PERF counter ticks AND the 68K handler fires.
;
; Detail codes:
;   1 = handler never fired within spin budget
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;; TOM
TOM_INT1        equ     $F000E0

;; JERRY
JPIT1           equ     $F10036                 ; timer 1 prescaler
JPIT2           equ     $F10038                 ; timer 1 divider
JINTCTRL        equ     $F10020                 ; interrupt control

;; Bits
JINT_TIMER1     equ     $0002
TOM_INT_DSP_EN  equ     $0400                   ; bit 10 enables DSP/JERRY IRQ

IRQ_FIRED       equ     $00000800
HW_IRQ_VECTOR   equ     $00000100
SPIN_LIMIT      equ     5000000

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d0
                move.l  d0,IRQ_FIRED.l
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear any pending TOM IRQs.
                move.w  #$1F00,TOM_INT1
                ;; Enable IRQ_DSP (JERRY routes through this).
                move.w  #TOM_INT_DSP_EN,TOM_INT1

                ;; Configure JERRY PIT1 with small divider for fast fire.
                move.w  #$0001,JPIT1            ; prescaler 1
                move.w  #$0010,JPIT2            ; divider 16

                ;; Enable timer 1 IRQ in JERRY.
                move.w  #JINT_TIMER1,JINTCTRL

                ;; Allow IPL=2 in 68K SR.
                move.w  #$2000,sr

                move.l  #SPIN_LIMIT,d2
.wait:          tst.l   IRQ_FIRED.l
                bne.s   .got_irq
                subq.l  #1,d2
                bne.s   .wait

                ACID_FAIL #1,IRQ_FIRED.l,#1

.got_irq:       ACID_PASS

irq_handler:
                addq.l  #1,IRQ_FIRED.l
                rte
