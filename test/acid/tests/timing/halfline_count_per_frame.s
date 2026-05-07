;
; tests/timing/halfline_count_per_frame.s - per-frame halfline count
; should match NTSC spec.
;
; Polls VC across two frame boundaries and confirms the difference is
; in the expected range (525 +- a few for slop).  If our HalflineCallback
; runs too often per frame the count will exceed; too rarely and it
; will fall short.
;
; Active suspect for the Doom 1.5-2x speed regression (issue #131).
;
; Detail codes:
;   1 = halfline count out of range; observed = max VC seen, expected =
;       VP+1 (target frame length)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

VC              equ     $F00006
VP              equ     $F0003E

EXPECT_VP       equ     524                     ; NTSC: VC sweeps 0..524
TOLERANCE       equ     8
SPIN_LIMIT      equ     5000000

                org     $802000
entry:
                ACID_INIT

                ;; VC includes the lower-field bit (#11 = $0800) which
                ;; toggles each frame; mask with $7FF to get the actual
                ;; halfline count.

                ;; Wait until VC wraps to a low value (frame start).
                move.w  VC,d1
                and.w   #$7FF,d1
                move.l  #SPIN_LIMIT,d4
.find_start:    move.w  VC,d2
                and.w   #$7FF,d2
                cmp.w   d1,d2
                bge.s   .keep
                moveq   #0,d6                   ; d6 = max VC seen
                bra.s   .measure
.keep:          move.w  d2,d1
                subq.l  #1,d4
                bne.s   .find_start
                ACID_FAIL #1,#0,#EXPECT_VP

.measure:       ;; Track the maximum VC we observe before the next wrap.
                move.l  #SPIN_LIMIT,d4
                move.w  VC,d1
                and.w   #$7FF,d1
.loop:          move.w  VC,d2
                and.w   #$7FF,d2
                ;; If VC went DOWN, we wrapped -> done.
                cmp.w   d1,d2
                blt.s   .done
                ;; Track max.
                cmp.w   d6,d2
                ble.s   .nomax
                move.w  d2,d6
.nomax:         move.w  d2,d1
                subq.l  #1,d4
                bne.s   .loop

                ACID_FAIL #1,d6,#EXPECT_VP

.done:          ;; d6 = highest VC seen this frame (already masked).
                move.w  d6,d3
                cmp.w   #EXPECT_VP-TOLERANCE,d3
                blt.s   .out_of_range
                cmp.w   #EXPECT_VP+TOLERANCE,d3
                bgt.s   .out_of_range

                ACID_PASS

.out_of_range:  and.l   #$FFFF,d6
                ACID_FAIL #1,d6,#EXPECT_VP
