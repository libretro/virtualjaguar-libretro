;
; tests/gpu/gpu_op_cmpq.s - GPU CMPQ opcode strict flag check.
;
; Sets r1=5, runs CMPQ #5,r1 which should set Z=1.  We verify the Z
; flag was set by performing JUMP Z,(r4) -- if it branches we store
; the "pass" sentinel; if it falls through we store the "fail" one.
;
; Layout (offsets from GPU_RAM, in bytes):
;   $00: movei #$DEADDEAD, r0          ; 6 bytes (fail marker)
;   $06: movei #$BEEFBEEF, r3          ; 6 bytes (pass marker)
;   $0C: movei #5, r1                  ; 6 bytes
;   $12: movei #PASS_TARGET, r4        ; 6 bytes (target if Z)
;   $18: movei #$00080000, r2          ; 6 bytes (result addr)
;   $1E: cmpq #5, r1                   ; 2 bytes -> sets Z
;   $20: jump Z, (r4)                  ; 2 bytes (delayed branch)
;   $22: nop                           ; delay slot
;   ;; FAIL fallthrough path:
;   $24: store r0, (r2)                ; *result = $DEADDEAD
;   $26: jr T, $26                     ; spin (self-branch w/ delay slot)
;   $28: nop                           ; delay slot
;   ;; PASS target = GPU_RAM + $2A:
;   $2A: store r3, (r2)                ; *result = $BEEFBEEF
;   $2C: jr T, $2C                     ; spin
;   $2E: nop                           ; delay slot
;
; Detail codes:
;   1 = stored value not pass marker (CMPQ didn't set Z, or jump didn't fire)
;   2 = sentinel intact (GPU never wrote)
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
EXPECTED        equ     $BEEFBEEF

PASS_TARGET     equ     GPU_RAM+$2A

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_ADDR.l

                lea     GPU_RAM.l,a0
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
                ;; movei #$00080000, r2
                move.w  #$9802,(a0)+
                move.w  #$0000,(a0)+
                move.w  #$0008,(a0)+
                ;; cmpq #5, r1   (op=31=$7C00, IMM_1=5, reg2=r1=1) -> $7CA1
                move.w  #$7CA1,(a0)+
                ;; jump Z,(r4)   (op=52=$D000, reg1=r4=4, IMM_2=cond=Z=2) -> $D082
                move.w  #$D082,(a0)+
                ;; delay slot nop
                move.w  #$E400,(a0)+
                ;; FAIL: store r0,(r2)  (RN=r0=value, RM=r2=addr) -> $BC40
                move.w  #$BC40,(a0)+
                ;; jr T,-1  (op=53=$D400, IMM_1=-1=$1F, IMM_2=cond=0) -> $D7E0
                move.w  #$D7E0,(a0)+
                ;; delay slot nop
                move.w  #$E400,(a0)+
                ;; PASS @$2A: store r3,(r2)  (RN=r3, RM=r2) -> $BC43
                move.w  #$BC43,(a0)+
                ;; jr T,-1
                move.w  #$D7E0,(a0)+
                ;; delay slot nop
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
