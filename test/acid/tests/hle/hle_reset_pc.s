;
; tests/hle/hle_reset_pc.s - HLE BIOS writes cart entry to reset PC.
;
; The Jaguar 68000 reset vector at $00000004 is the initial Program
; Counter.  HLE BIOS init reads the cart's entry word at $800404 and
; writes it to $00000004 before pulsing 68K reset.  For our acid
; tests, the cart entry is $00802000 (see include/jaguar_header.s).
;
; Verifies the long-word at $00000004 is $00802000 once execution starts.
;
; Detail codes:
;   1 = reset PC @ $00000004 not $00802000
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

PC_ADDR         equ     $00000004
PC_EXPECTED     equ     $00802000

                org     $802000
entry:
                ACID_INIT

                move.l  PC_ADDR.l,d5
                cmp.l   #PC_EXPECTED,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#PC_EXPECTED
