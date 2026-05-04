;
; tests/timing/pit_countdown_rate.s - JERRY PIT timer 1 must fire
; at the rate determined by its prescaler/divider, within +/- 10%.
;
; Per src/jerry/jerry.c:
;     usecs = (prescaler+1) * (divider+1) * RISC_CYCLE_IN_USEC
; with RISC_CYCLE_IN_USEC = 1 / 26.590906 MHz ~= 0.0376 us/cycle.
;
; We arm with prescaler=10, divider=100:
;     usecs = 11 * 101 * 0.0376 = ~41.78 us per IRQ
;     rate  = 1e6 / 41.78 = ~23936 Hz
;
; Run a calibrated 68K busy-loop window (~1 second wall clock at
; 13.295 MHz NTSC, same loop sizing as vblank_60hz_exact.s -- the
; `subq.l #1,Dn / bne.s` taken pair = 18 cycles, so 739_130 iters
; ~= 13.3 M cycles ~= 1.001 sec) and count IRQs.
; Expect ~23936 +/- 10%.
;
; Detail codes:
;   1 = IRQ count outside [21542, 26330] (+/-10%)
;       observed = counter, expected = 23936
;   2 = counter zero -- IRQ never delivered (wiring regression)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; JERRY register addresses (PIT writable setup; readback aliases at
;; JERRY_BASE+$36/$38 are read-only and don't actually arm the timer).
JPIT1           equ     JERRY_BASE+$00          ; timer 1 prescaler (W)
JPIT2           equ     JERRY_BASE+$02          ; timer 1 divider   (W)
JINTCTRL        equ     JERRY_BASE+$20          ; JERRY interrupt enable

;; IRQ flag stash (below the vector table user-area, above the vector
;; table itself).
IRQ_COUNT       equ     $00000800

;; All hardware IRQs land at vector 64 ($100) per irq_ack_handler.
HW_IRQ_VECTOR   equ     $00000100

;; Busy loop sized to ~1 second wall (matches vblank_60hz_exact).
BUSY_ITERS      equ     739130

;; Expected IRQ count for prescaler=10, divider=100 at the RISC-rate
;; PIT clock (~23936 Hz).  Tolerance widened to +/-10% from +/-5%
;; because at this rate the IRQ handler overhead (~140 cycles per
;; IRQ * ~24000 IRQs ~= 0.13 sec) materially extends the wall
;; window beyond the 1.001 sec the busy loop alone would take.
;; The vblank test fires only 60 IRQs in the same window so its
;; tighter +/-2 tolerance still works.
EXPECT_IRQS     equ     23936
LO_IRQS         equ     21542                   ; -10%
HI_IRQS         equ     26330                   ; +10%

PIT_PRESCALER   equ     10
PIT_DIVIDER     equ     100

                org     $802000
entry:
                ACID_INIT

                ;; Clear counter.
                moveq   #0,d0
                move.l  d0,IRQ_COUNT.l

                ;; Install handler at vector 64.
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear pending TOM IRQs.
                move.w  #$1F00,TOM_INT1

                ;; Enable IRQ_DSP in TOM (JERRY routes through this).
                ;; Low byte = enable mask; IRQ_DSP_MASK = $10.
                move.w  #IRQ_DSP_MASK,TOM_INT1

                ;; Arm JERRY PIT1 via WRITABLE setup regs (NOT the
                ;; readback aliases at $F10036/$F10038).
                move.w  #PIT_PRESCALER,JPIT1
                move.w  #PIT_DIVIDER,JPIT2

                ;; Enable IRQ2_TIMER1 in JERRY.
                move.w  #IRQ2_TIMER1,JINTCTRL

                ;; Allow IPL=2 in 68K SR.
                move.w  #$2000,sr

                ;; Busy-loop for ~1 second wall clock.
                move.l  #BUSY_ITERS,d2
.busy:          subq.l  #1,d2
                bne.s   .busy

                ;; Mask interrupts so the read is stable.
                move.w  #$2700,sr

                move.l  IRQ_COUNT.l,d5

                tst.l   d5
                beq     .never

                cmp.l   #LO_IRQS,d5
                blt     .out_of_range
                cmp.l   #HI_IRQS,d5
                bgt     .out_of_range

                ACID_PASS

.out_of_range:
                ACID_FAIL #1,d5,#EXPECT_IRQS

.never:
                ACID_FAIL #2,d5,#EXPECT_IRQS

irq_handler:
                addq.l  #1,IRQ_COUNT.l
                ;; Re-clear DSP/JERRY pending so the next PIT can fire.
                move.w  #$1000,TOM_INT1         ; clear IRQ_DSP pending
                move.w  #IRQ_DSP_MASK,TOM_INT1  ; re-enable
                ;; Re-arm JERRY IRQ2_TIMER1 (JINTCTRL low byte = enables).
                move.w  #IRQ2_TIMER1,JINTCTRL
                rte
