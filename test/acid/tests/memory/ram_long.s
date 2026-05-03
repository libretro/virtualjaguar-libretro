;
; tests/memory/ram_long.s - 32-bit RW round-trip on main RAM.
;
; Writes 8 known 32-bit longs, reads back, verifies.  Catches any
; bug where the LE host's byte-swap macros (GET32/SET32) drop bytes.
;
; Detail: index of first mismatched long (0..7)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

BUF             equ     $00080000

                org     $802000
entry:
                ACID_INIT

                lea     BUF.l,a0
                move.l  #$12345678,(a0)+
                move.l  #$9ABCDEF0,(a0)+
                move.l  #$AAAAAAAA,(a0)+
                move.l  #$55555555,(a0)+
                move.l  #$00000001,(a0)+
                move.l  #$80000000,(a0)+
                move.l  #$DEADBEEF,(a0)+
                move.l  #$CAFEBABE,(a0)+

                lea     BUF.l,a0
                lea     .expected(pc),a1
                moveq   #7,d2
                moveq   #0,d3
.read:          move.l  (a0)+,d5
                move.l  (a1)+,d4
                cmp.l   d4,d5
                bne.s   .mismatch
                addq.l  #1,d3
                dbra    d2,.read

                ACID_PASS

.mismatch:      ACID_FAIL d3,d5,d4

.expected:      dc.l    $12345678,$9ABCDEF0,$AAAAAAAA,$55555555
                dc.l    $00000001,$80000000,$DEADBEEF,$CAFEBABE
