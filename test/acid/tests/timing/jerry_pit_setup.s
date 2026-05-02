;
; tests/timing/jerry_pit_setup.s - JERRY PIT registers readable after
; configure.
;
; Writes a non-zero divider to JPIT1/JPIT2 and reads them back.  This
; is the path that commit 1ca2fdc fixed (was returning 0 silently);
; verify the read returns what we wrote.
;
; NOTE: real hardware would have the PIT counting down from those
; values; this test only checks the readback path, not the count-
; down behaviour (that's a future test in this category).
;
; Detail codes:
;   1 = JPIT1 prescaler readback wrong
;   2 = JPIT2 divider readback wrong
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

JPIT1           equ     $F10036                 ; timer 1 prescaler
JPIT2           equ     $F10038                 ; timer 1 divider

                org     $802000
entry:
                ACID_INIT

                ;; Configure timer 1 with known values.
                move.w  #$1234,JPIT1
                move.w  #$5678,JPIT2

                ;; Read back.
                move.w  JPIT1,d5
                cmp.w   #$1234,d5
                bne.s   .pit1_bad
                move.w  JPIT2,d5
                cmp.w   #$5678,d5
                bne.s   .pit2_bad

                ACID_PASS

.pit1_bad:      and.l   #$FFFF,d5
                ACID_FAIL #1,d5,#$1234
.pit2_bad:      and.l   #$FFFF,d5
                ACID_FAIL #2,d5,#$5678
