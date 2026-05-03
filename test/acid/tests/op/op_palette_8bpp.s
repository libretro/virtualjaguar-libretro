;
; tests/op/op_palette_8bpp.s - 8bpp BITMAP indexes the CRY palette.
;
; In 8bpp mode each source byte is a CLUT index; the OP looks up
; paletteRAM[index] (a 16-bit CRY/RGB value) and writes that into the
; line buffer.  paletteRAM lives at TOM tomRam8 + $400 -> $F00400.
;
; Strategy: write 4 known palette entries at $F00400 + index*2:
;   CLUT[$10] = $AAAA
;   CLUT[$11] = $BBBB
;   CLUT[$12] = $CCCC
;   CLUT[$13] = $DDDD
; Source pixels (8bpp, 8 bytes per phrase = 8 indices): $10 $11 $12 $13
;                                                       $00 $00 $00 $00.
; Expected line buffer @ XPOS=0:
;   LBUF[0] = $AAAA, LBUF[2] = $BBBB, LBUF[4] = $CCCC, LBUF[6] = $DDDD,
;   LBUF[8..14] = palette[0] (whatever that is).
;
; Detail codes:
;   1..4 = LBUF[N] mismatch
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
PALETTE         equ     $00F00400

                org     $802000
entry:
                ACID_INIT

                ;; ---- Set CLUT entries $10..$13. ----
                move.w  #$AAAA,PALETTE+($10*2).l
                move.w  #$BBBB,PALETTE+($11*2).l
                move.w  #$CCCC,PALETTE+($12*2).l
                move.w  #$DDDD,PALETTE+($13*2).l

                ;; Source phrase: 8 bytes of CLUT indices.
                ;; First 4 bytes are pixels 0..3, next 4 are pixels 4..7.
                move.l  #$10111213,DATA.l
                move.l  #$00000000,DATA+4.l

                ;; ---- BITMAP phrase 0: same as op_bitmap_render ----
                ;; high=$060000A0 (link=$50010, data=$60000), low=$02FFC000.
                move.l  #$060000A0,BITMAP_OBJ
                move.l  #$02FFC000,BITMAP_OBJ+4

                ;; ---- BITMAP phrase 1 ----
                ;; XPOS=0, DEPTH=3 (8bpp), IWIDTH=1, INDEX=0, FLAGS=0.
                ;; DEPTH=3 -> bits 12..14 = 3 -> $3000.
                ;; IWIDTH bit 28 -> $10000000.
                ;; Lower 32 = $10003000.  Upper 32 = 0.
                move.l  #$00000000,BITMAP_OBJ+8
                move.l  #$10003000,BITMAP_OBJ+12

                ;; STOP
                move.l  #$00000000,STOP_OBJ
                move.l  #$00000004,STOP_OBJ+4

                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Retry loop: HEIGHT decrements per render so we re-prime
                ;; p0 each attempt and re-write OLP, then check LBUF.
                ;; Same approach as op_bitmap_render.
                move.w  #100,d3
.observe:
                move.l  #$060000A0,BITMAP_OBJ
                move.l  #$02FFC000,BITMAP_OBJ+4
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                move.l  #2000,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                move.w  LBUF.l,d5
                cmp.w   #$AAAA,d5
                beq     .saw_first
                dbra    d3,.observe
                bra     .bad1

.saw_first:
                ;; Capture remaining 3 pixels in two longs to minimise race.
                move.l  LBUF+2.l,d4              ; pixels 1..2 packed
                move.l  LBUF+4.l,d6              ; pixels 2..3 packed (overlap ok)

                ;; Pixel 1 ($BBBB) -- upper word of d4.
                move.l  d4,d5
                swap    d5
                cmp.w   #$BBBB,d5
                bne     .bad2
                ;; Pixel 2 ($CCCC) -- lower word of d4 (also upper of d6).
                move.l  d4,d5
                cmp.w   #$CCCC,d5
                bne     .bad3
                ;; Pixel 3 ($DDDD) -- lower word of d6.
                move.l  d6,d5
                cmp.w   #$DDDD,d5
                bne     .bad4

                ACID_PASS

.bad1:          ext.l   d5
                ACID_FAIL #1,d5,#$AAAA
.bad2:          ext.l   d5
                ACID_FAIL #2,d5,#$BBBB
.bad3:          ext.l   d5
                ACID_FAIL #3,d5,#$CCCC
.bad4:          ext.l   d5
                ACID_FAIL #4,d5,#$DDDD
