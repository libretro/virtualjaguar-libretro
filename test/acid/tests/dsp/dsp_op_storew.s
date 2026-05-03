;
; tests/dsp/dsp_op_storew.s - DSP STOREW opcode strict result check.
;
; r1=$00C8DCBA; STOREW r1,(r2) writes only the low word $DCBA at the
; destination.  We aim r2 at $00080002 so the high half at $00080000
; (pre-set to $FACE) survives, giving the long $FACEDCBA.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

D_FLAGS         equ     DSP_BASE+$00
D_PC            equ     DSP_BASE+$10
D_CTRL          equ     DSP_BASE+$14

GO              equ     $00000001
RESULT_ADDR     equ     $00080000
TARGET_ADDR     equ     $00080002
EXPECTED        equ     $FACEDCBA

                org     $802000
entry:
                ACID_INIT
                move.l  #$FACEBEEF,RESULT_ADDR.l

                lea     DSP_RAM.l,a0
                ;; movei #$00C8DCBA, r1
                move.w  #$9801,(a0)+
                move.w  #$DCBA,(a0)+
                move.w  #$00C8,(a0)+
                ;; movei #TARGET_ADDR, r2
                move.w  #$9802,(a0)+
                move.w  #(TARGET_ADDR&$FFFF),(a0)+
                move.w  #((TARGET_ADDR>>16)&$FFFF),(a0)+
                ;; storew r1,(r2)  (op=46=$B800, RN=r1, RM=r2) -> $B841
                move.w  #$B841,(a0)+
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
                cmp.l   #$FACEBEEF,d5
                beq.s   .never_wrote
                cmp.l   #EXPECTED,d5
                bne.s   .bad
                ACID_PASS
.bad:           ACID_FAIL #1,d5,#EXPECTED
.never_wrote:   ACID_FAIL #2,d5,#EXPECTED
