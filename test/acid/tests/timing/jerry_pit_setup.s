; JERRY PIT setup and live prescaler readback.
; Jaguar Software Reference v2.4, pp. 70-71: writes preset the
; counters, and the prescaler counts down at the processor clock.
; Detail codes: 1 = prescaler out of range, 2 = divider wrong,
;               3 = prescaler did not count down.
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
                bhs.s   .pit1_bad
                ;; Allow bus and instruction timing differences, but
                ;; require a value near the programmed reload.
                cmp.w   #$1134,d5
                blo.s   .pit1_bad
                move.w  JPIT1_R,d6
                cmp.w   d5,d6
                bhs.w   .pit1_stuck
                move.w  JPIT2_R,d5
                cmp.w   #$5678,d5
                bne.s   .pit2_bad

                ACID_PASS

.pit1_bad:      and.l   #$FFFF,d5
                ACID_FAIL #1,d5,#$1234
.pit2_bad:      and.l   #$FFFF,d5
                ACID_FAIL #2,d5,#$5678
.pit1_stuck:    and.l   #$FFFF,d6
                and.l   #$FFFF,d5
                ACID_FAIL #3,d6,d5
