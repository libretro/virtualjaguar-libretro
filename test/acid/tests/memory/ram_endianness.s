;
; tests/memory/ram_endianness.s - Jaguar is big-endian; verify the
; emulator preserves byte order through 32->8 access.
;
; Writes a 32-bit value, reads each byte individually, verifies the
; high byte of the longword reads through the lowest address (the
; big-endian convention).
;
; If this fails on a little-endian host, the GET/SET byte-swap macros
; in vjag_memory.h are wrong.
;
; Detail codes:
;   1 = byte 0 (high byte) wrong
;   2 = byte 1 wrong
;   3 = byte 2 wrong
;   4 = byte 3 (low byte) wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

BUF             equ     $00080000

                org     $802000
entry:
                ACID_INIT

                ;; Write 32-bit $12345678 at BUF.
                move.l  #$12345678,BUF.l

                ;; Read each byte; expect $12, $34, $56, $78 in order.
                move.b  BUF.l,d5
                cmp.b   #$12,d5
                bne     .b0_bad
                move.b  BUF+1.l,d5
                cmp.b   #$34,d5
                bne     .b1_bad
                move.b  BUF+2.l,d5
                cmp.b   #$56,d5
                bne     .b2_bad
                move.b  BUF+3.l,d5
                cmp.b   #$78,d5
                bne     .b3_bad

                ACID_PASS

.b0_bad:        and.l   #$FF,d5
                ACID_FAIL #1,d5,#$12
.b1_bad:        and.l   #$FF,d5
                ACID_FAIL #2,d5,#$34
.b2_bad:        and.l   #$FF,d5
                ACID_FAIL #3,d5,#$56
.b3_bad:        and.l   #$FF,d5
                ACID_FAIL #4,d5,#$78
