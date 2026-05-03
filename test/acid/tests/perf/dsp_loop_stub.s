;
; tests/perf/dsp_loop_stub.s - 68K loop perf baseline (variant B).
;
; Same shape as gpu_loop_stub.s (10000-iter `addq + dbra`) but with
; a different initial accumulator value so the two tests are easy
; to tell apart in profiles.  Currently a placeholder -- could be
; wired to actually exercise the DSP later.
;
; Always PASSES.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

ITERS           equ     10000

                org     $802000
entry:
                ACID_INIT

                move.l  #ITERS-1,d2
                move.l  #$DEADBEEF,d3
.loop:
                addq.l  #1,d3
                dbra    d2,.loop

                ACID_PASS
