;
; tests/perf/gpu_loop_stub.s - 68K loop perf baseline (variant A).
;
; Runs 10000 iterations of a tight `addq + dbra` loop.  No real
; computation; the per-test perf-counter delta tells us how many
; halflines elapsed during the fixed work, which is a proxy for the
; raw speed of our 68K interpreter.
;
; Always PASSES.  Compare halfline_callbacks delta against
; dsp_loop_stub.s -- they should be similar (both 10000-iter 68K
; loops).  A widening gap or a sudden jump on either suggests the
; 68K interpreter regressed.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

ITERS           equ     10000

                org     $802000
entry:
                ACID_INIT

                move.l  #ITERS-1,d2
                moveq   #0,d3
.loop:
                addq.l  #1,d3
                dbra    d2,.loop

                ACID_PASS
