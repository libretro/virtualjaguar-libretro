;
; tests/memory/ram_word.s - 16-bit RW round-trip on main RAM.
;
; Writes 8 known 16-bit words, reads back, verifies.
;
; Detail: index of first mismatched word (0..7)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

BUF             equ     $00080000

                org     $802000
entry:
                ACID_INIT

                lea     BUF.l,a0
                move.w  #$1234,(a0)+
                move.w  #$5678,(a0)+
                move.w  #$9ABC,(a0)+
                move.w  #$DEF0,(a0)+
                move.w  #$AAAA,(a0)+
                move.w  #$5555,(a0)+
                move.w  #$0001,(a0)+
                move.w  #$8000,(a0)+

                ;; Read back, compare.
                lea     BUF.l,a0
                lea     .expected(pc),a1
                moveq   #7,d2
                moveq   #0,d3
.read:          move.w  (a0)+,d5
                move.w  (a1)+,d4
                cmp.w   d4,d5
                bne.s   .mismatch
                addq.l  #1,d3
                dbra    d2,.read

                ACID_PASS

.mismatch:
                and.l   #$FFFF,d4
                and.l   #$FFFF,d5
                ACID_FAIL d3,d5,d4

.expected:      dc.w    $1234,$5678,$9ABC,$DEF0,$AAAA,$5555,$0001,$8000
