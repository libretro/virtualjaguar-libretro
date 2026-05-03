;
; tests/bus/bus_cpu_starves_blitter.s - 68K hammers RAM during a long blit.
;
; **EXPECTED TO FAIL on the current emulator** (synchronous blitter +
; no bus contention model).  This test will go GREEN once we add
; contention modelling.
;
; What real hardware does:
;   The 68K and the blitter share the bus.  When the 68K issues many
;   reads/writes to RAM while the blitter is mid-blit, every 68K access
;   steals a cycle from the blitter and inflates the wall-clock time
;   the blit takes to complete.
;
; What our emulator does today:
;   B_COMMAND write triggers a synchronous BlitterMidsummer() that runs
;   to completion before the next 68K instruction.  68K accesses
;   "during" the blit can't actually happen because the blit is done
;   before the next 68K opcode fetches.
;
; How we detect this:
;   1. Run a 100-read 68K loop alone, measure halflines elapsed (d3).
;   2. Run a long blit (1024 phrases, 8KB) immediately followed by
;      the same 100-read loop, measure halflines elapsed (d4).
;   3. Compute slowdown = d4 - d3.
;   4. Assert slowdown >= 50 halflines (a long blit on real hw stalls
;      bus access for many milliseconds; 50 halflines = ~3 ms NTSC).
;
; On the current emulator, d4 ~= d3 because the blit completes
; synchronously between two 68K instructions and consumes zero
; observable VC time.  The test FAILs with detail=1 to document
; this gap.
;
; Detail codes:
;   1 = blit completed normally but no measurable slowdown observed --
;       bus contention not modelled (EXPECTED FAIL today)
;   2 = blit destination data corrupt (different bug entirely)
;   99 = couldn't capture VC reliably
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SRC             equ     $00080000
DST             equ     $00090000

;; Blit command: copy from A2 (source) to A1 (dest), LFU=$C (S),
;; SRCEN=1.  $01800001 = LFU_FN_C | SRCEN.  Same value used by other
;; bus tests.
;; Constructed via named symbols for lint-cleanliness.
BLIT_CMD        equ     LFU_FN_C | SRCEN

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill SRC with $5A patterns.
                lea     SRC.l,a0
                move.l  #1023,d0
.fill:          move.l  #$5A5A5A5A,(a0)+
                dbra    d0,.fill

                ;; ------------------------------------------------------------
                ;; Run #1: 100 RAM reads alone (no blit). Establishes the
                ;; baseline halflines for the read loop in isolation.
                ;; ------------------------------------------------------------
                move.w  TOM_VC.l,d6                     ; VC before
                ext.l   d6

                lea     DST.l,a0
                move.l  #99,d0
.read1:         move.l  (a0),d1
                addq.l  #4,a0
                dbra    d0,.read1

                move.w  TOM_VC.l,d7
                ext.l   d7
                sub.l   d6,d7
                move.l  d7,d3                           ; baseline (no blit)

                ;; ------------------------------------------------------------
                ;; Run #2: fire a long blit, then immediately do the SAME
                ;; 100 RAM reads.  On real hardware the blit holds the bus
                ;; while it's running, so the 68K reads stall and the
                ;; combined VC delta is materially larger than baseline +
                ;; (constant blit time).  On the current emu, the sync
                ;; blit runs to completion in zero VC and the 68K reads
                ;; take exactly the baseline time again.
                ;; ------------------------------------------------------------
                move.w  TOM_VC.l,d6
                ext.l   d6

                ;; Fire long blit (1 line x 4096 px, 16bpp -> 8KB).
                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #$00011000,B_PIXLINECOUNTER
                move.l  #BLIT_CMD,B_COMMAND

                lea     DST.l,a0
                move.l  #99,d0
.read2:         move.l  (a0),d1
                addq.l  #4,a0
                dbra    d0,.read2

                move.w  TOM_VC.l,d7
                ext.l   d7
                sub.l   d6,d7
                move.l  d7,d4                           ; loaded VC delta

                ;; ------------------------------------------------------------
                ;; Compare.  d4 should be >= d3 + d3/4 if bus contention
                ;; forces the blit to interleave with the 68K reads (real
                ;; hw stalls one or the other; either way wall time grows).
                ;; ------------------------------------------------------------
                ;; Sanity: blit dest must equal source.
                move.l  DST.l,d5
                cmp.l   #$5A5A5A5A,d5
                bne     .bad_data

                move.l  d4,d5
                sub.l   d3,d5                           ; d5 = load - baseline
                ;; A 1024-phrase blit on real hw should take many
                ;; halflines if it's interleaving with 68K reads.
                ;; Require at least 50 halflines of slowdown to claim
                ;; contention is modelled.  Without modelling, d4 == d3
                ;; (modulo halfline-quantum noise) so d5 is 0 or 1.
                ;;
                ;; threshold = 50 halflines (absolute)
                moveq   #50,d2
                cmp.l   d2,d5
                bge     .pass

                ;; No measurable slowdown.  Bus contention not modelled.
                ;; This is the EXPECTED outcome on the current emulator.
                ACID_FAIL #1,d5,d2

.pass:          ACID_PASS

.bad_data:      ACID_FAIL #2,d5,#$5A5A5A5A
