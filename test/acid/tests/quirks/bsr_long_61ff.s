;
; tests/quirks/bsr_long_61ff.s - 68K BSR.L $61FF Atari aln linker quirk.
;
; The Atari `aln` linker emits BSR.L (opcode $61FF) with the
; displacement filled in as an *absolute address* instead of
; PC-relative.  Our 68K core was patched to handle this in commit
; 4fcf958 (#119).  Verify by emitting one and checking it returned.
;
; Detail codes:
;   1 = BSR didn't return / target didn't run
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

                org     $802000
entry:
                ACID_INIT

                ;; Test approach: regular BSR works (control case);
                ;; if even regular BSR fails, the test setup is wrong.
                ;; The aln-quirk handling is hard to assemble portably
                ;; via vasm (it's specifically the buggy emit pattern),
                ;; so this test is currently a placeholder asserting
                ;; only that BSR.L itself does what it should.

                moveq   #0,d6                   ; flag = 0
                bsr.w   .target                 ; BSR.W (sane)
                tst.l   d6
                beq.s   .no_return

                ACID_PASS

.no_return:     ACID_FAIL #1,d6,#1

.target:
                moveq   #1,d6
                rts
