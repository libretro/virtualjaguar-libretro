;
; tests/bus/blitter_back_to_back.s - issue 4 blits with no spacing.
;
; Real hardware would queue / serialise these; our emulator runs
; each synchronously.  Either way, all 4 should land at distinct
; destinations.
;
; Detail codes:
;   N = blit N's destination doesn't match expected pattern
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

                move.l  #$11111111,SRC.l
                move.l  #$22222222,SRC+8.l
                move.l  #$33333333,SRC+16.l
                move.l  #$44444444,SRC+24.l

                lea     DST.l,a0
                moveq   #7,d0
.zero:          clr.l   (a0)+
                dbra    d0,.zero

                move.l  #$00001020,B_A1_FLAGS
                move.l  #$00001020,B_A2_FLAGS
                move.l  #$00010004,B_COUNT

                ;; Blit 1: SRC+0 -> DST+0
                move.l  #DST,B_A1_BASE
                move.l  #SRC,B_A2_BASE
                move.l  #0,B_A1_PIXEL
                move.l  #0,B_A2_PIXEL
                move.l  #$0001C000,B_COMMAND

                ;; Blit 2: SRC+8 -> DST+8
                move.l  #DST+8,B_A1_BASE
                move.l  #SRC+8,B_A2_BASE
                move.l  #0,B_A1_PIXEL
                move.l  #0,B_A2_PIXEL
                move.l  #$0001C000,B_COMMAND

                ;; Blit 3: SRC+16 -> DST+16
                move.l  #DST+16,B_A1_BASE
                move.l  #SRC+16,B_A2_BASE
                move.l  #0,B_A1_PIXEL
                move.l  #0,B_A2_PIXEL
                move.l  #$0001C000,B_COMMAND

                ;; Blit 4: SRC+24 -> DST+24
                move.l  #DST+24,B_A1_BASE
                move.l  #SRC+24,B_A2_BASE
                move.l  #0,B_A1_PIXEL
                move.l  #0,B_A2_PIXEL
                move.l  #$0001C000,B_COMMAND

                ;; Verify all 4.
                move.l  DST.l,d5
                cmp.l   #$11111111,d5
                bne     .b1_bad
                move.l  DST+8.l,d5
                cmp.l   #$22222222,d5
                bne     .b2_bad
                move.l  DST+16.l,d5
                cmp.l   #$33333333,d5
                bne     .b3_bad
                move.l  DST+24.l,d5
                cmp.l   #$44444444,d5
                bne     .b4_bad

                ACID_PASS

.b1_bad:        ACID_FAIL #1,d5,#$11111111
.b2_bad:        ACID_FAIL #2,d5,#$22222222
.b3_bad:        ACID_FAIL #3,d5,#$33333333
.b4_bad:        ACID_FAIL #4,d5,#$44444444
