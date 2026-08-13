;
; tests/timing/doom_update_gate_rate.s - a display-gated game loop must
; not run faster than one iteration per 3 video fields.
;
; This models Jaguar Doom's actual frame gate.  I_Update() (jagonly.c)
; ends every display with:
;
;       do { junk = ticcount; } while (junk-lastticcount < 3);
;       lasttics     = ticcount - lastticcount;
;       lastticcount = ticcount;
;
; and `ticcount` is bumped once per VI in the Frame: handler (init.s,
; `addq.l #1,_ticcount`), with VI programmed to a_vde|1 -- once per
; field.  MiniLoop (d_main.c) then calls the game/menu ticker exactly
; once per completed display, so the whole title's pace -- monster
; speed, firing cadence, menu auto-repeat -- is bounded by this gate at
; VI_rate / 3.  At the NTSC field rate that ceiling is 20 iterations
; per second, and Doom cannot legally exceed it.
;
; Why a ROM and not a measurement on the commercial cart: the game-side
; number is only reachable by scripting a real ROM into gameplay and
; reading its private counters, which is neither deterministic nor
; reviewable.  This reproduces the identical hardware path -- VI ->
; counter -> 3-tick gate -- in a few dozen instructions with a fixed
; window, so a regression names itself.
;
; The window is counted in VC wraps (video fields), the same reference
; vblank_60hz_exact.s uses, so the assertion is immune to 68K speed and
; to the dram_timing model: over WINDOW_FIELDS fields a correct machine
; completes exactly WINDOW_FIELDS/3 gate passes.  Running fast here means
; VI is delivering more often than once per field, which would let every
; display-gated title run quick.
;
; Must pass with virtualjaguar_dram_timing both enabled and disabled.
;
; Detail codes:
;   1 = gate passes outside tolerance; observed = passes, expected = 20.
;       High = VI firing more than once per field (titles run fast);
;       low  = VI dropping fields (titles run slow / stutter).
;   2 = zero gate passes -- VI never delivered, IRQ wiring regression
;       rather than a timing error.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; Scratch below ACID_BASE, clear of the vector table.
TICCOUNT        equ     $00000800               ; our stand-in for _ticcount

;; irq_ack_handler() returns vector 64 ($100) for ALL hardware IRQs.
HW_IRQ_VECTOR   equ     $00000100

VC_LINE_MASK    equ     $07FF                   ; VC bit 11 is the field flag

WINDOW_FIELDS   equ     60                      ; one NTSC second
GATE_TICKS      equ     3                       ; Doom's `< 3` wait
EXPECT_PASSES   equ     20                      ; 60 fields / 3 ticks
TOLERANCE       equ     1                       ; boundary fuzz at either end

                org     $802000
entry:
                ACID_INIT

                ;; Clear our ticcount.
                moveq   #0,d0
                move.l  d0,TICCOUNT.l

                ;; Install the VI handler at vector 64.
                lea     irq_handler(pc),a0
                move.l  a0,HW_IRQ_VECTOR.l

                ;; Clear pending TOM IRQs (high byte = clear bits).
                move.w  #$1F00,TOM_INT1

                ;; Fire VI once per field, near the top of the frame.
                move.w  #2,TOM_VI

                ;; Enable IRQ_VIDEO (low byte = enable mask).
                move.w  #IRQ_VIDEO_MASK,TOM_INT1

                ;; Allow IPL=2 in 68K SR (supervisor, mask=0).
                move.w  #$2000,sr

                ;; d3 = fields seen, d4 = gate passes, d5 = lastticcount
                moveq   #0,d3
                moveq   #0,d4
                moveq   #0,d5

                move.w  TOM_VC,d1
                and.w   #VC_LINE_MASK,d1

.spin:
                ;; --- Doom's gate: has ticcount advanced 3 since the
                ;; --- last display?  If so, complete a "display".
                move.l  TICCOUNT.l,d0
                sub.l   d5,d0
                cmp.l   #GATE_TICKS,d0
                blt.s   .no_gate
                move.l  TICCOUNT.l,d5           ; lastticcount = ticcount
                addq.l  #1,d4                   ; one display completed
.no_gate:

                ;; --- Window bookkeeping: count VC wraps (fields).
                move.w  TOM_VC,d0
                and.w   #VC_LINE_MASK,d0
                cmp.w   d1,d0
                bcc.s   .nowrap                 ; d0 >= d1: no wrap yet
                addq.l  #1,d3
.nowrap:        move.w  d0,d1
                cmp.l   #WINDOW_FIELDS,d3
                blt.s   .spin

                ;; Mask interrupts so the read is stable.
                move.w  #$2700,sr

                tst.l   d4
                beq     .never

                cmp.l   #EXPECT_PASSES-TOLERANCE,d4
                blt     .out_of_range
                cmp.l   #EXPECT_PASSES+TOLERANCE,d4
                bgt     .out_of_range

                ACID_PASS

.out_of_range:
                ACID_FAIL #1,d4,#EXPECT_PASSES

.never:
                ACID_FAIL #2,d4,#EXPECT_PASSES

irq_handler:
                addq.l  #1,TICCOUNT.l
                ;; Re-clear video pending bit so the next field can fire.
                move.w  #$0100,TOM_INT1         ; clear IRQ_VIDEO pending
                move.w  #IRQ_VIDEO_MASK,TOM_INT1 ; re-enable
                rte
