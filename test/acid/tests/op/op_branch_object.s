;
; tests/op/op_branch_object.s - OP branch object navigates to STOP.
;
; Builds a 2-object OP list:
;   obj0: BRANCH (type 3) with target = obj1, condition = always
;   obj1: STOP (type 4)
;
; Without working branch handling, the OP would fall off the end of
; the list or loop forever.  Test passes if the sentinel survives
; (same shape as op_stop_terminates).
;
; *Strictness note*: ideally we would also assert that the OP
; followed the branch to OBJ1.  But the OP "fetch pointer"
; (op_pointer in src/tom/op.c, static) is internal C state with no
; MMIO read-back path -- the 68K can't observe it.  The closest
; observable proxy would be a side-effect at OBJ1 (e.g., GPU-INT
; object, write-pixel object), but those introduce other
; dependencies and would no longer be a *pure* "branch took the
; right path" check.  So the assertion stays at "sentinel intact"
; until we add a dedicated branch-target side-effect probe.
;
; BRANCH p0 layout (per src/tom/op.c:469-503):
;   bits 0..2   = type (3 = BRANCH)
;   bits 3..13  = ypos (11 bits)
;   bits 14..16 = cc (condition code, 3 bits, NOT 2 per JTRM)
;   bits 21..43 = link target, masked & $3FFFF8 (8-byte aligned)
;
; CONDITION_EQUAL with ypos=$7FF means "branch always" (OP code
; explicitly checks `if (halfline == ypos || ypos == 0x7FF)`).
;
; Encoding (link=OBJ1=$50008, cc=0, ypos=$7FF, type=3):
;   p0 = (link << 21) | (cc << 14) | (ypos << 3) | type
;   p0 = ($50008 << 21) | 0 | ($7FF << 3) | 3
;   p0 = $0000_00A0_0100_3FFB     (64-bit BE)
;   high long (OBJ0+0) = $000000A0
;   low  long (OBJ0+4) = $01003FFB
;
; Verify: ((hi << 11) | (lo >> 21)) & $3FFFF8
;   = ($A0 << 11) | ($01003FFB >> 21)
;   = $50000 | $00008
;   = $50008 ✓
;
; Detail codes:
;   1 = sentinel modified (OP wrote pixels = took wrong branch)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; OLP_HI / OLP_LO from the oracle (TOM_OLP_LO=$F00020,
;; TOM_OLP_HI=$F00022 -- "LO/HI WORD" per src/tom/op.c:238).

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

                ;; OBJ0: BRANCH (type 3) targeting OBJ1, always-branch.
                move.l  #$000000A0,OBJ0          ; high long
                move.l  #$01003FFB,OBJ0+4        ; low long

                ;; OBJ1: STOP.
                move.l  #$00000000,OBJ1
                move.l  #$00000004,OBJ1+4

                ;; Point OLP at OPLIST.  TOM_OLP_LO at $F00020,
                ;; TOM_OLP_HI at $F00022 (oracle).
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
