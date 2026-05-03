;
; tests/blitter/lfu_passthrough_src.s - LFU=$C (S) source passthrough.
;
; Frames the basic SRC->DST copy explicitly as an LFU function test:
; LFU function $C selects "S" (output = source).  Same behaviour as
; copy_simple, but documented as the LFU passthrough case.
;
; Detail codes:
;   1 = DST does not match SRC after LFU=S blit
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

                ;; Recognisable source phrase.
                move.l  #$11223344,SRC.l
                move.l  #$55667788,SRC+4.l

                ;; Sentinel destination so we can see the overwrite.
                move.l  #$AAAAAAAA,DST.l
                move.l  #$AAAAAAAA,DST+4.l

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS
                move.l  #0,B_A1_PIXEL
                move.l  #SRC,B_A2_BASE
                move.l  #$00001020,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                move.l  #$00010004,B_COUNT
                ;; SRCEN | LFU short-form ity = $C000  (LFU function $C = S)
                move.l  #$0001C000,B_COMMAND

                ;; Compare DST hi/lo against SRC.
                move.l  DST.l,d5
                cmp.l   #$11223344,d5
                bne.s   .bad
                move.l  DST+4.l,d5
                cmp.l   #$55667788,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#$11223344
