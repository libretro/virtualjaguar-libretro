;
; tests/memory/ram_walking_one.s - walking-1s pattern over 1 KB of RAM.
;
; For each long in a 256-long (1 KB) window, write a value with a
; single bit set in a marching pattern (bit 0, 1, 2, ... 31, 0, 1, ...).
; Read back and verify.  Catches stuck-at-0 / stuck-at-1 / cross-talk
; bugs in the byte-swap macros that a uniform pattern would mask.
;
; Detail codes:
;   detail   = index of first mismatched long (0..255)
;   observed = readback value
;   expected = walking-1 pattern that should have been there
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

BUF             equ     $00080000
COUNT           equ     256

                org     $802000
entry:
                ACID_INIT

                ;; Write phase.
                lea     BUF.l,a0
                move.l  #COUNT-1,d2             ; loop counter
                moveq   #0,d3                   ; bit position 0..31
                move.l  #1,d4                   ; current walking value
.write:         move.l  d4,(a0)+
                addq.l  #1,d3
                cmp.l   #32,d3
                bne.s   .no_wrap_w
                moveq   #0,d3
                moveq   #1,d4
                bra.s   .next_w
.no_wrap_w:     lsl.l   #1,d4
.next_w:        dbra    d2,.write

                ;; Read-back phase.
                lea     BUF.l,a0
                move.l  #COUNT-1,d2
                moveq   #0,d3
                move.l  #1,d4
                moveq   #0,d6                   ; index counter
.read:          move.l  (a0)+,d5
                cmp.l   d4,d5
                bne     .mismatch
                addq.l  #1,d6
                addq.l  #1,d3
                cmp.l   #32,d3
                bne.s   .no_wrap_r
                moveq   #0,d3
                moveq   #1,d4
                bra.s   .next_r
.no_wrap_r:     lsl.l   #1,d4
.next_r:        dbra    d2,.read

                ACID_PASS

.mismatch:      ACID_FAIL d6,d5,d4
