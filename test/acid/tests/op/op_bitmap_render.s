;
; tests/op/op_bitmap_render.s - OP renders a BITMAP into the line buffer.
;
; Plants a 1-phrase 16-bpp BITMAP (type 0) source containing a known
; 4-pixel pattern at $00060000, points OLP at a list with that BITMAP
; followed by a STOP, runs the OP for several frames, then reads back
; the line buffer at $F01800 and verifies that the expected pixel
; values landed at the expected offsets.
;
; BITMAP object encoding (type 0, two 64-bit phrases):
;   p0 (bytes 0..7):
;     bits 0..2   = 000  (TYPE = BITMAP)
;     bits 3..13  = YPOS (set to 0; YPOS==0 is bumped to VDB internally,
;                  but our test just needs the halfline >= ypos check
;                  to pass repeatedly)
;     bits 14..23 = HEIGHT (number of source lines; 1 is enough)
;     bits 24..42 = LINK (bottom-3-zero byte addr; we point at STOP)
;     bits 43..63 = DATA (bottom-3-zero byte addr of source pixels >> 3)
;   p1 (bytes 8..15):
;     bits 0..10  = XPOS (signed 11-bit, 0 = leftmost line-buffer slot)
;     bits 12..14 = DEPTH (color depth: 0=1bpp, 1=2bpp, 2=4bpp,
;                  3=8bpp, 4=16bpp, 5=32bpp)
;     bits 15..17 = PITCH (source phrase pitch)
;     bits 28..37 = IWIDTH (image width in *phrases*)
;     bits 37..43 = INDEX (CLUT index for <8bpp modes)
;     bits 45..47 = FLAGS (REFLECT, RMW, TRANS)
;     bits 49..54 = FIRSTPIX
;
; In 16-bpp mode the OP writes the source phrase straight into the
; line buffer (4 pixels x 16 bits = 8 bytes per phrase).
;
; We pick:
;   YPOS=0, HEIGHT=$3FF (always render), DEPTH=4, IWIDTH=1, PITCH=0,
;   XPOS=0, FLAGS=0, INDEX=0, FIRSTPIX=0, no REFLECT.
;
; Source data (8 bytes at $00060000):
;   $1234 $5678 $9ABC $DEF0   (4 x 16-bit pixels)
;
; Expected line buffer ($F01800..$F01807) after one OP pass:
;   $1234 $5678 $9ABC $DEF0
;
; Detail codes:
;   1 = LBUF[0] mismatch
;   2 = LBUF[2] mismatch
;   3 = LBUF[4] mismatch
;   4 = LBUF[6] mismatch
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

                org     $802000
