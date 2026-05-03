;
; tests/op/op_olp_alignment.s - OLP behaviour when not phrase-aligned.
;
; The OP fetches phrases at OLP, OLP+8, OLP+16, ...; OPLoadPhrase
; explicitly does `offset &= ~0x07` so a misaligned OLP is silently
; rounded down.  We verify this is graceful (no crash, no wild writes
; to RAM outside our list).
;
; Strategy:
;   - Build a STOP object at $00050000 (well-aligned).
;   - Place a SENTINEL at $00060000.
;   - Point OLP at $00050001 (one byte past start, deliberately misaligned).
;   - Run, verify SENTINEL untouched and the test didn't hang.
;
; Detail codes:
;   1 = sentinel was modified (misaligned OLP caused wild write)
;   99 = couldn't observe behaviour (test never wrote a result)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
SENTINEL        equ     $00060000
SENTINEL_VAL    equ     $A5A55A5A
SPIN_LIMIT      equ     500000

                org     $802000
entry:
                ACID_INIT

                move.l  #SENTINEL_VAL,SENTINEL.l

                ;; STOP object at OPLIST.
                move.l  #$00000000,OPLIST.l
                move.l  #$00000004,OPLIST+4.l

                ;; Misaligned OLP: $00050001 (1 byte past start).
                ;; OPLoadPhrase masks low 3 bits, so this should fetch
                ;; the same STOP phrase.  Verify that's how the emulator
                ;; behaves (graceful) and not some wild memory access.
                move.w  #(OPLIST+1)&$FFFF,TOM_OLP_LO
                move.w  #((OPLIST+1)>>16)&$FFFF,TOM_OLP_HI

                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Sentinel intact?  If yes, the misaligned-OLP path
                ;; either gracefully aligned (read STOP correctly) or
                ;; produced a no-op.  Either is acceptable on real
                ;; hardware: there's no observed game that relies on
                ;; a specific misaligned-OLP value.
                move.l  SENTINEL.l,d5
                cmp.l   #SENTINEL_VAL,d5
                bne     .clobbered

                ACID_PASS

.clobbered:     ACID_FAIL #1,d5,#SENTINEL_VAL
