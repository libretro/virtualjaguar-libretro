;
; tests/timing/vc_increments.s - VC must monotonically advance (modulo wrap).
;
; Reads VC, burns ~50000 NOPs of busy work, reads VC again.  After
; masking with $7FF, the second sample must either be > the first
; (still in the same frame) OR < the first (we wrapped past the end
; of a frame).  Equality means VC is dead -- no halfline events have
; fired across the entire spin window, which is much longer than one
; halfline.
;
; Detail codes:
;   1 = VC was identical across the spin (timing dead)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

VC              equ     $F00006
SPIN_NOPS       equ     50000

                org     $802000
entry:
                ACID_INIT

                move.w  VC,d1
                and.l   #$7FF,d1

                move.l  #SPIN_NOPS,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.w  VC,d3
                and.l   #$7FF,d3

                cmp.l   d1,d3
                beq.s   .stuck

                ACID_PASS

.stuck:         ACID_FAIL #1,d3,d1