entry:
                ACID_INIT

                ;; Source pixels: $1234 $5678 $9ABC $DEF0
                move.l  #$12345678,DATA.l
                move.l  #$9ABCDEF0,DATA+4.l

                ;; ---- BITMAP object phrase 0 ----
                ;; YPOS=0, HEIGHT=$3FF, TYPE=0, LINK=STOP_OBJ ($50010),
                ;; DATA=DATA ($60000).
                ;;
                ;; OP code in op.c extracts:
                ;;   YPOS   = (p0 >> 3)  & $7FF
                ;;   HEIGHT = (p0 & $FFC000) >> 14
                ;;   LINK   = (p0 >> 21) & $3FFFF8        (byte addr, dphrase aligned)
                ;;   DATA   = (p0 >> 40) & $FFFFF8        (byte addr)
                ;;
                ;; LINK = $50010 (bits 4, 16, 18 set).  $50010 << 21 places
                ;; bits at positions 25, 37, 39 of the 64-bit phrase.
                ;;   bit 25 -> low32 $02000000
                ;;   bits 37,39 -> high32 bits 5,7 = $000000A0
                ;;
                ;; DATA = $60000 (bits 17, 18 set).  $60000 << 40 places
                ;; bits at positions 57, 58 -> high32 bits 25, 26 = $06000000.
                ;;
                ;; HEIGHT $3FF << 14 = $00FFC000 (in low32).
                ;;
                ;; Combined high = $000000A0 | $06000000 = $060000A0.
                ;; Combined low  = $02000000 | $00FFC000 = $02FFC000.
                move.l  #$060000A0,BITMAP_OBJ           ; p0 high
                move.l  #$02FFC000,BITMAP_OBJ+4         ; p0 low

                ;; ---- BITMAP object phrase 1 ----
                ;; XPOS=0, DEPTH=4 (16bpp), PITCH=0, IWIDTH=1,
                ;; INDEX=0, FLAGS=0, FIRSTPIX=0.
                ;;
                ;; bits 0..10  XPOS    = 0
                ;; bits 12..14 DEPTH   = 4 -> 4 << 12 = $4000
                ;; bits 15..17 PITCH   = 0
                ;; bits 28..37 IWIDTH  = 1 -> 1 << 28 = $10000000
                ;; bits 37..43 INDEX   = 0
                ;; bits 45..47 FLAGS   = 0
                ;; bits 49..54 FIRSTPIX= 0
                ;;
                ;; Lower 32 bits = $00004000 (DEPTH=4)
                ;; Upper 32 bits: bits 32..63 of p1.
                ;;   IWIDTH bit 28 is in lower 32 (bit 28).
                ;;   Wait: IWIDTH is bits 28..37 of p1, that crosses the
                ;;   boundary. The OP code does (p1 >> 28) & $3FF.
                ;;   For IWIDTH=1, we need bit 28 of p1 set.
                ;;   bit 28 in lower 32 is position 28 -> $10000000.
                ;; So lower 32 = $10000000 | $00004000 = $10004000
                ;; Upper 32 = $00000000.
                move.l  #$00000000,BITMAP_OBJ+8         ; p1 high
                move.l  #$10004000,BITMAP_OBJ+12        ; p1 low

                ;; ---- STOP object ----
                move.l  #$00000000,STOP_OBJ
                move.l  #$00000004,STOP_OBJ+4

                ;; Point OLP at start of list.
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Spin so OP gets many halflines to render.  After
                ;; HEIGHT=$3FF iterations the BITMAP exhausts and only
                ;; BG fill runs, clobbering LBUF to zero.  To avoid
                ;; that we re-prime p0 every iteration of the outer
                ;; observe loop -- write fresh HEIGHT/DATA, write OLP,
                ;; do a SHORT spin (one halfline-ish), then read LBUF.
                ;; If we caught the LBUF mid-render we should see our
                ;; expected pixels.

                move.w  #100,d3                 ; outer attempts
.observe:
                ;; Re-prime BITMAP_OBJ p0 (HEIGHT may have been
                ;; decremented by previous OP visits).
                move.l  #$060000A0,BITMAP_OBJ
                move.l  #$02FFC000,BITMAP_OBJ+4
                ;; Re-write OLP (also resets the BITMAP write-back
                ;; cycle on the next halfline).
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Short spin (a few halflines).
                move.l  #2000,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Snapshot LBUF[0..6] into d4..d6 fast.
                move.w  LBUF.l,d5
                cmp.w   #$1234,d5
                beq     .saw_first
                dbra    d3,.observe
                bra     .bad1

.saw_first:
                ;; Got expected pixel 0; capture all 4 pixels in two
                ;; long reads (2 pixels per long) to minimize the
                ;; window where a halfline could BG-clear the buffer.
                move.l  LBUF.l,d4               ; pixels 0..1 packed
                move.l  LBUF+4.l,d6             ; pixels 2..3 packed

                ;; Verify pixel 0 ($1234) -- upper word of d4.
                move.l  d4,d5
                swap    d5
                cmp.w   #$1234,d5
                bne     .bad1
                ;; Pixel 1 ($5678) -- lower word of d4.
                move.l  d4,d5
                cmp.w   #$5678,d5
                bne     .bad2
                ;; Pixel 2 ($9ABC) -- upper word of d6.
                move.l  d6,d5
                swap    d5
                cmp.w   #$9ABC,d5
                bne     .bad3
                ;; Pixel 3 ($DEF0) -- lower word of d6.
                move.l  d6,d5
                cmp.w   #$DEF0,d5
                bne     .bad4

                ACID_PASS

.bad1:          ext.l   d5
                ACID_FAIL #1,d5,#$1234
.bad2:          ext.l   d5
                ACID_FAIL #2,d5,#$5678
.bad3:          ext.l   d5
                ACID_FAIL #3,d5,#$9ABC
.bad4:          ext.l   d5
                ACID_FAIL #4,d5,#$DEF0
