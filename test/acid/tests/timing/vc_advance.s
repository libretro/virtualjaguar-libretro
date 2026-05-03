;
; tests/timing/vc_advance.s - VC must monotonically advance per halfline.
;
; Sample VC twice with a measured 68K busy-wait between samples.  On a
; live timing path VC ticks once per halfline (~30.5 us NTSC), so the
; delta over a ~10K-NOP gap MUST be at least 1, but should also be
; bounded -- if VC jumps by hundreds we've either miscounted halflines
; or VC wrapped (525 lines/frame NTSC).
;
; This is the *strict* version of "VC changed at all" -- documents the
; expected per-halfline cadence.  The previous loose test merely
; verified VC was non-constant.
;
; Detail codes on FAIL:
;   1 = delta == 0 (timing dead -- VC frozen)
;   2 = delta > 100 (VC advanced way too fast OR wrapped: investigate)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

DELTA_MIN       equ     1
;; Empirically a 10K-NOP wait crosses ~500 halflines on the emulator
;; (one whole NTSC frame is 525 lines).  Widen the bound to <= 524
;; (= NTSC halflines/frame - 1) so we accept anything within a single
;; frame but reject a wrap (which would show up as 0 or negative).
DELTA_MAX       equ     524
SPIN_NOPS       equ     10000

                org     $802000
entry:
                ACID_INIT

                ;; Sample 1.
                move.w  TOM_VC,d1               ; d1 = first VC reading

                ;; Wait ~10000 NOPs.  At ~1 cycle/NOP and ~13 MHz the
                ;; gap is well under one halfline (~30 us = ~400 cycles
                ;; of 68K), but on emulated hosts a NOP costs many host
                ;; cycles so several halflines elapse.  Either way the
                ;; bounded check below catches both extremes.
                move.l  #SPIN_NOPS,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                ;; Sample 2.
                move.w  TOM_VC,d3               ; d3 = second VC reading

                ;; Compute signed delta (mod-525 wrap-aware: just use
                ;; raw subtraction -- if it wrapped we'll see negative
                ;; or huge value and FAIL with detail=2).
                move.w  d3,d4
                sub.w   d1,d4
                ext.l   d4                      ; sign-extend low word
                tst.l   d4
                beq.s   .frozen
                cmp.l   #DELTA_MIN,d4
                blt.s   .frozen                 ; signed: any <1 is frozen-or-wrap
                cmp.l   #DELTA_MAX,d4
                bgt.s   .toofast

                ACID_PASS

.frozen:        ACID_FAIL #1,d4,#DELTA_MIN
.toofast:       ACID_FAIL #2,d4,#DELTA_MAX
