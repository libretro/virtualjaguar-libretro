;
; tests/dsp/dsp_op_jump.s - DSP JUMP T,(rN) opcode strict control-flow check.
;
; JUMP T always branches.  Pass marker is stored at the target.
;
; Layout (offsets from DSP_RAM):
;   $00: movei #$DEADDEAD, r0
;   $06: movei #$CAFEBABE, r3
;   $0C: movei #PASS_TARGET, r4
;   $12: movei #RESULT_ADDR, r2
;   $18: jump T,(r4)
;   $1A: nop  (delay slot)
;   $1C: store r0,(r2)
;   $1E: jr T,-1
;   $20: nop
;   $22: store r3,(r2)   ; PASS target
;   $24: jr T,-1
;   $26: nop
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
EXPECTED        equ     $CAFEBABE
PASS_TARGET     equ     DSP_RAM+$22

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     DSP_RAM.l,a0
                move.w  #$9800,(a0)+
                move.w  #$DEAD,(a0)+
                move.w  #$DEAD,(a0)+
                move.w  #$9803,(a0)+
                move.w  #$BABE,(a0)+
                move.w  #$CAFE,(a0)+
                move.w  #$9804,(a0)+
                move.w  #(PASS_TARGET&$FFFF),(a0)+
                move.w  #((PASS_TARGET>>16)&$FFFF),(a0)+
                move.w  #$9802,(a0)+
                move.w  #(RESULT_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_ADDR>>16)&$FFFF),(a0)+
                ;; jump T,(r4)  (op=52, reg1=4, IMM_2=0) -> $D080
                move.w  #$D080,(a0)+
                ;; delay slot nop
                move.w  #$E400,(a0)+
                ;; FAIL fallthrough: store r0,(r2) -> $BC40
                move.w  #$BC40,(a0)+
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+
                ;; PASS: store r3,(r2) -> $BC43
                move.w  #$BC43,(a0)+
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                move.l  #0,D_FLAGS
                move.l  #DSP_RAM,D_PC
                move.l  #GO,D_CTRL
                move.l  #200000,d2
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
