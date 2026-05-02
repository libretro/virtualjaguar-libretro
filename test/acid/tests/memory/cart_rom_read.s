;
; tests/memory/cart_rom_read.s - reading our own cart bytes works.
;
; The first 32 bytes of the cart are the "ATARI APPROVED..." tag in
; jaguar_header.s.  Read byte 0 and verify it's 'A' ($41).
;
; If this fails, the cart-ROM dispatch in JaguarReadByte/Word/Long is
; broken (or the cart wasn't loaded into the right address).
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

CART_BASE       equ     $00800000               ; cart maps here

                org     $802000
entry:
                ACID_INIT

                ;; "ATARI APPROVED DATA HEADER ATRI " starts at $800000.
                ;; offset 0='A', 1='T', 2='A', 3='R', 4='I', 5=' ', 6='A'...
                move.b  CART_BASE.l,d5          ; expect 'A'
                cmp.b   #'A',d5
                bne     .bad1
                move.b  CART_BASE+4.l,d5        ; expect 'I'
                cmp.b   #'I',d5
                bne     .bad2
                move.b  CART_BASE+6.l,d5        ; expect 'A' (start of "APPROVED")
                cmp.b   #'A',d5
                bne     .bad3

                ACID_PASS

.bad1:          and.l   #$FF,d5
                ACID_FAIL #1,d5,#'A'
.bad2:          and.l   #$FF,d5
                ACID_FAIL #2,d5,#'I'
.bad3:          and.l   #$FF,d5
                ACID_FAIL #3,d5,#'A'
