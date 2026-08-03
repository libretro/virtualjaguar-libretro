;
; tests/timing/vblank_60hz_exact.s - exactly one VBlank IRQ per video
; field.  Over a 60-field window NTSC must deliver 60 +/- 1.
;
; SCOPE CHANGED -- read this before treating a pass as a rate check.
; This used to time a ~1-second window with a calibrated 68K busy loop
; (739_130 iterations of a `subq.l/bne.s` pair billed at the datasheet
; 18 cycles) and assert 60 VBlanks in it, i.e. an absolute 60 Hz check.
; That window was never trustworthy: these ROMs execute from cart ROM at
; $802000, which at the reset MEMCON1 ($1861, ROMSPEED=0) costs 10 system
; clocks per fetch, so an iteration really costs ~20 cycles, not 18.
; Under the virtualjaguar_dram_timing model the loop ran ~8% long and
; this test failed at 65 VBlanks -- with nothing wrong with video timing.
;
; The window is now 60 VC wraps, which makes this a check that VI fires
; once and only once per field: no double-fires, no dropped frames. That
; is a real invariant and it is immune to 68K speed, but it is NOT the
; absolute-rate check the old test claimed to be -- the video clock is
; now both the thing measured and the reference. Coverage genuinely
; narrowed here. The surviving absolute anchor is pit_countdown_rate.s,
; which measures JERRY's PIT against this same video clock; those two
; hardware dividers are independent, so together they still pin the
; video rate.
;
; Structure:
;   * Installs a vector-64 handler that bumps a counter.
;   * Configures TOM VI to fire once per frame (VI = 1 halfline).
;   * Enables IRQ_VIDEO via TOM_INT1 low byte.
;   * Drops 68K SR mask to allow IPL=2.
;   * Counts VC wraps until the window closes.
;
; Must pass with virtualjaguar_dram_timing both enabled and disabled.
;
; Detail codes:
;   1 = VBlank counter outside [59, 61] -- emulator timing drift.
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

;; Window measured in video fields (VC wraps), not 68K instructions.
WINDOW_FIELDS   equ     60
;; VC's bit 11 is the field flag (src/tom/tom.c) -- mask it off, or every
;; field flip reads as a wrap.
VC_LINE_MASK    equ     $07FF

EXPECT_VBLANK   equ     60
;; One VI per field, so the count should equal the window exactly; allow
;; +/-1 for landing mid-field at either end.
TOLERANCE       equ     1                       ; +/- accept

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

                ;; Wait out WINDOW_FIELDS video fields by watching VC wrap.
                moveq   #0,d3                   ; fields seen
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

                ;; Mask interrupts again so the read is stable.
                move.w  #$2700,sr

                ;; Read the count.
                move.l  IRQ_COUNT.l,d5

                tst.l   d5
                beq     .never

                ;; Expect 59..61 (60 +/- TOLERANCE for boundary fuzz).
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
