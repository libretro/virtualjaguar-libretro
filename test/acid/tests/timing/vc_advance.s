;
; tests/timing/vc_advance.s - the VC counter must advance.
;
; Reads TOM VC ($F00006) over a busy-wait loop and confirms it
; changes value at least once.  This is the simplest possible test
; that timing events are firing at all -- if VC never changes, the
; HalflineCallback isn't being scheduled and nothing else timing-
; sensitive can possibly work.
;
; Detail codes on FAIL:
;   1 = VC never changed during the busy-wait (timing dead)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

VC              equ     $F00006
LOOP_ITERS      equ     100000          ; ~0.5 ms of work on real Jag

                org     $802000
entry:
                ACID_INIT

                ;; Snapshot VC.
                move.w  VC,d1           ; d1 = initial VC
                move.l  #LOOP_ITERS,d2

.spin:          move.w  VC,d3           ; d3 = current VC
                cmp.w   d1,d3
                bne.s   .changed        ; VC moved -- timing alive
                subq.l  #1,d2
                bne.s   .spin

                ;; Spun out without ever seeing VC change.
                ACID_FAIL #1,d3,d1

.changed:
                ACID_PASS
