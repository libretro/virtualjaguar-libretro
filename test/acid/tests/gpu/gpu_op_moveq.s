;
; tests/gpu/gpu_op_moveq.s - GPU MOVEQ opcode strict result check.
;
; MOVEQ in the Jaguar GPU is `RN = IMM_1` -- the raw 5-bit IMM_1 field
; goes into RN unsigned (no sign extension, unlike 68K MOVEQ).  So
; MOVEQ #$1F,r0 sets r0 = $0000001F, NOT $FFFFFFFF.  We pre-load r0
; with $FFFFFFFF then run MOVEQ to verify the high bits are cleared.
;
; Detail codes:
;   1 = wrong stored value (high bits not cleared, or low bits wrong)
;   2 = sentinel intact -> GPU never wrote
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
EXPECTED        equ     $0000001F

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     GPU_RAM.l,a0
                ;; movei #$FFFFFFFF, r0  (so we can detect any stale high bits)
                move.w  #$9800,(a0)+
                move.w  #$FFFF,(a0)+
                move.w  #$FFFF,(a0)+
                ;; moveq #$1F, r0   (op=35=$8C00, IMM_1=$1F, reg2=r0=0)
                ;;   word = $8C00 | ($1F<<5) | 0 = $8C00 | $3E0 = $8FE0
                move.w  #$8FE0,(a0)+
                ;; movei #RESULT_ADDR, r2
                move.w  #$9802,(a0)+
                move.w  #(RESULT_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_ADDR>>16)&$FFFF),(a0)+
                ;; store r0,(r2)   (RN=r0=value, RM=r2=addr) -> $BC40
                move.w  #$BC40,(a0)+
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
