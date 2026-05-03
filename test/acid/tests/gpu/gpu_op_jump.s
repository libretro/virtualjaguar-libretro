;
; tests/gpu/gpu_op_jump.s - GPU JUMP opcode strict control-flow check.
;
; Loads a jump target into r4, performs JUMP T,(r4) (always), and the
; target stores a marker.  The fall-through path stores a different
; marker.  68K verifies the pass marker.
;
; Layout (offsets from GPU_RAM):
;   $00: movei #$DEADDEAD, r0             ; fail marker
;   $06: movei #$CAFEBABE, r3             ; pass marker
;   $0C: movei #PASS_TARGET, r4           ; target
;   $12: movei #$00080000, r2             ; result address
;   $18: jump T,(r4)                      ; always branch (delayed)
;   $1A: nop                              ; delay slot
;   ;; FAIL fallthrough:
;   $1C: store r0,(r2)
;   $1E: jr T,-1 / nop spin
;   $20: nop
;   ;; PASS target = GPU_RAM + $22:
;   $22: store r3,(r2)
;   $24: jr T,-1
;   $26: nop
;
; Detail codes:
;   1 = stored value not pass marker -> JUMP didn't take the branch
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
EXPECTED        equ     $CAFEBABE

PASS_TARGET     equ     GPU_RAM+$22

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     GPU_RAM.l,a0
                ;; movei #$DEADDEAD, r0
                move.w  #$9800,(a0)+
                move.w  #$DEAD,(a0)+
                move.w  #$DEAD,(a0)+
                ;; movei #$CAFEBABE, r3
                move.w  #$9803,(a0)+
                move.w  #$BABE,(a0)+
                move.w  #$CAFE,(a0)+
                ;; movei #PASS_TARGET, r4
                move.w  #$9804,(a0)+
                move.w  #(PASS_TARGET&$FFFF),(a0)+
                move.w  #((PASS_TARGET>>16)&$FFFF),(a0)+
                ;; movei #$00080000, r2
                move.w  #$9802,(a0)+
                move.w  #$0000,(a0)+
                move.w  #$0008,(a0)+
                ;; jump T,(r4)   (op=52=$D000, reg1=r4=4, IMM_2=cond=0=T) -> $D080
                move.w  #$D080,(a0)+
                ;; delay slot nop
                move.w  #$E400,(a0)+
                ;; FAIL: store r0,(r2)  (RN=r0, RM=r2) -> $BC40
                move.w  #$BC40,(a0)+
                ;; jr T,-1 / nop
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+
                ;; PASS: store r3,(r2)  (RN=r3, RM=r2) -> $BC43
                move.w  #$BC43,(a0)+
                ;; jr T,-1 / nop
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                move.l  #200000,d2
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
