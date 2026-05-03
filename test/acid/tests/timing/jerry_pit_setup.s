;
; tests/timing/jerry_pit_setup.s - JERRY PIT writable setup -> readback round-trip.
;
; Per src/jerry/jerry.c:
;   $F10000/$F10002 are WRITE addresses for JPIT1/JPIT2 (timer 1
;     prescaler/divider).  Writes here arm the timer via
;     JERRYResetPIT1().
;   $F10036/$F10038 are READBACK addresses for the same registers
;     (added by commit 1ca2fdc).
;
; This test arms the timer with a known prescaler/divider via the
; WRITABLE addresses, then reads back through the READBACK addresses
; and verifies the values match.
;
; Detail codes:
;   1 = prescaler readback wrong
;   2 = divider readback wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

;; WRITABLE setup
JPIT1_W         equ     $F10000                 ; timer 1 prescaler (W)
JPIT2_W         equ     $F10002                 ; timer 1 divider   (W)

;; READBACK
JPIT1_R         equ     $F10036
JPIT2_R         equ     $F10038

                org     $802000
entry:
                ACID_INIT

                ;; Arm timer 1 with known values via writable regs.
                move.w  #$1234,JPIT1_W
                move.w  #$5678,JPIT2_W

                ;; Read back via readback regs.
                move.w  JPIT1_R,d5
                cmp.w   #$1234,d5
                bne.s   .pit1_bad
                move.w  JPIT2_R,d5
                cmp.w   #$5678,d5
                bne.s   .pit2_bad

                ACID_PASS

.pit1_bad:      and.l   #$FFFF,d5
                ACID_FAIL #1,d5,#$1234
.pit2_bad:      and.l   #$FFFF,d5
                ACID_FAIL #2,d5,#$5678
