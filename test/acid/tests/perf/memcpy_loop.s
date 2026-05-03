;
; tests/perf/memcpy_loop.s - 68K memcpy throughput baseline.
;
; Copies a fixed N longs from SRC to DST via 68K instructions only
; (no blitter).  Strict spot-check (tightened from "first long
; matches"): verify DST[0], DST[N/2], and DST[N-1] all match the
; expected `$AAAA0000 + index` pattern.  This catches off-by-one
; bugs in the copy loop, premature termination, and any cycle-
; timing pathology that might silently truncate the copy.
;
; Detail codes:
;   1 = DST[0] mismatch
;   2 = DST[N/2] mismatch
;   3 = DST[N-1] mismatch
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
N_LONGS         equ     1024                    ; 4 KB
PATTERN_BASE    equ     $AAAA0000

EXPECT_FIRST    equ     PATTERN_BASE + 0
EXPECT_MID      equ     PATTERN_BASE + (N_LONGS/2)
EXPECT_LAST     equ     PATTERN_BASE + (N_LONGS-1)
OFF_MID         equ     (N_LONGS/2) * 4
OFF_LAST        equ     (N_LONGS-1) * 4

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC with PATTERN_BASE + index pattern.
                lea     SRC.l,a0
                move.l  #N_LONGS-1,d2
                move.l  #PATTERN_BASE,d3
.fill:          move.l  d3,(a0)+
                addq.l  #1,d3
                dbra    d2,.fill

                ;; memcpy SRC -> DST.
                lea     SRC.l,a0
                lea     DST.l,a1
                move.l  #N_LONGS-1,d2
.copy:          move.l  (a0)+,(a1)+
                dbra    d2,.copy

                ;; Spot-check: first, middle, last.
                move.l  DST.l,d5
                cmp.l   #EXPECT_FIRST,d5
                bne.s   .bad1

                move.l  DST+OFF_MID.l,d5
                cmp.l   #EXPECT_MID,d5
                bne.s   .bad2

                move.l  DST+OFF_LAST.l,d5
                cmp.l   #EXPECT_LAST,d5
                bne.s   .bad3

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#EXPECT_FIRST
.bad2:          ACID_FAIL #2,d5,#EXPECT_MID
.bad3:          ACID_FAIL #3,d5,#EXPECT_LAST
