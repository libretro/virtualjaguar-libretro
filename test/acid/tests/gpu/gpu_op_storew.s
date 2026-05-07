;
; tests/gpu/gpu_op_storew.s - GPU STOREW opcode strict result check.
;
; Loads $00C8DCBA into r1, stores the low word ($DCBA) at $00080000
; via STOREW.  68K reads back the word.
;
; STOREW writes only the low 16 bits of RN; the high half of the long
; at the destination should remain whatever was there.  We pre-init
; the long with $FACEBEEF and expect $FACEDCBA after STOREW.
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
;; STOREW will overwrite the high word at +0 (since dest is +2 from a
;; long boundary?  Actually we'll target +2 so the LOW word at +2 is
;; written, and the HIGH word at +0 stays $FACE).
TARGET_ADDR     equ     $00080002
EXPECTED        equ     $FACEDCBA

                org     $802000
entry:
                ACID_INIT
                ;; Pre-fill the destination long with a known sentinel so
                ;; we can spot a 32-bit overwrite.
                move.l  #$FACEBEEF,RESULT_ADDR.l

                lea     GPU_RAM.l,a0
                ;; movei #$00C8DCBA, r1   (low word = $DCBA, high word = $00C8)
                move.w  #$9801,(a0)+
                move.w  #$DCBA,(a0)+
                move.w  #$00C8,(a0)+
                ;; movei #TARGET_ADDR, r2
                move.w  #$9802,(a0)+
                move.w  #(TARGET_ADDR&$FFFF),(a0)+
                move.w  #((TARGET_ADDR>>16)&$FFFF),(a0)+
                ;; storew r1,(r2)
                ;;   value source RN = r1 (reg2 = 1)
                ;;   address RM   = r2 (reg1 = 2)
                ;;   word = $B800 | (2<<5) | 1 = $B841
                move.w  #$B841,(a0)+
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
                cmp.l   #$FACEBEEF,d5
                beq.s   .never_wrote
                cmp.l   #EXPECTED,d5
                bne.s   .bad
                ACID_PASS
.bad:           ACID_FAIL #1,d5,#EXPECTED
.never_wrote:   ACID_FAIL #2,d5,#EXPECTED
