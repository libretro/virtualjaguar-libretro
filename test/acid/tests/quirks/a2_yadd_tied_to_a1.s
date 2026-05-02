;
; tests/quirks/a2_yadd_tied_to_a1.s - Jaguar 1 hardware bug.
;
; Per JTRM and BlitterMidsummer2 source (the line "a2addy = a1addy"):
; "Bugs in Jaguar I -- A2 channel Y add bit is tied to A1's".
;
; Configure A1 with YADD=1 (add 1 to Y) and A2 with YADD=0; then
; observe whether A2's Y actually advances after a blit.  If we
; correctly model the J1 quirk, A2 Y will advance even though we
; asked for YADD=0.
;
; This test currently checks the QUIRK is present.  If we ever
; reach J2-accurate behaviour the test should be inverted.
;
; Detail codes:
;   1 = A2 Y didn't advance (J2 behavior; we want J1 quirk to be
;       active because real game ROMs were written for J1)
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

                ;; A1 (dest) FLAGS with YADD bit set (bit 18 = $40000)
                ;; plus pixsize=4, phrase, e=2:
                ;; $00041020
                move.l  #DST,B_A1_BASE
                move.l  #$00041020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL           ; X=0, Y=0

                ;; A2 (src) FLAGS WITHOUT YADD set:
                ;; $00001020
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL           ; X=0, Y=0

                move.l  #$00010004,B_COUNT
                ;; UPDA1 + UPDA2 to actually update pointers.
                ;; UPDA1=bit 4 ($10), UPDA2=bit 3 ($08).  Plus SRCEN+LFU=src.
                move.l  #$0001C019,B_COMMAND

                ;; Read back A2_PIXEL.  Y is in upper 16 bits.
                move.l  B_A2_PIXEL,d5
                swap    d5                      ; now d5 low = Y
                and.l   #$FFFF,d5

                ;; Quirk active: A2 Y advanced (=1, since A1 YADD=1).
                ;; Without quirk: A2 Y stays 0.
                tst.w   d5
                beq.s   .no_advance

                ACID_PASS                       ; J1 quirk active -> good

.no_advance:    ACID_FAIL #1,d5,#1
