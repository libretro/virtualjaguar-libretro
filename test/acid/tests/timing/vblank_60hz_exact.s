;
; tests/timing/vblank_60hz_exact.s - count VBlank IRQs in a fixed
; ~1-second 68K busy-loop window.  NTSC must deliver 60 +/- 1.
;
; Strict version of the existing loose vblank_delivery test:
;   * Installs a vector-64 handler that bumps a counter.
;   * Configures TOM VI to fire once per frame (VI = 1 halfline).
;   * Enables IRQ_VIDEO via TOM_INT1 low byte.
;   * Drops 68K SR mask to allow IPL=2.
;   * Runs a busy loop sized to ~1 wall-clock second.
;     The 68K runs at 13.295453 MHz NTSC (M68K_CLOCK_RATE_NTSC).
;     A `subq.l #1,Dn / bne.s` (taken) pair takes 8 + 10 = 18 cycles
;     on the UAE 68K timing model.  So 1 second / 18 cycles
;     ~= 738_636 iterations.  We use 739_130 to land on a
;     ~1.001 sec wall-clock window.
;
; Detail codes:
;   1 = VBlank counter outside [58, 62] -- emulator timing drift.
;       observed = counter value, expected = 60.
;   2 = counter is zero -- IRQ never delivered (regression in IRQ
;       wiring, not a timing issue).
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; Where we stash the IRQ counter (out of the vector table area,
;; below ACID_BASE).
IRQ_COUNT       equ     $00000800

;; irq_ack_handler() returns vector 64 ($100) for ALL hardware IRQs.
HW_IRQ_VECTOR   equ     $00000100

;; Busy-loop iterations sized to ~1 second on a real (or accurate)
;; NTSC 68K @ 13.295 MHz.  Inner loop is `subq.l #1,Dn / bne.s`
;; (taken) = 8 + 10 = 18 cycles -- 739_130 iters ~= 13.3 M cycles
;; ~= 1 sec wall.
BUSY_ITERS      equ     739130

EXPECT_VBLANK   equ     60
TOLERANCE       equ     2                       ; +/- accept

                org     $802000
entry:
                ACID_INIT

                ;; Clear the counter.
                moveq   #0,d0
                move.l  d0,IRQ_COUNT.l

                ;; Install handler at vector 64.
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear pending TOM IRQs (high byte = clear bits).
                move.w  #$1F00,TOM_INT1

                ;; Fire VI at halfline 2 (very top of frame).
                move.w  #2,TOM_VI

                ;; Enable IRQ_VIDEO (low byte = enable mask).
                move.w  #IRQ_VIDEO_MASK,TOM_INT1

                ;; Allow IPL=2 in 68K SR (supervisor, mask=0).
                move.w  #$2000,sr

                ;; Busy-loop for ~1 second wall clock.
                move.l  #BUSY_ITERS,d2
.busy:          subq.l  #1,d2
                bne.s   .busy

                ;; Mask interrupts again so the read is stable.
                move.w  #$2700,sr

                ;; Read the count.
                move.l  IRQ_COUNT.l,d5

                tst.l   d5
                beq     .never

                ;; Expect 58..62 (60 +/- 2 for boundary fuzz).
                cmp.l   #EXPECT_VBLANK-TOLERANCE,d5
                blt     .out_of_range
                cmp.l   #EXPECT_VBLANK+TOLERANCE,d5
                bgt     .out_of_range

                ACID_PASS

.out_of_range:
                ACID_FAIL #1,d5,#EXPECT_VBLANK

.never:
                ACID_FAIL #2,d5,#EXPECT_VBLANK

irq_handler:
                addq.l  #1,IRQ_COUNT.l
                ;; Re-clear video pending bit so the next vblank can fire.
                move.w  #$0100,TOM_INT1         ; clear IRQ_VIDEO pending
                move.w  #IRQ_VIDEO_MASK,TOM_INT1 ; re-enable
                rte
