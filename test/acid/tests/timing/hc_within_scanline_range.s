;
; tests/timing/hc_within_scanline_range.s - HC value must be bounded.
;
; Sample HC ($F00004) and confirm the value sits in [0, 1000].  HP is
; typically ~424 on NTSC and our deterministic stub returns 0 or HP/2,
; so any reading above 1000 indicates either a runaway counter or a
; stale rand()-style stub returning 16-bit garbage.
;
; Detail codes:
;   1 = observed HC out of expected [0, 1000] range
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

HC              equ     $F00004
HC_MAX          equ     1000

                org     $802000
entry:
                ACID_INIT

                move.w  HC,d5
                and.l   #$FFFF,d5
                cmp.l   #HC_MAX,d5
                bgt.s   .too_big

                ACID_PASS

.too_big:       ACID_FAIL #1,d5,#HC_MAX
