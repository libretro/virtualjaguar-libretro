;
; tests/hle/hle_post_init_state.s - HLE BIOS leaves expected register state.
;
; Verifies the values JaguarReset's HLE branch writes:
;   - $0804 = $00000001 (HLE_BIOS_WORK_FLAG_ADDR / WORK_READY)
;   - $F03000 = some non-zero GPU auth magic
;
; If we extend HLE to match more real-BIOS state in the future, add
; assertions here so we don't silently regress.
;
; Detail codes:
;   1 = $0804 work-flag wrong
;   2 = GPU auth magic at $F03000 zero (HLE init didn't run?)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

WORK_FLAG       equ     $0804
GPU_AUTH        equ     $F03000

                org     $802000
entry:
                ACID_INIT

                ;; Skip the auth-magic check if BIOS path is in use --
                ;; in that case, the real BIOS sets $F03000 differently.
                ;; This test is HLE-only by convention.
                move.l  WORK_FLAG.l,d5
                cmp.l   #$00000001,d5
                bne.s   .bad_flag

                move.l  GPU_AUTH.l,d5
                tst.l   d5
                beq.s   .no_auth

                ACID_PASS

.bad_flag:      ACID_FAIL #1,d5,#$00000001
.no_auth:       ACID_FAIL #2,#0,#1
