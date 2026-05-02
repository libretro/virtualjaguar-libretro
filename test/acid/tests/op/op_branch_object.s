;
; tests/op/op_branch_object.s - OP branch object navigates to STOP.
;
; Builds a 2-object OP list:
;   obj0: BRANCH (type 3) with target = obj1
;   obj1: STOP (type 4)
;
; Without working branch handling, the OP would fall off the end of
; the list or loop forever.  Test passes if the sentinel survives
; (same shape as op_stop_terminates).
;
; Detail codes:
;   1 = sentinel modified (OP wrote pixels = took wrong branch)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_OLP_HI      equ     $F00020
TOM_OLP_LO      equ     $F00022

OPLIST          equ     $00050000
OBJ0            equ     OPLIST + 0
OBJ1            equ     OPLIST + 8
SENTINEL        equ     $00060000
SENTINEL_VAL    equ     $A5A55A5A
SPIN_LIMIT      equ     500000

                org     $802000
entry:
                ACID_INIT

                move.l  #SENTINEL_VAL,SENTINEL.l

                ;; OBJ0: BRANCH (type 3) targeting OBJ1.
                ;; Branch object low-3-bits = 3.  Target field
                ;; varies by exact branch encoding; for "always
                ;; branch" we'd encode condition + link target.
                ;; Simplest workable: go-to-link object.
                ;; Layout: 64 bits, type=3 in low 3 bits.
                move.l  #$00000000,OBJ0
                move.l  #(OBJ1 << 5) | 3,OBJ0+4

                ;; OBJ1: STOP.
                move.l  #$00000000,OBJ1
                move.l  #$00000004,OBJ1+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                move.l  SENTINEL.l,d5
                cmp.l   #SENTINEL_VAL,d5
                bne.s   .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d5,#SENTINEL_VAL
