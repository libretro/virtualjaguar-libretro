;
; tests/blitter/bkgwren_test.s - BKGWREN + DCOMPEN background-write gate.
;
; DCOMPEN (data compare enable, command bit 8 = $0100) inhibits a
; write when the source pixel matches the comparison key (typically
; "background" / colour 0 / pattern data).  BKGWREN (background write
; enable, command bit 10 = $0400) re-enables those writes.  The most
; common idiom is "transparent blit": DCOMPEN on, BKGWREN off, source
; bytes equal to the compare key are skipped.
;
; This is intentionally permissive: a source where some bytes are
; zero (the implicit transparent value) and some are non-zero should,
; with DCOMPEN+!BKGWREN, leave the zero-source positions untouched
; and overwrite the non-zero positions.  Initial dest = $AA in every
; byte so we can tell what got skipped.
;
; Source 8 bytes:  $11 $22 $33 $44  $00 $00 $77 $88
; Initial dest:    $AA in all 8 bytes
; Expected dest:   $11 $22 $33 $44  $AA $AA $77 $88
;                                  ^^^^ zero-source positions kept
;
; Command bits:
;   SRCEN=1   (bit 0)
;   DSTEN=1   (bit 5) -> $00000020   ; need to read existing dest
;   DCOMPEN=1 (bit 8) -> $00000100
;   ity=$C000 (LFU=S)
; -> $0001C121
;
; A?_FLAGS for 8bpp phrase: $00001018.
;
; Detail codes:
;   N = first dest byte index (1-based) that doesn't match expected
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

                ;; Source pattern: $11 $22 $33 $44 $00 $00 $77 $88
                move.l  #$11223344,SRC.l
                move.l  #$00007788,SRC+4.l

                ;; Initial dest: all $AA.
                move.l  #$AAAAAAAA,DST.l
                move.l  #$AAAAAAAA,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001018,B_A1_FLAGS   ; 8bpp phrase
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001018,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                ;; 1 line, 8 pixels.
                move.l  #$00010008,B_COUNT
                ;; SRCEN | DSTEN | DCOMPEN | LFU=S
                move.l  #$0001C121,B_COMMAND

                ;; Walk dest vs expected.
                lea     DST.l,a0
                lea     .expected(pc),a1
                moveq   #7,d2
                moveq   #1,d3
.cmp_loop:      move.b  (a0)+,d5
                move.b  (a1)+,d4
                cmp.b   d4,d5
                bne.s   .bad
                addq.l  #1,d3
                dbra    d2,.cmp_loop

                ACID_PASS

.bad:           and.l   #$FF,d5
                and.l   #$FF,d4
                ACID_FAIL d3,d5,d4

.expected:      dc.b    $11,$22,$33,$44,$AA,$AA,$77,$88
                even
