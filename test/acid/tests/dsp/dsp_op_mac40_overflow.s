;
; tests/dsp/dsp_op_mac40_overflow.s - 40-bit DSP MAC accumulator test.
;
; The DSP MAC accumulator is 40 bits (not 32 like the GPU).  We verify
; that summing 5 IMACN products that overflow 32 bits doesn't truncate.
;
; r0 = r1 = $7FFF (signed +32767).  Each product = $7FFF * $7FFF =
; $3FFF0001.  Five accumulations:
;
;   After IMULTN  : acc = $3FFF0001
;   + IMACN #1    : acc = $7FFE0002
;   + IMACN #2    : acc = $BFFD0003   (high bit set; signed-32 negative)
;   + IMACN #3    : acc = $FFFC0004
;   + IMACN #4    : acc = $00 13FFB0005 (40-bit; low 32 = $3FFB0005,
;                                        high byte = $01)
;
; A truncating 32-bit accumulator would lose the carry and end at
; $3FFB0005 with no way to detect the overflow.  The 40-bit accumulator
; keeps the $01 high byte, readable from the DSP side via control reg
; D_BASE + $20 (sign-extended top 8 bits).
;
; The 68K can't read $F1A120 directly because JERRYReadWord routes
; only addresses < D_BASE+$20 to DSPReadWord; $20 falls through to a
; generic handler that returns 0.  So the DSP itself loads $F1A120
; after RESMAC, then stores it to RESULT_ADDR+4 where the 68K reads
; it back.  RESMAC's low 32 bits go to RESULT_ADDR+0.
;
; PASS criteria (both must hold):
;   *$00080000  == $3FFB0005  (low 32 bits via RESMAC)
;   *$00080004  == $00000001  (high 8 bits, sign-extended; from DSP load)
;
; Detail codes:
;   1 = low 32 bits wrong
;   2 = sentinel intact for low slot (DSP never wrote)
;   3 = high 8 bits wrong (40-bit accumulator was truncated to 32)
;   4 = sentinel intact for high slot (DSP never wrote slot 2)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

D_FLAGS         equ     DSP_BASE+$00
D_PC            equ     DSP_BASE+$10
D_CTRL          equ     DSP_BASE+$14
D_ACC_HIGH      equ     DSP_BASE+$20

GO              equ     $00000001
SENTINEL        equ     $A5A5A5A5
RESULT_LO_ADDR  equ     $00080000
RESULT_HI_ADDR  equ     $00080004
EXPECTED_LO     equ     $3FFB0005
EXPECTED_HI     equ     $00000001

                org     $802000
entry:
                ACID_INIT
                move.l  #SENTINEL,RESULT_LO_ADDR.l
                move.l  #SENTINEL,RESULT_HI_ADDR.l

                lea     DSP_RAM.l,a0
                ;; movei #$7FFF, r0
                move.w  #$9800,(a0)+
                move.w  #$7FFF,(a0)+
                move.w  #$0000,(a0)+
                ;; movei #$7FFF, r1
                move.w  #$9801,(a0)+
                move.w  #$7FFF,(a0)+
                move.w  #$0000,(a0)+
                ;; imultn r0, r1   (op=18=$4800) -- seed acc = r0*r1
                move.w  #$4801,(a0)+
                ;; imacn r0, r1   (op=20=$5000) x4
                move.w  #$5001,(a0)+
                move.w  #$5001,(a0)+
                move.w  #$5001,(a0)+
                move.w  #$5001,(a0)+
                ;; resmac r2   (op=19=$4C00, reg2=r2=2) -> $4C02
                move.w  #$4C02,(a0)+
                ;; movei #RESULT_LO_ADDR, r3
                move.w  #$9803,(a0)+
                move.w  #(RESULT_LO_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_LO_ADDR>>16)&$FFFF),(a0)+
                ;; store r2,(r3)  (RN=r2, RM=r3) -> $BC62
                move.w  #$BC62,(a0)+
                ;; -- now read DSP control reg D_BASE+$20 (high 8 bits of acc)
                ;; movei #D_ACC_HIGH, r4
                move.w  #$9804,(a0)+
                move.w  #(D_ACC_HIGH&$FFFF),(a0)+
                move.w  #((D_ACC_HIGH>>16)&$FFFF),(a0)+
                ;; load (r4), r5   (op=41=$A400, reg1=r4=4, reg2=r5=5) -> $A485
                move.w  #$A485,(a0)+
                ;; movei #RESULT_HI_ADDR, r6
                move.w  #$9806,(a0)+
                move.w  #(RESULT_HI_ADDR&$FFFF),(a0)+
                move.w  #((RESULT_HI_ADDR>>16)&$FFFF),(a0)+
                ;; store r5,(r6)   (RN=r5, RM=r6) -> $BC00 | (6<<5) | 5 = $BCC5
                move.w  #$BCC5,(a0)+
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

                ;; Verify low 32 bits.
                move.l  RESULT_LO_ADDR.l,d5
                cmp.l   #SENTINEL,d5
                beq     .never_wrote_lo
                cmp.l   #EXPECTED_LO,d5
                bne     .bad_lo

                ;; Verify high 8 bits.
                move.l  RESULT_HI_ADDR.l,d6
                cmp.l   #SENTINEL,d6
                beq     .never_wrote_hi
                cmp.l   #EXPECTED_HI,d6
                bne     .bad_hi

                ACID_PASS

.bad_lo:        ACID_FAIL #1,d5,#EXPECTED_LO
.never_wrote_lo: ACID_FAIL #2,d5,#EXPECTED_LO
.bad_hi:        ACID_FAIL #3,d6,#EXPECTED_HI
.never_wrote_hi: ACID_FAIL #4,d6,#EXPECTED_HI
