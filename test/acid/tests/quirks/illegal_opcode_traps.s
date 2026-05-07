;
; tests/quirks/illegal_opcode_traps.s - 68K illegal-instruction handler.
;
; Many ROMs (especially ones built with newer m68k-atari-mint-gcc /
; Removers Library) emit 68020 instructions like MULS.L / DIVS.L
; that the 68000 doesn't natively understand.  Our 68K core traps
; these via IllegalOpcode and emulates a useful subset (PR #119).
;
; This test executes a 68020-only opcode (MULS.L) and verifies the
; result -- if the trap+emulate path works the result lands; if not,
; either the illegal handler crashes or returns garbage.
;
; Detail codes:
;   1 = MULS.L result wrong (trap-emulate path broken or absent)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT

                ;; MULS.L #imm,Dn is encoded as $4C3C dddd | reg-spec...
                ;; vasm 68000 syntax accepts it but warns; emit
                ;; manually to be safe:
                ;;   muls.l  d2,d3      ->  $4C03 0C00
                move.l  #100,d2
                move.l  #200,d3
                ;; Inline-encode muls.l d2,d3 (32x32 -> 32, signed).
                dc.w    $4C02,$3000             ; muls.l d2,d3 (low 32)

                cmp.l   #20000,d3
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d3,#20000
