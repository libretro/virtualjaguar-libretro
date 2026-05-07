;
; tests/memory/ram_byte.s - 8-bit RW round-trip on main RAM.
;
; Writes a known byte pattern across a small window, reads it back,
; verifies it survived.  If this fails, every other test that uses
; RAM is suspect.
;
; Detail: index of first mismatched byte (0..15)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

BUF             equ     $00080000               ; 2 MB into RAM, well clear

                org     $802000
entry:
                ACID_INIT

                ;; Pattern: index XOR $A5, written 16 bytes.
                lea     BUF.l,a0
                moveq   #15,d2                  ; d2 = loop counter
                moveq   #0,d3                   ; d3 = index 0..15
.write:         move.b  d3,d4
                eor.b   #$A5,d4
                move.b  d4,(a0)+
                addq.b  #1,d3
                dbra    d2,.write

                ;; Read back, compare.
                lea     BUF.l,a0
                moveq   #15,d2
                moveq   #0,d3
.read:          move.b  d3,d4
                eor.b   #$A5,d4                 ; d4 = expected
                move.b  (a0)+,d5                ; d5 = observed
                cmp.b   d4,d5
                bne.s   .mismatch
                addq.b  #1,d3
                dbra    d2,.read

                ACID_PASS

.mismatch:
                and.l   #$FF,d4
                and.l   #$FF,d5
                and.l   #$FF,d3
                ACID_FAIL d3,d5,d4
