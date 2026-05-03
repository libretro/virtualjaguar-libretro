;
; tests/gpu/gpu_op_and.s - GPU AND opcode strict result check.
;
; r0=$F0F0, r1=$FFFF; AND r0,r1 => r1 = $F0F0.
;
; Detail codes:
;   1 = wrong stored value
;   2 = sentinel intact
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

G_FLAGS         equ     GPU_BASE+$00
G_PC            equ     GPU_BASE+$10
G_CTRL          equ     GPU_BASE+$14

GO              equ     $00000001
SENTINEL        equ     $A5A5A5A5
RESULT_ADDR     equ     $00080000
EXPECTED        equ     $0000F0F0

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     GPU_RAM.l,a0
                ;; movei #$F0F0, r0
                move.w  #$9800,(a0)+
                move.w  #$F0F0,(a0)+
                move.w  #$0000,(a0)+
                ;; movei #$FFFF, r1
                move.w  #$9801,(a0)+
                move.w  #$FFFF,(a0)+
                move.w  #$0000,(a0)+
                ;; and r0, r1   (op=9=$2400, reg1=r0=0, reg2=r1=1)
                move.w  #$2401,(a0)+
                ;; movei #$00080000, r2
                move.w  #$9802,(a0)+
                move.w  #$0000,(a0)+
                move.w  #$0008,(a0)+
                ;; store r1, (r2)   (RN=r1=value, RM=r2=addr) -> $BC41
                move.w  #$BC41,(a0)+
                ;; jr T,-1 / nop spin
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,G_CTRL

                move.l  RESULT_ADDR.l,d5
                cmp.l   #SENTINEL,d5
                beq.s   .never_wrote
                cmp.l   #EXPECTED,d5
                bne.s   .bad
                ACID_PASS
.bad:           ACID_FAIL #1,d5,#EXPECTED
.never_wrote:   ACID_FAIL #2,d5,#EXPECTED
