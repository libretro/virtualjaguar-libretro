;
; tests/bus/cpu_blitter_concurrent.s - 68K and blitter access RAM together.
;
; Issues a blitter copy and IMMEDIATELY (without waiting for it to
; finish) reads the source data from 68K.  On real hardware bus
; arbitration would interleave; in our emulator the blitter is
; synchronous and runs to completion before the next 68K instruction
; resumes, so the read always succeeds.
;
; **Expected to PASS today** (because synchronous blitter), but if
; we ever go async this test will surface the contention question.
;
; Detail codes:
;   1 = post-blit source read returned wrong value
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

                org     $802000
entry:
                ACID_INIT

                move.l  #$DEADBEEF,SRC.l
                move.l  #$00000000,DST.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL
                move.l  #$00010004,B_COUNT
                move.l  #$01800001,B_COMMAND    ; blit fires here

                ;; Read SRC immediately -- on async hardware this
                ;; would race; here it should just succeed.
                move.l  SRC.l,d5
                cmp.l   #$DEADBEEF,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#$DEADBEEF
