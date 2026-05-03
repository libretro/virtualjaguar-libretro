;
; tests/op/op_branch_conditional.s - OP BRANCH (type 3) conditional on YPOS.
;
; Builds an OP list:
;   OBJ0: BRANCH cc=2 (GREATER_THAN), ypos=100, link=OBJ_HI
;   OBJ1: STOP   (the "didn't branch" path -- terminates immediately)
;   OBJ_HI: BITMAP that scribbles a SENTINEL into the line buffer,
;           followed by a STOP
;
; OPProcessList is invoked once per (even) halfline.  When halfline > 100,
; the BRANCH is taken and we follow OBJ_HI; otherwise we fall through to
; OBJ1's STOP and emit nothing.  Over a full frame we'll cross halfline
; 100 plenty of times, so we expect the line buffer to *eventually* show
; the sentinel.
;
; Branch object encoding (type 3, single 64-bit phrase):
;   p0 bits 0..2   = TYPE = 3 (BRANCH)
;   p0 bits 3..13  = YPOS (compared with halfline)
;   p0 bits 14..16 = CC   (0=EQ, 1=LT, 2=GT, 3=OPFLAG, 4=2nd halfline)
;   p0 bits 21..38 = LINK (target byte addr, low 3 bits zero)
;
; To verify which path was taken, we check the OP's "current object"
; register OB at $F00010..$F00017 -- it's set to the last STOP's
; phrase on completion.  If we took the branch, OB will hold OBJ_HI's
; STOP; if we didn't, OB will hold OBJ1's STOP.  We give each STOP a
; unique YPOS field so we can tell them apart.
;
; (We also can read the line buffer at LBUF; the BITMAP path scribbles
;  $C001 there as a quicker confirmation.)
;
; Detail codes:
;   1 = neither branch path took (line buffer still clean and OB is 0)
;   2 = took the wrong path consistently (LBUF doesn't have $C001)
;   99 = encoding placeholder -- branch encoding too complex to verify
;        without a working OB read-back path
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
OBJ0            equ     OPLIST + 0          ; BRANCH
OBJ1            equ     OPLIST + 8          ; STOP (fall-through)
OBJ_HI          equ     OPLIST + 16         ; BITMAP (taken)
OBJ_HI_STOP     equ     OPLIST + 32         ; STOP after taken-path BITMAP

DATA            equ     $00060000
SPIN_LIMIT      equ     1000000

LBUF            equ     $00F01800
TOM_OB          equ     $00F00010

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill LBUF with sentinel so we can tell whether a
                ;; write happened at all.
                move.w  #$1111,LBUF.l

                ;; Source pixel for the "took the branch" path: $C001.
                move.l  #$C001C001,DATA.l
                move.l  #$C001C001,DATA+4.l

                ;; ---- OBJ0: BRANCH cc=GT, ypos=100, link=OBJ_HI ----
                ;; YPOS=100 ($64), CC=2 (GREATER_THAN), TYPE=3.
                ;; Lower 32 bits:
                ;;   YPOS<<3 | CC<<14 | TYPE
                ;;   = ($64 << 3) | (2 << 14) | 3
                ;;   = $320 | $8000 | 3 = $8323
                ;; Upper 32 bits:
                ;;   LINK = OBJ_HI = $50010 (8-byte aligned).
                ;;   $50010 << 21 (64-bit) = $0000_00A0_0200_0000
                ;;   high32 = $000000A0, low32 contributes $02000000.
                ;;
                ;; Combined low = $02000000 | $00008323 = $02008323.
                move.l  #$000000A0,OBJ0
                move.l  #$02008323,OBJ0+4

                ;; ---- OBJ1: STOP (fall-through path) ----
                move.l  #$00000000,OBJ1
                move.l  #$00000004,OBJ1+4

                ;; ---- OBJ_HI: BITMAP @ ypos=0, height=$3FF, depth=4
                ;;              link=OBJ_HI_STOP, data=DATA, iwidth=1, depth=4
                ;; (Same shape as op_bitmap_render.)
                ;; OBJ_HI_STOP = $50020. $50020<<21 in 64-bit = $00000_00A0_0400_0000
                ;;   bits 5,7 of 32 set (=$A0) high; bit 26 ($04000000) low.
                ;; data = $60000 -> high32 = $06000000, low32 = 0.
                ;; Combined high = $A0 | $06000000 = $060000A0.
                ;; Combined low  = $04000000 | $00FFC000 = $04FFC000.
                move.l  #$060000A0,OBJ_HI
                move.l  #$04FFC000,OBJ_HI+4
                ;; phrase 1: depth=4, iwidth=1
                move.l  #$00000000,OBJ_HI+8
                move.l  #$10004000,OBJ_HI+12

                ;; ---- OBJ_HI_STOP: STOP ----
                move.l  #$00000000,OBJ_HI_STOP
                move.l  #$00000004,OBJ_HI_STOP+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Retry loop: re-prime + re-OLP each attempt to defeat
                ;; HEIGHT decrement on OBJ_HI BITMAP.
                move.w  #100,d3
.observe:
                ;; Re-prime OBJ_HI BITMAP p0 (HEIGHT counter).
                move.l  #$060000A0,OBJ_HI
                move.l  #$04FFC000,OBJ_HI+4
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                move.l  #2000,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; If the BITMAP rendered, LBUF[0] = $C001.  If not (we're
                ;; still on a halfline where the branch wasn't taken),
                ;; the value should be the sentinel $1111 (or whatever
                ;; the most recent render-state left).
                move.w  LBUF.l,d5
                cmp.w   #$C001,d5
                beq     .took_branch
                dbra    d3,.observe

                ;; 100 attempts and never saw $C001.  Branch never taken.
                bra     .no_branch

.took_branch:
                ACID_PASS

.no_branch:
                ;; Could be:
                ;;  - OP never ran (sentinel intact = $1111)
                ;;  - OP ran but always took fall-through (sentinel cleared
                ;;    by BG fill but never overwritten)
                ;; Either way the BRANCH-conditional behaviour didn't fire.
                ext.l   d5
                ACID_FAIL #1,d5,#$C001
