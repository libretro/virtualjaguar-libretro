;
; tests/quirks/btst_dynamic.s - BTST Dn,Dn (dynamic bit number).
;
; The dynamic form `BTST Dn,Dm` tests bit (Dn mod 32) of Dm and sets
; Z to the inverted bit value (Z=0 if bit was 1, Z=1 if bit was 0).
;
; Two cases against D0 = $00000080 (only bit 7 set):
;   D1 = 7, BTST D1,D0  -> bit 7 is set   -> Z=0
;   D1 = 6, BTST D1,D0  -> bit 6 is clear -> Z=1
;
; Detail codes:
;   1 = case A (bit 7) -- BTST set Z incorrectly (expected Z=0)
;   2 = case B (bit 6) -- BTST cleared Z incorrectly (expected Z=1)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

                org     $802000
entry:
                ACID_INIT

                move.l  #$00000080,d0           ; bit 7 set, all others clear

                ;; -------- case A: BTST 7,D0 -- bit 7 IS set, Z must be 0 --------
                moveq   #7,d1
                btst    d1,d0
                ;; Z=1 means bit was zero -> incorrect for this case.
                beq     .bad_a

                ;; -------- case B: BTST 6,D0 -- bit 6 NOT set, Z must be 1 --------
                moveq   #6,d1
                btst    d1,d0
                bne     .bad_b

                ACID_PASS

.bad_a:         ;; Bit 7 was set, but BTST reported Z=1 (bit clear).
                ACID_FAIL #1,#0,#1                ; expected bit value 1 (set)
.bad_b:         ;; Bit 6 was clear, but BTST reported Z=0 (bit set).
                ACID_FAIL #2,#1,#0                ; expected bit value 0 (clear)
