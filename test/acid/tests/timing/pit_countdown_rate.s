;
; tests/timing/pit_countdown_rate.s - JERRY PIT timer 1 must fire
; at the rate determined by its prescaler/divider, within +/- 5%.
;
; REGRESSION GUARD: this test catches the recurring bug of putting PIT
; at the half (M68K) clock rate.  Per JTRM (docs/jtrm-clocks-timing.md)
; the PIT counter decrements at the FULL system clock (~26.59 MHz NTSC).
; Half-rate implementations have historically broken Doom and Rayman
; music timing.  If you "fix" something by halving these constants you
; will break this test.  Don't.
;
; Per src/jerry/jerry.c:
;     usecs = (prescaler+1) * (divider+1) * RISC_CYCLE_IN_USEC
; with RISC_CYCLE_IN_USEC = 1 / 26.590906 MHz ~= 0.0376 us/cycle.
;
; We arm with prescaler=255, divider=255:
;     period = 256 * 256 / 26.590906e6 = ~2464.6 us per IRQ
;     rate   = 1e6 / 2464.6 = ~405.8 Hz
;
; Run a calibrated 68K busy-loop window (~1 second wall clock at
; 13.295 MHz NTSC, same loop sizing as vblank_60hz_exact.s -- the
; `subq.l #1,Dn / bne.s` taken pair = 18 cycles, so 739_130 iters
; ~= 13.3 M cycles ~= 1.001 sec) and count IRQs.
;
; Expected ~406 IRQs/sec.  Note that handler overhead at this rate is
; non-negligible (each IRQ steals ~12 us = ~0.5% of the window per
; firing), so the effective observed count drops slightly below the
; theoretical 406.  We use +/-5% tolerance which absorbs that.
;
; Detail codes:
;   1 = IRQ count outside [385, 426] (+/-5%)
;       observed = counter, expected = ~406
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

;; Expected IRQ count for prescaler=255, divider=255 at the FULL system
;; clock PIT rate (RISC, ~26.59 MHz NTSC).  Handler overhead bites a
;; little here, so widen the window to +/-5%.
EXPECT_IRQS     equ     406
LO_IRQS         equ     385                     ; -5%
HI_IRQS         equ     426                     ; +5%

PIT_PRESCALER   equ     255
PIT_DIVIDER     equ     255

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
