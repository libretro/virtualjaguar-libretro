;
; tests/gpu/gpu_op_shlq.s - GPU SHLQ opcode strict result check.
;
; r1=$00000001; SHLQ #4, r1 => r1 = $10.
;
; SHLQ encoding quirk: the shift amount field in IMM_1 is encoded as
; (32 - shift), so shift-left-by-4 stores 28 ($1C) in reg1.
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
EXPECTED        equ     $00000010

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     GPU_RAM.l,a0
                ;; movei #1, r1
                move.w  #$9801,(a0)+
                move.w  #1,(a0)+
                move.w  #$0000,(a0)+
                ;; shlq #4, r1   (op=24=$6000, reg1=28=$1C (i.e. 32-4), reg2=r1=1)
                ;;   word = $6000 | ($1C<<5) | $01 = $6000 | $0380 | $01 = $6381
                move.w  #$6381,(a0)+
                ;; movei #$00080000, r2
                move.w  #$9802,(a0)+
                move.w  #$0000,(a0)+
                move.w  #$0008,(a0)+
                ;; store r1, (r2)   (RN=r1, RM=r2) -> $BC41
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
