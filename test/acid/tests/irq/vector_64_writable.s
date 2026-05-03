;
; tests/irq/vector_64_writable.s - vector 64 ($00000100) must be RW.
;
; Writes a known value to vector 64 (the autovector landing pad used
; by irq_ack_handler() for ALL hardware IRQs in our 68K core), reads
; back, verifies it persists.  Without this working, vblank_delivery
; and every other IRQ test can never PASS -- the handler we install
; would just be ignored.
;
; The HLE BIOS init writes a default RTE stub here, so the test value
; we write must be the LAST writer for the readback to match.
;
; Detail codes:
;   1 = readback != written value
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

HW_IRQ_VECTOR   equ     $00000100
TEST_VAL        equ     $C0DEFACE

                org     $802000
entry:
                ACID_INIT

                move.l  #TEST_VAL,HW_IRQ_VECTOR.l
                move.l  HW_IRQ_VECTOR.l,d5
                cmp.l   #TEST_VAL,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#TEST_VAL
