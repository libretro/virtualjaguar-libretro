;
; tests/timing/hc_advance.s - HC counter must change within a scanline.
;
; The Horizontal Count register at $F00004 advances within each
; halfline; reads at different times during one scanline should show
; different values.
;
; This is one of the registers that was a rand() stub before commit
; 1ca2fdc.  Verify it now returns a varying-but-bounded value.
;
; Detail codes:
;   1 = HC never changed across the spin (timing dead, or HC is a
;       constant)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

HC              equ     $F00004
LOOP_ITERS      equ     50000

                org     $802000
entry:
                ACID_INIT

                move.w  HC,d1                   ; d1 = initial sample
                move.l  #LOOP_ITERS,d2

.spin:          move.w  HC,d3
                cmp.w   d1,d3
                bne.s   .changed
                subq.l  #1,d2
                bne.s   .spin

                ACID_FAIL #1,d3,d1

.changed:       ACID_PASS
