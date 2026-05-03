;
; tests/blitter/gourd_basic.s - GOURD (gouraud shading) liveness check.
;
; GOURD (command bit 12 = $1000) enables gouraud interpolation on
; writes.  This test does not validate the precise interpolated values
; (the math involves I/F intensity registers we don't program here);
; it just verifies the gouraud-active write path produces *some*
; non-zero output on a pre-cleared destination phrase.  If dest stays
; exactly all-zero, the gouraud path didn't fire at all.
;
; Command bits:
;   SRCEN=1   (bit 0)
;   GOURD=1   (bit 12) -> $1000
;   ity = $C000 (LFU=S short-form)
; -> $0001D001
;
; Detail codes:
;   1 = dest still fully zero after blit (gouraud path inactive)
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

                ;; Non-trivial source colour data so any passthrough
                ;; or interpolation produces non-zero.
                move.l  #$11223344,SRC.l
                move.l  #$55667788,SRC+4.l

                ;; Pre-clear dest so we can detect "nothing happened".
                move.l  #$00000000,DST.l
                move.l  #$00000000,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS   ; 16bpp phrase
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                move.l  #$0001D001,B_COMMAND    ; SRCEN | GOURD | ity=S

                ;; If both halves stayed zero, gouraud path didn't run.
                move.l  DST.l,d5
                move.l  DST+4.l,d4
                or.l    d4,d5
                beq.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#$00000000
