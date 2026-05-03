;
; tests/timing/vc_per_frame.s - VC should hit ~525 unique values per frame.
;
; The Jaguar VC counter increments every halfline and resets at end-
; of-frame.  NTSC: 525 halflines/frame, so VC should sweep
; 0..524 once per frame.  This test polls VC continuously for a
; known number of host frames and counts how many times we see VC
; wrap back to 0 (each wrap = one frame).
;
; The runner runs this for 600 host frames by default (10 emulated
; seconds at 60 Hz NTSC).  We'd expect ~600 frames worth of VC
; resets -- this test passes if we observe at least 60 (1 second's
; worth, well below 600 to absorb startup latency and any frame
; the test takes to set up its loop).
;
; Detail codes:
;   1 = saw zero frame transitions in our spin window
;       observed = total VC reads we did
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

VC              equ     $F00006
SPIN_LIMIT      equ     2000000         ; bound the loop in case VC frozen
MIN_TRANSITIONS equ     60              ; 1 sec worth of NTSC frames

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d2           ; d2 = transition count
                move.l  #SPIN_LIMIT,d4  ; safety bound
                moveq   #0,d5           ; d5 = total reads (for diagnostics)
                move.w  VC,d1           ; d1 = previous VC sample

.spin:          addq.l  #1,d5
                move.w  VC,d3           ; d3 = current VC
                ;; We count "VC just decreased" as a frame boundary -- VC
                ;; goes up monotonically within a frame and snaps back
                ;; to a low value at end-of-frame (could be 0 with
                ;; lower-field bit set, etc).
                cmp.w   d1,d3
                bge.s   .no_wrap
                addq.l  #1,d2
                cmp.l   #MIN_TRANSITIONS,d2
                bge.s   .ok
.no_wrap:       move.w  d3,d1
                subq.l  #1,d4
                bne.s   .spin

                ;; Ran out of spin budget; report what we got.
                ACID_FAIL #1,d2,#MIN_TRANSITIONS

.ok:
                ACID_PASS
