;
; tests/quirks/m68k_set_sr_supervisor.s - 68K boots in supervisor mode
;                                          AND with the documented IPL.
;
; Per src/m68000/m68kinterface.c:m68k_pulse_reset():
;   regs.s = 1            -> SR bit 13 (S) set
;   regs.intmask = 0x07   -> SR bits 8..10 (IPL) all set
;   T1 = T0 = 0           -> SR bits 14..15 clear
;
; Strict assertion: read SR at entry, mask the architectural bits we
; care about (T1/T0/S/IPL == $E700) and require the value be exactly
; $2700.  Just checking S alone wouldn't catch a bogus IPL or a
; runaway tracebit.
;
; Detail codes:
;   1 = SR & $E700 != $2700 (S clear, IPL wrong, or T bit set)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

SR_MASK         equ     $E700           ; T1|T0|S|IPL2|IPL1|IPL0
SR_EXPECTED     equ     $2700           ; S=1, IPL=7, T=0

                org     $802000
entry:
                ACID_INIT

                move.w  sr,d5
                and.l   #SR_MASK,d5
                cmp.l   #SR_EXPECTED,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#SR_EXPECTED
