;
; tests/dsp/dsp_op_cmpq.s - DSP CMPQ opcode strict flag check.
;
; r1=5; CMPQ #5,r1 sets Z=1.  We verify by JUMP Z,(r4) -- if Z is set
; we land on the pass path (stores $BEEFBEEF), otherwise fail path
; stores $DEADDEAD.
;
; Layout (offsets from DSP_RAM):
;   $00: movei #$DEADDEAD, r0
;   $06: movei #$BEEFBEEF, r3
;   $0C: movei #5, r1
;   $12: movei #PASS_TARGET, r4
;   $18: movei #RESULT_ADDR, r2
;   $1E: cmpq #5, r1
;   $20: jump Z,(r4)
;   $22: nop (delay slot)
;   $24: store r0,(r2)
;   $26: jr T,-1
;   $28: nop
;   $2A: store r3,(r2)   ; PASS target
;   $2C: jr T,-1
;   $2E: nop
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
EXPECTED        equ     $BEEFBEEF
PASS_TARGET     equ     DSP_RAM+$2A

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     DSP_RAM.l,a0
                ;; movei #$DEADDEAD, r0
                move.w  #$9800,(a0)+
                move.w  #$DEAD,(a0)+
                move.w  #$DEAD,(a0)+
                ;; movei #$BEEFBEEF, r3
                move.w  #$9803,(a0)+
                move.w  #$BEEF,(a0)+
                move.w  #$BEEF,(a0)+
                ;; movei #5, r1
                move.w  #$9801,(a0)+
                move.w  #5,(a0)+
                move.w  #$0000,(a0)+
                ;; movei #PASS_TARGET, r4
                move.w  #$9804,(a0)+
                move.w  #(PASS_TARGET&$FFFF),(a0)+
                move.w  #((PASS_TARGET>>16)&$FFFF),(a0)+
                ;; movei #RESULT_ADDR, r2
                move.w  #$9802,(a0)+
                move.w  #(RESULT_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_ADDR>>16)&$FFFF),(a0)+
                ;; cmpq #5, r1   (op=31=$7C00, IMM_1=5, reg2=r1=1) -> $7CA1
                move.w  #$7CA1,(a0)+
                ;; jump Z,(r4)   (op=52=$D000, reg1=r4=4, IMM_2=Z=2) -> $D082
                move.w  #$D082,(a0)+
                ;; delay slot nop
                move.w  #$E400,(a0)+
                ;; FAIL: store r0,(r2)  (RN=r0, RM=r2) -> $BC40
                move.w  #$BC40,(a0)+
                ;; jr T,-1 / nop spin
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+
                ;; PASS: store r3,(r2)  (RN=r3, RM=r2) -> $BC43
                move.w  #$BC43,(a0)+
                ;; jr T,-1 / nop spin
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
