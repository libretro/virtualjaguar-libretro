;
; tests/bus/bus_blitter_starves_cpu.s - blitter steals cycles from 68K.
;
; **EXPECTED TO FAIL today** (synchronous blitter, no contention).
;
; Inverse of bus_cpu_starves_blitter.s.  On real hardware:
;   While the blitter holds the bus, each 68K memory access stalls
;   waiting for the bus.  68K's effective MIPS while a long blit is
;   running is significantly lower than its no-blit MIPS.
;
; What our emulator does:
;   B_COMMAND triggers a blocking blit; 68K is "frozen" for zero wall
;   time and zero halflines.  After the blit returns, 68K runs at full
;   speed.  No interleaving possible.
;
; How we detect:
;   1. Run a fixed-size 68K loop (1000 RAM reads), measure VC delta.
;   2. Repeat with a long blit fired immediately before the loop
;      (the blit will have FINISHED in the emu by the time the loop
;      starts -- but on real hw the blit and loop overlap, so the
;      loop's VC delta would be larger).
;   3. Compare.  If the loop's elapsed halflines is the same with or
;      without the blit, the emulator isn't modelling bus arbitration.
;
; Detail codes:
;   1 = 68K loop took the same time with/without blit (no contention)
;   99 = couldn't capture VC reliably
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000
SCRATCH         equ     $000A0000

BLIT_CMD        equ     LFU_FN_C | SRCEN

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC for blit.
                lea     SRC.l,a0
                move.l  #1023,d0
.fill:          move.l  #$AA55AA55,(a0)+
                dbra    d0,.fill

                ;; Pre-fill SCRATCH so the 68K loop has data to read.
                lea     SCRATCH.l,a0
                move.l  #999,d0
.fill2:         move.l  #$DEADBEEF,(a0)+
                dbra    d0,.fill2

                ;; ------------------------------------------------------------
                ;; Run #1: 68K loop alone (1000 reads of SCRATCH).
                ;; ------------------------------------------------------------
                move.w  TOM_VC.l,d6
                ext.l   d6

                lea     SCRATCH.l,a0
                move.l  #999,d0
.loop1:         move.l  (a0)+,d1
                dbra    d0,.loop1

                move.w  TOM_VC.l,d7
                ext.l   d7
                sub.l   d6,d7
                move.l  d7,d3                           ; baseline VC delta

                ;; ------------------------------------------------------------
                ;; Run #2: fire a long blit, then immediately run the
                ;; same 1000-read loop.  On real hardware these would
                ;; overlap and the loop would take longer.
                ;; ------------------------------------------------------------
                move.w  TOM_VC.l,d6
                ext.l   d6

                ;; Fire the blit (long: 4096 px x 1 line = 8KB).
                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #$00011000,B_PIXLINECOUNTER
                move.l  #BLIT_CMD,B_COMMAND

                ;; ... and the read loop.
                lea     SCRATCH.l,a0
                move.l  #999,d0
.loop2:         move.l  (a0)+,d1
                dbra    d0,.loop2

                move.w  TOM_VC.l,d7
                ext.l   d7
                sub.l   d6,d7
                move.l  d7,d4                           ; loaded VC delta

                ;; ------------------------------------------------------------
                ;; Compare. d4 should be > d3 by at least d3/4 if bus
                ;; contention forces the 68K to stall during the blit.
                ;; ------------------------------------------------------------
                move.l  d4,d5
                sub.l   d3,d5
                ;; Require at least 50 halflines of slowdown to claim
                ;; contention is modelled (same threshold as the inverse
                ;; bus_cpu_starves_blitter test).
                moveq   #50,d2
                cmp.l   d2,d5
                bge     .pass

                ;; No measurable slowdown.  Bus contention not modelled.
                ;; This is the EXPECTED outcome on the current emulator.
                ACID_FAIL #1,d5,d2

.pass:          ACID_PASS
