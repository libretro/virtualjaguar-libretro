;
; tests/op/op_scaled_bitmap.s - OP can navigate a scaled bitmap object.
;
; Builds a 3-phrase scaled-bitmap object (type 2) followed by a STOP
; (type 4).  We don't validate the rendered output here -- that's a
; later test once basic OP coverage is established.  This test just
; verifies:
;
;   - the OP doesn't crash / hang on a scaled bitmap object
;   - the STOP-after-scaled terminates cleanly
;   - the sentinel byte at SENTINEL is preserved (OP didn't scribble
;     wildly outside its data region)
;
; Detail codes:
;   1 = sentinel modified (OP went off-rails)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; OLP_HI / OLP_LO from oracle (LO=$F00020, HI=$F00022 per
;; src/tom/op.c:238 "LO/HI WORD" comment).

OPLIST          equ     $00050000               ; OP list
SCALED_OBJ      equ     OPLIST + 0
STOP_OBJ        equ     OPLIST + 24             ; 3 phrases past scaled
DATA            equ     $00060000               ; bitmap pixel data
SENTINEL        equ     $00070000
SENTINEL_VAL    equ     $A5A55A5A
SPIN_LIMIT      equ     500000

                org     $802000
entry:
                ACID_INIT

                move.l  #SENTINEL_VAL,SENTINEL.l

                ;; Bitmap data: 8 bytes ($A5 pattern).
                move.l  #$A5A5A5A5,DATA.l
                move.l  #$A5A5A5A5,DATA+4.l

                ;; Scaled bitmap object (type 2).
                ;; Phrase 0: ypos[13:3], height[23:14], link[42:24],
                ;;           data ptr[63:43], type[2:0]=2.
                ;; Pack:
                ;;   ypos    = 0
                ;;   height  = 1
                ;;   link    = STOP_OBJ >> 3
                ;;   data    = DATA >> 3 (to high bits)
                ;;   type    = 2
                ;;
                ;; Easiest to write the raw 64-bit phrase directly.
                ;; This is a minimal-sane configuration; on real
                ;; hardware some other fields matter, but for our
                ;; "doesn't crash" gate this is enough.
                move.l  #(DATA>>3<<11)|((STOP_OBJ>>3)&$7FFFF)<<3|2,SCALED_OBJ+4
                move.l  #(1<<14)|(0<<3),SCALED_OBJ

                ;; Phrase 1 (iwidth/dwidth/etc).  Set to mostly zero.
                move.l  #0,SCALED_OBJ+8
                move.l  #$00010001,SCALED_OBJ+12  ; some non-zero widths

                ;; Phrase 2 (hscale/vscale/remainder).  Set to 1:1 scale.
                move.l  #0,SCALED_OBJ+16
                move.l  #$00010100,SCALED_OBJ+20  ; vscale=1, hscale=1

                ;; STOP object.
                move.l  #0,STOP_OBJ
                move.l  #4,STOP_OBJ+4

                ;; Point OLP at start of list.
                move.w  #(OPLIST&$FFFF),TOM_OLP_LO
                move.w  #((OPLIST>>16)&$FFFF),TOM_OLP_HI

                ;; Run for a while; OP processes the list each halfline.
                move.l  #SPIN_LIMIT,d2
.spin:          subq.l  #1,d2
                bne.s   .spin

                ;; Sentinel intact?
                move.l  SENTINEL.l,d5
                cmp.l   #SENTINEL_VAL,d5
                bne.s   .clobbered

                ACID_PASS

.clobbered:     ACID_FAIL #1,d5,#SENTINEL_VAL
