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
; Address error is a GROUP 0 exception: it stacks 14 bytes, not the
; 6 bytes every other exception uses --
;
;     SP+ 0  special status word
;     SP+ 2  address that caused the fault (long)
;     SP+ 6  first word of the faulting instruction
;     SP+ 8  status register
;     SP+10  program counter (long)
;
; RTE only ever pops 6 bytes, so a handler must discard the extra 8
; first; "ADDQ.L #8,A7 ; RTE" is the standard idiom (see
; src/m68000/cpuextra.c:Exception and issue #138, where Pitfall: The
; Mayan Adventure needs exactly this frame).  This test used to assume
; the 6-byte layout and bump "the return PC" at SP+2 -- which is really
; the fault address -- then RTE straight off the 14-byte frame.  It
; popped the special status word as SR and the fault address as PC,
; ran away into zeroed RAM, and never wrote a signature: the suite read
; it as NOT-RUN-YET on every build.
;
; Because that failure mode was invisible, the handler now also asserts
; the frame layout in place, so a regression back to a short frame
; reports a clean FAIL instead of a runaway.
;
; Detail codes:
;   1 = trap never fired (PC continued straight past the unaligned access)
;   2 = frame's fault-address field is not the odd address we touched
;       (a short/garbled group-0 frame -- regression guard for #138)
;   3 = frame's saved SR does not have the supervisor bit set
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

                ;; Assert the group-0 frame really is the 14-byte one.
                ;; SP+2 holds the faulting access address.
                move.l  2(sp),d6
                cmp.l   #BAD_ODD_ADDR,d6
                bne     .bad_frame

                ;; SP+8 holds the saved SR; we faulted in supervisor
                ;; mode, so bit 13 must be set.
                moveq   #0,d6
                move.w  8(sp),d6
                btst    #13,d6
                beq     .bad_sr

                ;; The saved PC at SP+10 already points past the
                ;; faulting instruction, so no PC fixup is needed --
                ;; just drop the 8 extra bytes and let RTE pop the
                ;; SR/PC pair it expects.
                addq.l  #8,sp
                rte

.bad_frame:     ACID_FAIL #2,d6,#BAD_ODD_ADDR
.bad_sr:        ACID_FAIL #3,d6,#$2000
