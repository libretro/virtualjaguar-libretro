;
; tests/quirks/a1_yadd_quirk_partner.s - companion to a2_yadd_tied_to_a1.s.
;
; The Jaguar 1 hardware bug ties A2's YADD bit to A1's.  The partner
; test (a2_yadd_tied_to_a1.s) verifies that a YADD=0 on A2 still
; advances A2's Y if A1 has YADD=1.  This test is the sanity check
; for the *other* side: if A1 has YADD=1, A1's own Y must also
; advance after a 1-line blit.  If A1's YADD is broken too, that
; would mask the partner test.
;
; Detail codes:
;   1 = A1 Y did not advance after a YADD=1 blit
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

                ;; A1 (dest) FLAGS with YADD=1 (bit 18 = $40000),
                ;; pixsize=4, phrase, pitch=2: $00041020
                move.l  #DST,B_A1_BASE
                move.l  #$00041020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL           ; X=0, Y=0

                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                move.l  #$0001C019,B_COMMAND    ; SRCEN|LFU=src|UPDA1|UPDA2

                ;; Read A1_PIXEL.  Y is in upper 16 bits.
                move.l  B_A1_PIXEL,d5
                swap    d5
                and.l   #$FFFF,d5

                tst.w   d5
                beq.s   .no_advance

                ACID_PASS

.no_advance:    ACID_FAIL #1,d5,#1
