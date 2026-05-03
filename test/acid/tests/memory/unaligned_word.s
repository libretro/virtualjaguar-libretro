;
; tests/memory/unaligned_word.s - 16-bit access at odd address must
; raise address error on 68000.
;
; The 68000 traps unaligned word/long accesses with an address-error
; exception (vector 3).  HLE BIOS init points vector 3 at
; HLE_EXCEPT_HANDLER which RTEs cleanly.  We install our own
; handler so we can detect that the trap fired and resume execution
; past the offending instruction.
;
; Detail codes:
;   1 = trap never fired (PC continued straight past the unaligned access)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

ADDR_ERR_VEC    equ     $0000000C       ; vector 3 (address error)
HANDLER_FIRED   equ     $00080010
;; Use an address inside main RAM that's intentionally ODD.
;; Reading a word here MUST trap on 68000.
BAD_ODD_ADDR    equ     $00080001

                org     $802000
entry:
                ACID_INIT

                ;; Pre-init the "did the trap fire" flag.
                move.l  #0,HANDLER_FIRED.l

                ;; Install our handler at vector 3.
                lea     addr_err_handler(pc),a0
                move.l  a0,ADDR_ERR_VEC.l

                ;; Force unaligned word load.  This MUST trap on real
                ;; 68000.  vasm doesn't refuse the encoding when the
                ;; address is in a register, so we stage the odd
                ;; address in a4 and dereference (a4) -- still a real
                ;; misaligned load at runtime.
                lea     BAD_ODD_ADDR,a4
                move.w  (a4),d5         ; trap to vector 3 here

                ;; Execution resumes here AFTER the trap handler RTEs.
                ;; The trap MUST have fired and bumped HANDLER_FIRED;
                ;; if it didn't, we're on a 68020+ (no address error)
                ;; or the trap path is broken.
                move.l  HANDLER_FIRED.l,d5
                tst.l   d5
                beq.s   .no_trap

                ACID_PASS

.no_trap:       ACID_FAIL #1,d5,#1

addr_err_handler:
                addq.l  #1,HANDLER_FIRED.l
                ;; Skip the offending instruction.  68000 stack frame
                ;; for address error has the return PC at SP+2; bump
                ;; it past the 2-byte `move.w (a4),d5`.
                addq.l  #2,2(sp)
                rte
