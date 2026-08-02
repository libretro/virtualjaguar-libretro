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
; The one-second window is measured by counting 60 VC wraps (video
; fields), NOT by running a calibrated 68K busy loop.  It used to do the
; latter -- 739_130 iterations of a `subq.l/bne.s` pair billed at the
; datasheet 18 cycles -- and that is only a wall clock if you know the
; per-iteration cost.  You don't: these ROMs execute from cart ROM at
; $802000, which at the reset MEMCON1 ($1861, ROMSPEED=0) costs 10 system
; clocks per fetch, so each iteration really costs ~20 cycles.  With the
; virtualjaguar_dram_timing model enabled the loop ran ~8% long and this
; test failed at 437 IRQs; before that model's double-count was fixed it
; ran 49% long and failed at 606.  Neither was a PIT bug.
;
; Counting video fields instead makes this a ratio between two
; independent hardware dividers -- JERRY's PIT off the system clock
; versus TOM's video clock -- so it holds under any CPU timing model.
; It must pass with virtualjaguar_dram_timing both enabled and disabled;
; if it ever passes in only one of those, the window has gone back to
; depending on 68K instruction throughput.
;
; Expected ~406 IRQs/sec.  Note that handler overhead at this rate is
; non-negligible (each IRQ steals ~12 us = ~0.5% of the window per
; firing), so the effective observed count drops slightly below the
; theoretical 406.  We use +/-5% tolerance which absorbs that.
;
; The handler must clear JERRY's own pending latch (JINTCTRL high byte,
; write-1-to-clear) as well as TOM's INT1 copy.  Acking only TOM left
; JERRY driving INT1 bit 4, the 68K re-took the level-2 request at every
; instruction boundary, and the wait loop below never retired a single
; iteration -- the test wedged without ever writing a signature and read
; as NOT-RUN-YET on every build.  See docs/jtrm-jerry.md (JINTCTRL) and
; src/tom/tom.c:TOMPendingMask.
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

;; One-second window measured in video fields (VC wraps), not 68K
;; instructions.  NTSC is 60 fields/sec, so 60 wraps ~= 1.0 s.
WINDOW_FIELDS   equ     60
;; VC's bit 11 is the field flag (src/tom/tom.c) -- mask it off before
;; comparing, or every field flip looks like a wrap.
VC_LINE_MASK    equ     $07FF

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

                ;; ---- One-second window, measured off the VIDEO clock ----
                ;; NOT off a 68K instruction count.  A busy loop is only a
                ;; wall clock if you know its per-iteration cost, and that
                ;; cost depends on memory timing: these ROMs execute from
                ;; cart ROM at $802000, which at the reset MEMCON1 costs 10
                ;; system clocks per fetch, so the `subq.l/bne.s` pair is
                ;; ~20 cycles rather than the datasheet 18.  Counting VC
                ;; wraps instead makes the window depend only on the video
                ;; clock, so this measures PIT rate against video rate --
                ;; two independent hardware dividers -- and is immune to
                ;; however fast the 68K happens to retire instructions.
                moveq   #0,d3                   ; fields (VC wraps) seen
                move.w  TOM_VC,d1
                and.w   #VC_LINE_MASK,d1
.wait:          move.w  TOM_VC,d0
                and.w   #VC_LINE_MASK,d0
                cmp.w   d1,d0
                bcc.s   .nowrap                 ; d0 >= d1: no wrap yet
                addq.l  #1,d3
.nowrap:        move.w  d0,d1
                cmp.l   #WINDOW_FIELDS,d3
                blt.s   .wait

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
                ;; Drop JERRY's pending latch FIRST.  JERRY drives TOM
                ;; INT1 bit 4 for as long as an enabled source has an
                ;; uncleared latch, and the 68K re-samples that level-2
                ;; request at every instruction boundary -- so acking only
                ;; TOM's copy below leaves the line asserted and the CPU
                ;; re-enters this handler forever without retiring a
                ;; single busy-loop instruction.  Writing the enable bit
                ;; alone does NOT clear it: JINTCTRL's low byte is the
                ;; enable mask, its high byte is write-1-to-clear.
                move.w  #IRQ2_TIMER1_CLR|IRQ2_TIMER1,JINTCTRL
                ;; Now ack TOM's side and re-enable.
                move.w  #$1000,TOM_INT1         ; clear IRQ_DSP pending
                move.w  #IRQ_DSP_MASK,TOM_INT1  ; re-enable
                rte
