;
; tests/dsp/dsp_op_abs.s - DSP ABS opcode strict result check.
;
; r0 = $FFFFFFFE (-2); ABS r0 => r0 = 2.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

D_FLAGS         equ     DSP_BASE+$00
D_PC            equ     DSP_BASE+$10
D_CTRL          equ     DSP_BASE+$14

GO              equ     $00000001
SENTINEL        equ     $A5A5A5A5
RESULT_ADDR     equ     $00080000
EXPECTED        equ     2

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     DSP_RAM.l,a0
                move.w  #$9800,(a0)+
                move.w  #$FFFE,(a0)+
                move.w  #$FFFF,(a0)+
                ;; abs r0   (op=22=$5800, reg2=r0=0)
                move.w  #$5800,(a0)+
                move.w  #$9802,(a0)+
                move.w  #(RESULT_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_ADDR>>16)&$FFFF),(a0)+
                ;; store r0,(r2)  (RN=r0, RM=r2) -> $BC40
                move.w  #$BC40,(a0)+
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL
                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin
                move.l  #0,D_CTRL

                move.l  RESULT_ADDR.l,d5
                cmp.l   #SENTINEL,d5
                beq.s   .never_wrote
                cmp.l   #EXPECTED,d5
                bne.s   .bad
                ACID_PASS
.bad:           ACID_FAIL #1,d5,#EXPECTED
.never_wrote:   ACID_FAIL #2,d5,#EXPECTED
