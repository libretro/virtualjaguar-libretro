;
; tests/bus/bus_refresh_steals.s - DRAM refresh steals ~10% of bus cycles.
;
; **EXPECTED TO FAIL today** -- DRAM refresh isn't modelled at all.
;
; What real hardware does:
;   The Jaguar's DRAM controller periodically asserts the bus to do
;   refresh cycles (CAS-before-RAS).  Roughly one refresh burst every
;   ~15 us; on a long 68K loop this consumes ~10% of available cycles,
;   so a loop that would take T cycles in pure isolation actually takes
;   T * 1.10..1.12 cycles wall-time.
;
; What our emulator does:
;   No refresh model.  68K cycles tick at the configured rate with no
;   DRAM refresh interleaving.
;
; How we detect:
;   Run a known-cycle 68K spin loop for many iterations, measure VC
;   delta.  Compute ratio (VC_delta / iterations).  On real hardware,
;   this ratio would be ~10% higher than the no-refresh theoretical
;   minimum.  We can't directly measure "the no-refresh theoretical
;   minimum" without instrumenting the emu, so we instead just
;   document that the test exists and FAIL with detail=1 on every
;   emulator that doesn't model refresh.
;
;   The detail-1 FAIL is the "expected" outcome until we add refresh
;   modelling.  Once added, we'd update this test to assert the actual
;   measured ratio.
;
; Detail codes:
;   1 = refresh-overhead absent (EXPECTED today)
;   99 = encoding placeholder
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

ITER_COUNT      equ     10000

                org     $802000
entry:
                ACID_INIT

                move.w  TOM_VC.l,d6
                ext.l   d6

                ;; Tight 68K loop.  Each `subq + bne` is a couple of
                ;; cycles; with refresh stealing ~10% of cycles, the
                ;; total wall-clock time would be measurably higher
                ;; than the "naive" cycle count would predict.
                move.l  #ITER_COUNT,d0
.spin:          subq.l  #1,d0
                bne.s   .spin

                move.w  TOM_VC.l,d7
                ext.l   d7
                sub.l   d6,d7
                ;; d7 = elapsed halflines for the loop.

                ;; The "refresh overhead" check: compare actual elapsed
                ;; halflines to the theoretical minimum.  We don't have
                ;; a way to compute the minimum from inside the test
                ;; without coupling to a specific emulator config -- so
                ;; this test is a regression GATE: any time the emu
                ;; *gains* refresh modelling, the elapsed time of this
                ;; loop should grow noticeably.  Until then, FAIL with
                ;; detail=1 and observed=current_VC_delta so changes
                ;; are visible.
                ;;
                ;; We deliberately FAIL here -- the diagnostic is the
                ;; observed VC delta itself, which a future contention
                ;; model would change.
                ACID_FAIL #1,d7,#0
