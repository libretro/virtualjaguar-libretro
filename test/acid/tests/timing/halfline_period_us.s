;
; tests/timing/halfline_period_us.s - the NTSC scanline period, measured
; against the JERRY PIT rather than against 68K instruction count.
;
; HC alternates between 0 and (0x0400 | HP/2) every halfline (per
; src/tom/tom.c).  A 0 -> non-zero transition therefore marks the start
; of one full scanline (= two halflines), which on NTSC is 63.5 us.
;
; We arm PIT timer 1 at a known period and count scanline starts across
; a fixed number of PIT interrupts.  Both sides of that ratio are
; hardware dividers -- TOM's video clock and JERRY's PIT off the system
; clock -- so the result does not depend on how fast the 68K retires
; instructions.
;
;   PIT period = 256 * 256 / 26.590906e6      = 2464.6 us
;   window     = 16 * 2464.6                  = 39434 us
;   scanlines  = 39434 / 63.5                 = ~621
;
; WHY THIS CHANGED.  The old version counted 68K loop iterations between
; two HC=0 events and multiplied by CYCLES_PER_ITER = 56 -- a constant
; its own comment described as "Tuned to land observed in the [798, 930]
; window".  That makes the assertion a statement about 68K instruction
; cost, not about video timing.  It is not a knowable constant: these
; ROMs execute from cart ROM at $802000, which at the reset MEMCON1
; ($1861, ROMSPEED=0) costs 10 system clocks per fetch.  With the
; virtualjaguar_dram_timing model enabled each iteration got dearer,
; fewer fitted in a scanline, and the estimate fell to 728 against an
; expected 844 -- while the scanline period itself never moved.
;
; Must pass with virtualjaguar_dram_timing both enabled and disabled.
;
; Detail codes:
;   1 = scanline count outside [LO_LINES, HI_LINES]
;       observed = scanlines counted, expected = ~621
;   2 = no PIT interrupt ever arrived (PIT/IRQ wiring regression)
;   3 = HC never transitioned 0 -> non-zero (HC stuck)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

JPIT1           equ     JERRY_BASE+$00          ; timer 1 prescaler (W)
JPIT2           equ     JERRY_BASE+$02          ; timer 1 divider   (W)
JINTCTRL        equ     JERRY_BASE+$20

PIT_COUNT       equ     $00000800
HW_IRQ_VECTOR   equ     $00000100

PIT_PRESCALER   equ     255
PIT_DIVIDER     equ     255

;; PIT interrupts to span.  16 * 2464.6 us = 39.4 ms.
WINDOW_IRQS     equ     16

;; 39434 us / 63.5 us per NTSC scanline.
EXPECT_LINES    equ     621
LO_LINES        equ     590                     ; -5%
HI_LINES        equ     652                     ; +5%

;; Poll budget, so stuck HC fails instead of hanging.
POLL_BUDGET     equ     8000000

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d0
                move.l  d0,PIT_COUNT.l

                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear pending TOM IRQs, then enable IRQ_DSP (JERRY
                ;; reaches the 68K through TOM's bit 4).
                move.w  #$1F00,TOM_INT1
                move.w  #IRQ_DSP_MASK,TOM_INT1

                ;; Arm PIT1 via the WRITABLE setup regs.
                move.w  #PIT_PRESCALER,JPIT1
                move.w  #PIT_DIVIDER,JPIT2
                move.w  #IRQ2_TIMER1,JINTCTRL

                move.w  #$2000,sr               ; allow IPL=2

                ;; Count HC 0 -> non-zero transitions until the PIT has
                ;; ticked WINDOW_IRQS times.
                moveq   #0,d5                   ; scanlines seen
                move.l  #POLL_BUDGET,d6
                move.w  TOM_HC,d1
                and.w   #$0400,d1               ; d1 != 0 while mid-halfline

.poll:          move.w  TOM_HC,d0
                and.w   #$0400,d0
                cmp.w   d1,d0
                beq.s   .no_edge
                tst.w   d0
                beq.s   .store                  ; non-zero -> 0: not our edge
                addq.l  #1,d5                   ; 0 -> non-zero: scanline start
.store:         move.w  d0,d1

.no_edge:       move.l  PIT_COUNT.l,d7
                cmp.l   #WINDOW_IRQS,d7
                bge.s   .done
                subq.l  #1,d6
                bne.s   .poll

                ;; Budget exhausted: say which side stalled.
                move.w  #$2700,sr
                move.l  PIT_COUNT.l,d7
                tst.l   d7
                beq     .no_pit
                ACID_FAIL #3,d5,#EXPECT_LINES

.done:          move.w  #$2700,sr               ; stable read

                cmp.l   #LO_LINES,d5
                blt     .out_of_range
                cmp.l   #HI_LINES,d5
                bgt     .out_of_range

                ACID_PASS

.out_of_range:  ACID_FAIL #1,d5,#EXPECT_LINES
.no_pit:        ACID_FAIL #2,d7,#WINDOW_IRQS

irq_handler:
                addq.l  #1,PIT_COUNT.l
                ;; Clear JERRY's own latch first (high byte is
                ;; write-1-to-clear), then ack TOM's copy -- acking only
                ;; TOM leaves JERRY driving INT1 bit 4 and the 68K
                ;; re-enters this handler at every instruction boundary.
                move.w  #IRQ2_TIMER1_CLR|IRQ2_TIMER1,JINTCTRL
                move.w  #$1000,TOM_INT1
                move.w  #IRQ_DSP_MASK,TOM_INT1
                rte
