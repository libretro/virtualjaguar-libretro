;
; tests/gpu/gpu_op_add.s - GPU ADD opcode strict result check.
;
; Builds a small GPU program that loads two values, ADDs them, and
; stores the result to RAM where the 68K can verify it byte-for-bit.
;
; GPU program (in GPU_RAM):
;   movei #$00001000, r0
;   movei #$00002345, r1
;   add   r0, r1                  ; r1 = r0 + r1
;   movei #$00080000, r2
;   store r1, (r2)                ; *r2 = r1
;   nop                           ; spin
;
; In Jaguar GPU encoding, "add r0,r1" puts reg1=r0 (RM source) and
; reg2=r1 (RN dest+source), with result written back to r1.
;
; Detail codes:
;   1 = stored value at $00080000 doesn't match expected $00003345
;   2 = sentinel still intact -- GPU never wrote
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
EXPECTED        equ     $00003345

                org     $802000
entry:
                ACID_INIT

                ;; Pre-init result with sentinel so we can tell whether
                ;; the GPU actually wrote.
                move.l  #SENTINEL,RESULT_ADDR.l

                ;; Build GPU program at GPU_RAM.
                lea     GPU_RAM.l,a0
                ;; movei #$00001000, r0   (op=38, reg1=0=imm marker, reg2=0=r0)
                move.w  #$9800,(a0)+
                move.w  #$1000,(a0)+        ; lo
                move.w  #$0000,(a0)+        ; hi
                ;; movei #$00002345, r1   (reg2=1)
                move.w  #$9801,(a0)+
                move.w  #$2345,(a0)+
                move.w  #$0000,(a0)+
                ;; add r0, r1   (op=0=$0000, reg1=r0=0, reg2=r1=1)
                move.w  #$0001,(a0)+
                ;; movei #$00080000, r2   (reg2=2)
                move.w  #$9802,(a0)+
                move.w  #$0000,(a0)+        ; lo
                move.w  #$0008,(a0)+        ; hi
                ;; store r1, (r2)
                ;;   value source RN = r1 (reg2 field = 1)
                ;;   address RM   = r2 (reg1 field = 2)
                ;;   word = $BC00 | (2<<5) | 1 = $BC41
                move.w  #$BC41,(a0)+
                ;; jr T,-1 / nop  (infinite spin so GPU stays put)
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                ;; Start GPU.
                move.l  #0,G_FLAGS
                move.l  #GPU_RAM,G_PC
                move.l  #GO,G_CTRL

                ;; Spin so GPU gets cycles.
                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,G_CTRL

                ;; Check result.
                move.l  RESULT_ADDR.l,d5
                cmp.l   #SENTINEL,d5
                beq.s   .never_wrote
                cmp.l   #EXPECTED,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#EXPECTED
.never_wrote:   ACID_FAIL #2,d5,#EXPECTED
