;
; tests/quirks/bsr_l_61ff_real.s - Atari aln linker BSR.L $61FF.
;
; Real $61FF emit (no vasm pseudo-op).  PR #119 (commit 4fcf958) added
; a special case to our 68K core that interprets $61FF as a "BSR to
; absolute address" -- the 4 bytes after the opcode are the target
; address (NOT a 68020-style PC-relative displacement).
;
; Background (cpuemu.c around line 14965): the Removers/aln linker
; emits this convention.  Without our special case, games like Iron
; Soldier 2, Skyhammer, Hover Strike hard-hang in libgcc helpers.
;
; The test:
;   1. Set d6 = 0 (clear the "subroutine ran" flag)
;   2. Emit $61FF followed by absolute address of `subr`
;   3. Verify d6 = 1 after the BSR returns (subr executed, RTS'd back)
;
; Detail codes:
;   1 = subr never ran (d6 stayed 0); $61FF handler broken or absent
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d6                   ; flag = "didn't run"

                ;; Emit BSR.L $61FF + 32-bit target = subr.
                dc.w    $61FF
                dc.l    subr

                ;; Execution resumes here after subr's RTS.
                cmp.b   #1,d6
                bne.s   .never_ran

                ACID_PASS

.never_ran:     and.l   #$FF,d6
                ACID_FAIL #1,d6,#1

;; Subroutine the BSR.L $61FF should jump to.
subr:
                moveq   #1,d6
                rts
