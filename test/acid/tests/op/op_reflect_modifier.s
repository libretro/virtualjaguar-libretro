;
; tests/op/op_reflect_modifier.s - BITMAP with REFLECT flag mirrors pixels.
;
; In REFLECT mode the OP walks the source phrase L->R but writes the
; line buffer R->L (lbufDelta = -2 for 16bpp).  XPOS marks the *right*
; edge of the bitmap.
;
; Strategy: place a 4-pixel BITMAP at xpos = 7 (so 4 pixels at xpos 4..7)
; with REFLECT set.  Source pixels = $0001, $0002, $0003, $0004.
; Without REFLECT we'd see [LBUF+4..+10] = $0001 $0002 $0003 $0004.
; With REFLECT (writes from right to left), the OP starts the LBUF
; pointer at xpos*2 = 14 and decrements -- so we get LBUF[14] = $0001,
; LBUF[12] = $0002, LBUF[10] = $0003, LBUF[8] = $0004.
;
; Effectively the visible pixels at LBUF byte offsets 8..14 (low->high)
; are: $0004 $0003 $0002 $0001 -- the source mirrored.
;
; Detail codes:
;   1 = LBUF[8]  != $0004
;   2 = LBUF[10] != $0003
;   3 = LBUF[12] != $0002
;   4 = LBUF[14] != $0001
;   99 = encoding placeholder
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

OPLIST          equ     $00050000
BITMAP_OBJ      equ     OPLIST + 0
STOP_OBJ        equ     OPLIST + 16
DATA            equ     $00060000
SPIN_LIMIT      equ     500000

LBUF            equ     $00F01800

;; OPFLAG_REFLECT = 1 (bit 0 of the 3-bit flags field at p1 bits 45..47).
;; In our packed p1 layout: flags<<45.  REFLECT = 1<<45.
;; In hi half (bits 32..63 of p1): bit (45-32)=13 -> $00002000.
OPFLAG_REFLECT_HI equ   $00002000

                org     $802000
entry:
                ACID_INIT

                ;; Pre-fill region of LBUF with sentinel $EEEE.
                move.l  #$EEEEEEEE,LBUF.l
                move.l  #$EEEEEEEE,LBUF+4.l
                move.l  #$EEEEEEEE,LBUF+8.l
                move.l  #$EEEEEEEE,LBUF+12.l

                ;; Source: $0001 $0002 $0003 $0004 (4 x 16-bit pixels).
                move.l  #$00010002,DATA.l
                move.l  #$00030004,DATA+4.l

                ;; ---- BITMAP phrase 0 ----
                ;; YPOS=0, HEIGHT=$3FF, LINK=STOP_OBJ ($50010), DATA=$60000.
                ;; Same encoding as op_bitmap_render: high=$060000A0, low=$02FFC000.
                move.l  #$060000A0,BITMAP_OBJ
                move.l  #$02FFC000,BITMAP_OBJ+4

                ;; ---- BITMAP phrase 1 ----
                ;; XPOS = 7. (signed 11-bit, sign-extend so bits 0..10 = $007).
                ;; DEPTH = 4 (16bpp), IWIDTH = 1, FLAGS = REFLECT (bit 0).
                ;; FLAGS field is at p1 bits 45..47, so flags=1 -> 1<<45.
                ;;
                ;; Lower 32 bits: XPOS | (DEPTH<<12) | (IWIDTH bit at 28)
                ;;   = 7 | (4<<12) | (1<<28)
                ;;   = 7 | $4000 | $10000000 = $10004007
                ;; Upper 32 bits: REFLECT bit (1<<45) -> bit 13 of upper
                ;;   = $00002000
                move.l  #OPFLAG_REFLECT_HI,BITMAP_OBJ+8
                move.l  #$10004007,BITMAP_OBJ+12

                ;; STOP
                move.l  #$00000000,STOP_OBJ
                move.l  #$00000004,STOP_OBJ+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; XPOS=7 -> startPos = 7, lbufAddress = $1800 + 7*2 = $180E.
                ;; With REFLECT, lbufDelta = -2.  Inner loop emits 4 pixels:
                ;;   LBUF[14] = src[0] = $0001
                ;;   LBUF[12] = src[1] = $0002
                ;;   LBUF[10] = src[2] = $0003
                ;;   LBUF[8]  = src[3] = $0004
                ;;
                ;; Retry loop: re-prime + re-OLP each attempt to defeat
                ;; HEIGHT decrement.
                move.w  #100,d3
.observe:
                move.l  #$060000A0,BITMAP_OBJ
                move.l  #$02FFC000,BITMAP_OBJ+4
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                move.l  #2000,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Look for $0004 at LBUF+8 first.
                move.w  LBUF+8.l,d5
                cmp.w   #$0004,d5
                beq     .saw_first
                dbra    d3,.observe
                bra     .bad1

.saw_first:
                ;; Snapshot remaining 3 pixels in long reads.
                move.l  LBUF+8.l,d4              ; pixels 4,3 (already verified pix 3=$0004)
                move.l  LBUF+12.l,d6             ; pixels 2,1

                ;; Pixel at LBUF+10 ($0003) -- lower word of d4.
                move.l  d4,d5
                cmp.w   #$0003,d5
                bne     .bad2
                ;; Pixel at LBUF+12 ($0002) -- upper word of d6.
                move.l  d6,d5
                swap    d5
                cmp.w   #$0002,d5
                bne     .bad3
                ;; Pixel at LBUF+14 ($0001) -- lower word of d6.
                move.l  d6,d5
                cmp.w   #$0001,d5
                bne     .bad4

                ACID_PASS

.bad1:          ext.l   d5
                ACID_FAIL #1,d5,#$0004
.bad2:          ext.l   d5
                ACID_FAIL #2,d5,#$0003
.bad3:          ext.l   d5
                ACID_FAIL #3,d5,#$0002
.bad4:          ext.l   d5
                ACID_FAIL #4,d5,#$0001
