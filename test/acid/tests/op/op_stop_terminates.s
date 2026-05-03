;
; tests/op/op_stop_terminates.s - OP must terminate on a STOP object.
;
; Builds a minimal OP list with just a single STOP object (type 4),
; points OLP at it, lets it tick.  A STOP object writes no pixels, so
; the framebuffer-region we pre-fill with sentinels must remain
; untouched after several OP-eligible halflines elapse.
;
; Strict assertion: pre-fill an 8 KB sentinel block at $00060000 with
; alternating $A5A55A5A / $5A5AA5A5 patterns; after the spin every
; longword in that block must still match the expected pattern.  This
; catches any spurious OP-driven write -- not just a single sentinel.
;
; Detail codes:
;   1 = sentinel modified at offset (d6 contains offset)
;   observed = bad longword
;   expected = expected longword
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; OP list location (well clear of code/stack/sig)
OPLIST          equ     $00050000
SENTINEL        equ     $00060000
SENTINEL_LEN    equ     2048                    ; 2048 longs = 8 KB
SENTINEL_A      equ     $A5A55A5A
SENTINEL_B      equ     $5A5AA5A5
SPIN_LIMIT      equ     500000

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill sentinel block with alternating pattern.
                lea     SENTINEL.l,a0
                move.l  #SENTINEL_LEN-1,d0
                moveq   #0,d1                   ; parity counter
.fill:          btst    #0,d1
                bne.s   .odd
                move.l  #SENTINEL_A,(a0)+
                bra.s   .next
.odd:           move.l  #SENTINEL_B,(a0)+
.next:          addq.l  #1,d1
                dbra    d0,.fill

                ;; Build STOP object at OPLIST: low 3 bits = 4 (STOP).
                move.l  #$00000000,OPLIST.l
                move.l  #$00000004,OPLIST+4.l

                ;; Point OLP at OPLIST.
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Spin so OP gets a chance to run.
                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Verify every sentinel longword is intact.
                lea     SENTINEL.l,a0
                move.l  #SENTINEL_LEN-1,d0
                moveq   #0,d6                   ; offset counter
                moveq   #0,d1                   ; parity counter
.check:         move.l  (a0)+,d5
                btst    #0,d1
                bne.s   .checkB
                cmp.l   #SENTINEL_A,d5
                bne.s   .badA
                bra.s   .ok1
.checkB:        cmp.l   #SENTINEL_B,d5
                bne.s   .badB
.ok1:           addq.l  #4,d6
                addq.l  #1,d1
                dbra    d0,.check

                ACID_PASS

.badA:          ACID_FAIL #1,d5,#SENTINEL_A
.badB:          ACID_FAIL #1,d5,#SENTINEL_B
