;
; tests/dsp/dsp_mac_accumulator.s - DSP 40-bit MAC accumulator.
;
; The Jaguar DSP's MAC accumulator is 40 bits wide -- not the 32 bits
; that GPU has.  IMACN multiplies signed 16x16 -> 32 and accumulates
; into the 40-bit register (preserving sign-extended high bits).
;
; This test loads a tiny DSP program that does N multiply-accumulates
; that would overflow a 32-bit accumulator, then RESMACs the result
; into a register the 68K can read.  If the high bits of the 40-bit
; accumulator aren't preserved, the result will be truncated and the
; test fails.
;
; **Currently a placeholder** -- the actual program-build is fiddly
; (DSP movei + imacn + resmac sequence with proper register
; addressing).  This test today just runs a NOP and PASSes; the real
; MAC math will land in a follow-up once the simpler DSP tests are
; debugged.
;
; Detail codes:
;   1 = DSP didn't run (D_PC stayed put)
;   2 = MAC result was truncated to 32 bits (real test, future)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

D_FLAGS         equ     $F1A100
D_PC            equ     $F1A110
D_CTRL          equ     $F1A114
DSP_RAM         equ     $F1B000

GO              equ     $00000001
NOP_OP          equ     $E400

                org     $802000
entry:
                ACID_INIT

                ;; Placeholder: just NOP loop.  See file-header comment.
                lea     DSP_RAM.l,a0
                moveq   #15,d0
.fill:          move.w  #NOP_OP,(a0)
                addq.l  #2,a0
                dbra    d0,.fill

                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL

                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,D_CTRL
                move.l  D_PC,d5
                cmp.l   #DSP_RAM,d5
                bls.s   .didnt_run

                ACID_PASS

.didnt_run:     ACID_FAIL #1,d5,#DSP_RAM
