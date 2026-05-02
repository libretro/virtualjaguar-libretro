;
; tests/perf/memcpy_loop.s - 68K memcpy throughput baseline.
;
; Copies a fixed N bytes from SRC to DST via 68K instructions only
; (no blitter).  Test always passes; useful as a perf-counter
; baseline -- the per-test perf summary will show how many halflines
; elapsed for a known amount of work.
;
; If a future change makes 68K instruction timing slower (e.g. extra
; cycles per memory access), this test's halfline_callbacks delta
; will jump.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

SRC             equ     $00080000
DST             equ     $00090000
N_LONGS         equ     1024                    ; 4 KB

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC with a recognizable pattern.
                lea     SRC.l,a0
                move.l  #N_LONGS-1,d2
                move.l  #$AAAA0000,d3
.fill:          move.l  d3,(a0)+
                addq.l  #1,d3
                dbra    d2,.fill

                ;; memcpy SRC -> DST.
                lea     SRC.l,a0
                lea     DST.l,a1
                move.l  #N_LONGS-1,d2
.copy:          move.l  (a0)+,(a1)+
                dbra    d2,.copy

                ;; Spot-check: first long matches.
                move.l  DST.l,d5
                cmp.l   #$AAAA0000,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#$AAAA0000
