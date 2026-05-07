;
; tests/dsp/dsp_mailbox.s - DSP <-> 68K mailbox round-trip via DSP_RAM.
;
; The DSP doesn't expose a 68K-readable HIDATA register the way the
; GPU does (DSP control offset $18 is dsp_modulo on the DSP side).
; Instead the canonical 68K <-> DSP mailbox is shared DSP work RAM at
; $F1B000.  This test exercises that path:
;
;   1. 68K writes $C0DECAFE to DSP_RAM+0 (the inbox).
;   2. DSP program loads inbox, increments by 1, stores to DSP_RAM+8
;      (the outbox).  DSP_RAM+4 is left as a sanity sentinel.
;   3. 68K reads outbox, must equal $C0DECAFF.
;
; PASS = exact bit match in the outbox; the inbox value must also be
; preserved (DSP did not corrupt it on the way through).
;
; DSP program layout at DSP_RAM+$20 (first 16 bytes used as data):
;   $20: movei #INBOX_ADDR, r0
;   $26: load (r0), r1            ; r1 = inbox
;   $28: movei #1, r2
;   $2E: add r2, r1               ; r1 += 1
;   $30: movei #OUTBOX_ADDR, r3
;   $36: store r1,(r3)
;   $38: jr T,-1
;   $3A: nop
;
; Mailbox slot layout in DSP_RAM:
;   DSP_RAM+$00 .. INBOX  (68K writes; DSP reads)
;   DSP_RAM+$04 .. canary (DSP must not touch)
;   DSP_RAM+$08 .. OUTBOX (DSP writes; 68K reads)
;
; Detail codes:
;   1 = outbox doesn't equal inbox+1 (DSP didn't run the math)
;   2 = inbox sentinel got clobbered (DSP corrupted shared RAM)
;   3 = outbox sentinel intact (DSP never wrote)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

D_FLAGS         equ     DSP_BASE+$00
D_PC            equ     DSP_BASE+$10
D_CTRL          equ     DSP_BASE+$14

GO              equ     $00000001

INBOX_ADDR      equ     DSP_RAM+$00
CANARY_ADDR     equ     DSP_RAM+$04
OUTBOX_ADDR     equ     DSP_RAM+$08
PROG_ADDR       equ     DSP_RAM+$20

INBOX_VAL       equ     $C0DECAFE
EXPECTED        equ     $C0DECAFF
CANARY_VAL      equ     $5A5A5A5A
OUTBOX_SENT     equ     $A5A5A5A5

                org     $802000
entry:
                ACID_INIT

                ;; Seed mailbox.
                move.l  #INBOX_VAL,INBOX_ADDR.l
                move.l  #CANARY_VAL,CANARY_ADDR.l
                move.l  #OUTBOX_SENT,OUTBOX_ADDR.l

                ;; Build DSP program at PROG_ADDR.
                lea     PROG_ADDR.l,a0
                ;; movei #INBOX_ADDR, r0
                move.w  #$9800,(a0)+
                move.w  #(INBOX_ADDR&$FFFF),(a0)+
                move.w  #((INBOX_ADDR>>16)&$FFFF),(a0)+
                ;; load (r0), r1   (op=41=$A400, reg1=r0=0, reg2=r1=1) -> $A401
                move.w  #$A401,(a0)+
                ;; movei #1, r2
                move.w  #$9802,(a0)+
                move.w  #1,(a0)+
                move.w  #$0000,(a0)+
                ;; add r2, r1   (op=0=$0000, RM=r2=2, RN=r1=1) -> $0041
                move.w  #$0041,(a0)+
                ;; movei #OUTBOX_ADDR, r3
                move.w  #$9803,(a0)+
                move.w  #(OUTBOX_ADDR&$FFFF),(a0)+
                move.w  #((OUTBOX_ADDR>>16)&$FFFF),(a0)+
                ;; store r1,(r3)   (RN=r1, RM=r3) -> $BC00 | (3<<5) | 1 = $BC61
                move.w  #$BC61,(a0)+
                ;; jr T,-1 / nop spin
                move.w  #$D7E0,(a0)+
                move.w  #$E400,(a0)+

                ;; Start DSP at PROG_ADDR.
                move.l  #0,D_FLAGS
                move.l  #PROG_ADDR,D_PC
                move.l  #GO,D_CTRL

                move.l  #100000,d2
.spin:          nop
                subq.l  #1,d2
                bne.s   .spin

                move.l  #0,D_CTRL

                ;; Verify outbox.
                move.l  OUTBOX_ADDR.l,d5
                cmp.l   #OUTBOX_SENT,d5
                beq.s   .never_wrote
                cmp.l   #EXPECTED,d5
                bne.s   .bad

                ;; Verify canary intact.
                move.l  CANARY_ADDR.l,d6
                cmp.l   #CANARY_VAL,d6
                bne.s   .canary_bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#EXPECTED
.canary_bad:    ACID_FAIL #2,d6,#CANARY_VAL
.never_wrote:   ACID_FAIL #3,d5,#EXPECTED
