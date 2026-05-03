;
; tests/hle/hle_ssp_value.s - HLE BIOS writes SSP=$00004000 at $00000000.
;
; The Jaguar 68000 reset vector at $00000000 is the initial Supervisor
; Stack Pointer.  Cart-mode HLE init writes $00004000 there (BIOS
; workspace ends at $4000; stack grows down).  This test verifies the
; long-word at $00000000 is exactly $00004000 once execution starts.
;
; Detail codes:
;   1 = SSP @ $00000000 not $00004000 (HLE init didn't run, or value
;       changed)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

SSP_ADDR        equ     $00000000
SSP_EXPECTED    equ     $00004000

                org     $802000
entry:
                ACID_INIT

                move.l  SSP_ADDR.l,d5
                cmp.l   #SSP_EXPECTED,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#SSP_EXPECTED
