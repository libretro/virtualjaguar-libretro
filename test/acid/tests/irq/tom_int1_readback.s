;
; tests/irq/tom_int1_readback.s - TOM_INT1 enable mask is *write-only*.
;
; Per src/tom/tom.c the documented hardware semantic for $F000E0 is
; "R/W ---xxxxx ---xxxxx" -- only the low 5 bits of each byte are
; meaningful, and writes to bits 8..12 (the enable mask high byte)
; are NOT readable.  Reads return pending status in the low 5 bits
; of the low byte; the high byte always reads as 0.
;
; This test pins down that semantic so a future change can't
; silently make the enable bits readable.  If real hardware does
; reflect them, this test should FAIL and force a discussion about
; whether the change matches the spec.
;
; Detail codes:
;   1 = high-byte read returned non-zero (enable bits leaked into
;       readback)
;   2 = low-byte read non-zero immediately after CLR_ALL (pending
;       bits stuck)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_INT1        equ     $F000E0

                org     $802000
entry:
                ACID_INIT

                ;; Clear any latched pending bits.
                move.w  #$1F00,TOM_INT1                 ; CLR_ALL

                ;; Write a real enable mask in the LOW byte (per
                ;; src/tom/tom.c the LOW byte holds the enable mask;
                ;; this is the path the test claims to be probing).
                ;; $0F = enable VIDEO|GPU|OPFLAG|TIMER (not DSP).
                move.w  #$000F,TOM_INT1

                ;; Read back.
                move.w  TOM_INT1,d5

                ;; High byte must be zero -- the documented hardware
                ;; semantic is that the enable mask is write-only
                ;; (per the comment at src/tom/tom.c:85
                ;; "R/W ---xxxxx ---xxxxx").
                move.l  d5,d6
                and.l   #$FF00,d6
                tst.l   d6
                bne.s   .high_leaked

                ;; Low 5 bits hold pending status, which must be 0
                ;; immediately after CLR_ALL (we never armed any IRQ
                ;; source that could re-pend within these 5 cycles).
                move.l  d5,d6
                and.l   #$001F,d6
                tst.l   d6
                bne.s   .low_stuck

                ACID_PASS

.high_leaked:   and.l   #$FFFF,d5
                ACID_FAIL #1,d5,#0
.low_stuck:     and.l   #$FFFF,d5
                ACID_FAIL #2,d5,#0
