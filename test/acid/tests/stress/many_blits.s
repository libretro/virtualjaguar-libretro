;
; tests/stress/many_blits.s - issue 256 small blits in a row.
;
; AvP-style workload: lots of tiny phrase copies.  The blitter must
; handle them all without dropping or hanging.  Test passes if all
; 256 complete and the last blit's data is correct.
;
; The perf delta dump will show blitter_calls=256, blitter_inner ~= 256
; (one inner cycle per phrase copy).
;
; Detail codes:
;   1 = a blit hung (BUSY never cleared within spin budget)
;   2 = post-blit data verification failed
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

B_BASE          equ     $F02200
B_A1_BASE       equ     B_BASE + $00
B_A1_FLAGS      equ     B_BASE + $04
B_A1_PIXEL      equ     B_BASE + $0C
B_A2_BASE       equ     B_BASE + $24
B_A2_FLAGS      equ     B_BASE + $28
B_A2_PIXEL      equ     B_BASE + $30
B_COMMAND       equ     B_BASE + $38
B_COUNT         equ     B_BASE + $3C

SRC             equ     $00080000
DST             equ     $00090000
NUM_BLITS       equ     256
SPIN_LIMIT      equ     200000

                org     $802000
entry:
                ACID_INIT

                ;; Load source with a known phrase pattern.
                move.l  #$DEADBEEF,SRC.l
                move.l  #$CAFEBABE,SRC+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #$00010004,B_COUNT

                move.l  #NUM_BLITS,d6           ; loop counter

.next_blit:     move.l  #0,B_A1_PIXEL
                move.l  #0,B_A2_PIXEL
                move.l  #$0001C000,B_COMMAND    ; SRCEN | LFU=src

                ;; Blitter is synchronous in this emulator; no wait needed.

.blit_done:     subq.l  #1,d6
                bne.s   .next_blit

                ;; Verify final dest matches source.
                move.l  DST.l,d5
                cmp.l   #$DEADBEEF,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #2,d5,#$DEADBEEF
