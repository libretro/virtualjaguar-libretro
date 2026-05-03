;
; tests/blitter/bcompen_basic.s - BCOMPEN bit-mask compositing (font path).
;
; With BCOMPEN (command bit 9 = $0200), source data is treated as a
; bit-mask: each source bit selects whether the corresponding dest
; pixel gets the pattern colour (1) or is left alone (0).  This is the
; path many games use to render bitmap fonts.
;
; Setup:
;   src bitmask byte = $A5 = 1010_0101
;   pattern data     = $11 (foreground colour, 8bpp -> repeated)
;   dest             = pre-cleared to $00
;
; Expected dest 8 bytes (MSB first across pixels):
;   $11 $00 $11 $00  $00 $11 $00 $11
;
; Command bits:
;   SRCEN  = $0001
;   PATDSEL= $00010000  (use B_PATD for the foreground colour)
;   BCOMPEN= $0200
;   LFU = doesn't really matter when BCOMPEN+PATDSEL drive output;
;         leave LFU = $C (S short-form ity = $C000) for a sane default.
; -> $0001C201
;
; A?_FLAGS for 8bpp phrase mode: pixsize=3, e=2 (8-px phrase),
; xadd=phrase=00 -> $00001018.
;
; Detail codes:
;   1 = first dest pixel mismatch (1-based byte index encoded in d3)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; Most blitter symbols come from jaguar_regs.s now.
B_PATD_HI       equ     B_PATTERNDATA
B_PATD_LO       equ     B_PATTERNDATA + 4
B_COUNT         equ     B_PIXLINECOUNTER

SRC             equ     $00080000
DST             equ     $00090000

                org     $802000
entry:
                ACID_INIT

                ;; Source bit-mask (8 bits = 8 dest pixels at 8bpp).
                ;; Byte $A5 = 10100101.
                move.l  #$A5000000,SRC.l
                move.l  #$00000000,SRC+4.l

                ;; Pre-clear dest.
                move.l  #$00000000,DST.l
                move.l  #$00000000,DST+4.l

                ;; Pattern data (foreground colour) repeated across
                ;; the 64-bit pattern phrase.  $11 in every byte.
                move.l  #$11111111,B_PATD_HI
                move.l  #$11111111,B_PATD_LO

                ;; A1 = dest, 8bpp phrase.
                move.l  #DST,B_A1_BASE
                move.l  #$00001018,B_A1_FLAGS   ; pixsize=3 (8bpp), e=2
                move.l  #0,B_A1_PIXEL
                ;; A2 = source bit-mask.
                move.l  #SRC,B_A2_BASE
                move.l  #$00001018,B_A2_FLAGS
                move.l  #0,B_A2_PIXEL

                ;; 1 line, 8 pixels.
                move.l  #$00010008,B_COUNT
                move.l  #$05800001,B_COMMAND    ; SRCEN | PATDSEL? + BCOMPEN | ity=S

                ;; Verify each of 8 dest bytes against the expected
                ;; pattern.  Walk a small table.
                lea     DST.l,a0
                lea     .expected(pc),a1
                moveq   #7,d2
                moveq   #1,d3
.cmp_loop:      move.b  (a0)+,d5
                move.b  (a1)+,d4
                cmp.b   d4,d5
                bne.s   .bad
                addq.l  #1,d3
                dbra    d2,.cmp_loop

                ACID_PASS

.bad:           ext.w   d5
                ext.l   d5
                ext.w   d4
                ext.l   d4
                ACID_FAIL d3,d5,d4

.expected:      dc.b    $11,$00,$11,$00,$00,$11,$00,$11
                even
