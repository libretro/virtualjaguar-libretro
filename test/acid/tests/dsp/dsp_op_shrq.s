;
; tests/dsp/dsp_op_shrq.s - DSP SHRQ opcode strict result check.
;
; r1=$10000000; SHRQ #4, r1 => r1 = $01000000.
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
EXPECTED        equ     $01000000

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     DSP_RAM.l,a0
                move.w  #$9801,(a0)+
                move.w  #$0000,(a0)+
                move.w  #$1000,(a0)+
                ;; shrq #4, r1  (op=25=$6400, IMM_1=4, reg2=r1=1) -> $6481
                move.w  #$6481,(a0)+
                move.w  #$9802,(a0)+
                move.w  #(RESULT_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_ADDR>>16)&$FFFF),(a0)+
                move.w  #$BC41,(a0)+
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
