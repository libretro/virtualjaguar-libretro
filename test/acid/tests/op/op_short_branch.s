;
; tests/op/op_short_branch.s - chain of BRANCH-to-next-object terminating in STOP.
;
; Builds an OP list of 4 unconditional BRANCH objects, each linking to
; the next, ending in a STOP.  After OP processes the list once, OB
; (the "current object" register at $F00010) should hold the STOP's
; phrase (lowest 3 bits = 4).
;
; Each BRANCH is encoded with cc=0 (CONDITION_EQUAL) and ypos=$7FF
; (special "always branch" sentinel per OP code:
;   case CONDITION_EQUAL:
;     if (halfline == ypos || ypos == 0x7FF) op_pointer = link;
; ).
;
; Branch object encoding (type 3, single 64-bit phrase):
;   p0 bits 0..2   = TYPE = 3
;   p0 bits 3..13  = YPOS = $7FF (always branch)
;   p0 bits 14..16 = CC   = 0 (EQUAL)
;   p0 bits 21..38 = LINK (target byte addr)
;
; Detail codes:
;   1 = OB doesn't show STOP (chain didn't reach end)
;   99 = encoding placeholder (OB read-back unreliable)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
BR0             equ     OPLIST + 0
BR1             equ     OPLIST + 8
BR2             equ     OPLIST + 16
BR3             equ     OPLIST + 24
STOP_OBJ        equ     OPLIST + 32
SPIN_LIMIT      equ     500000

TOM_OB          equ     $00F00010

;; Helper macro: build the lower-32 bits of a BRANCH p0.
;;   YPOS=$7FF, CC=0, TYPE=3.
;;   Lower = ($7FF << 3) | (0 << 14) | 3 = $3FF8 | 3 = $3FFB.
BR_LOW          equ     $00003FFB

                org     $802000
entry:
                ACID_INIT

                ;; LINK encoding: code does (p0 >> 21) & $3FFFF8.
                ;; So LINK byte addr placed at p0 << 21.  All our links live
                ;; in $50000..$50020; their high32 bits are always $A0
                ;; (bits 5,7 from positions 37,39 = bits 16,18 of the
                ;; aligned byte addr).  The low32 contribution depends on
                ;; the specific value of bits 0..15 of the byte addr after
                ;; shifting left 21 -- effectively (T & 0x7FF) << 21.
                ;;
                ;;   T=$50008: bit 3 set -> bit 24 -> low = $01000000
                ;;   T=$50010: bit 4 set -> bit 25 -> low = $02000000
                ;;   T=$50018: bits 3,4 -> bits 24,25 -> low = $03000000
                ;;   T=$50020: bit 5 set -> bit 26 -> low = $04000000

                ;; ---- BR0 -> BR1 ($50008) ----
                move.l  #$000000A0,BR0
                move.l  #BR_LOW|$01000000,BR0+4

                ;; ---- BR1 -> BR2 ($50010) ----
                move.l  #$000000A0,BR1
                move.l  #BR_LOW|$02000000,BR1+4

                ;; ---- BR2 -> BR3 ($50018) ----
                move.l  #$000000A0,BR2
                move.l  #BR_LOW|$03000000,BR2+4

                ;; ---- BR3 -> STOP_OBJ ($50020) ----
                move.l  #$000000A0,BR3
                move.l  #BR_LOW|$04000000,BR3+4

                ;; ---- STOP ----
                ;; Mark with $C0DE in upper bits so we can confirm it's
                ;; the right STOP if OB happens to capture it.
                move.l  #$C0DE0000,STOP_OBJ
                move.l  #$00000004,STOP_OBJ+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Read OB lower long ($F00014..$F00017) and check the
                ;; low 3 bits == 4 (STOP type).  OPSetCurrentObject
                ;; stores the 8 bytes of p0 at $F00010..$F00017 in
                ;; big-endian: high 32 at +$10, low 32 at +$14.
                move.l  TOM_OB+4.l,d5
                move.l  d5,d6
                and.l   #$00000007,d6
                cmp.l   #$00000004,d6
                bne     .bad

                ACID_PASS

.bad:           ACID_FAIL #1,d6,#$00000004
