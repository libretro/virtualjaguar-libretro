;
; tests/quirks/m68k_set_sr_supervisor.s - 68K boots in supervisor mode.
;
; Cart code on the Jaguar starts in supervisor mode (S bit of SR set).
; If the core ever boots us in user mode, every supervisor-only
; instruction (move.w sr,Dn / move to SR / RTE / stop / ...) the test
; suite uses would silently misbehave.
;
; `move.w sr,Dn` is privileged on later 68K family but allowed on
; 68000 -- our core targets 68000.  We read SR, mask the S bit
; ($2000), and verify it is set.
;
; Detail codes:
;   1 = SR S bit clear (we are in user mode somehow)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

S_BIT           equ     $2000

                org     $802000
entry:
                ACID_INIT

                move.w  sr,d5
                and.l   #$E000,d5               ; T1/T0/S bits
                btst    #13,d5                  ; S bit
                beq.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#S_BIT
