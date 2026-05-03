;
; tests/timing/vc_resets_at_vp.s - VC must wrap to 0 (or $0800 for
; the lower-field) exactly when its halfline counter == VP, not
; before, not after.
;
; Per src/core/jaguar.c HalflineCallback:
;     vc++
;     if ((vc & 0x7FF) >= VP_reg + 1):
;         lowerField = !lowerField
;         vc = lowerField ? 0x0800 : 0x0000
;
; So as a 68K observer: each time we see VC drop to a value with
; (VC & 0x7FF) == 0, the PREVIOUS sample (also masked) must have
; been EXACTLY equal to (VP_reg & 0x7FF).
;   * If previous masked VC < VP -> wrap fired too early (off-by-one)
;   * If previous masked VC > VP -> wrap fired too late (impossible
;     by the code, but we check for it as a robustness signal: it
;     means the (>=) test was actually (>) somewhere)
;
; We check this across several frame boundaries to catch any
; intermittent off-by-one and to make sure both field-bit values
; hit (lower / upper).
;
; Detail codes:
;   1 = wrap happened too early (prev masked VC < VP)
;       observed = prev masked VC, expected = VP register value
;   2 = wrap happened too late (prev masked VC > VP)
;       observed = prev masked VC, expected = VP register value
;   3 = never observed a wrap within spin budget
;       observed = wraps-seen counter, expected = MIN_WRAPS
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

VC_MASK         equ     $07FF
SPIN_LIMIT      equ     8000000
MIN_WRAPS       equ     3                       ; check across >=3 frames

                org     $802000
entry:
                ACID_INIT

                ;; d7 = VP register value (masked) -- expected pre-wrap VC.
                move.w  TOM_VP,d7
                and.w   #VC_MASK,d7

                ;; d6 = wrap-event counter
                moveq   #0,d6

                ;; d1 = previous masked VC sample
                move.w  TOM_VC,d1
                and.w   #VC_MASK,d1

                move.l  #SPIN_LIMIT,d4

.spin:          move.w  TOM_VC,d3
                and.w   #VC_MASK,d3

                ;; Wrap detected when current masked VC < previous masked VC.
                cmp.w   d1,d3
                bge.s   .no_wrap

                ;; --- wrap event ---
                ;; previous-sample (d1) MUST equal VP (d7).
                cmp.w   d7,d1
                blt     .too_early
                bgt     .too_late

                ;; OK -- bump wrap counter; finished if MIN_WRAPS reached.
                addq.l  #1,d6
                cmp.l   #MIN_WRAPS,d6
                bge     .ok

.no_wrap:       move.w  d3,d1
                subq.l  #1,d4
                bne     .spin

                ;; spun out without enough wraps
                ACID_FAIL #3,d6,#MIN_WRAPS

.too_early:     and.l   #$FFFF,d1
                and.l   #$FFFF,d7
                ACID_FAIL #1,d1,d7

.too_late:      and.l   #$FFFF,d1
                and.l   #$FFFF,d7
                ACID_FAIL #2,d1,d7

.ok:
                ACID_PASS
