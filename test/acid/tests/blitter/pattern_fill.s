;
; tests/blitter/pattern_fill.s - PATDSEL fills destination from B_PATD.
;
; Programs the blitter without SRCEN, with PATDSEL set, and a known
; pattern in B_PATD.  Each phrase write should land the pattern.
;
; **KNOWN-FAIL** (root cause documented):
;
; BlitterWriteByte() in src/tom/blitter_mmio.c swaps the high and low
; halves of B_PATD on CPU writes (offset+4 / offset-4 mapping).  This
; means `move.l #X, B_PATD; move.l #Y, B_PATD+4` ends up with X in the
; *low* 32 bits and Y in the *high* 32 bits of the internal 64-bit
; pattern register, the inverse of JTRM addressing.  Phrase write to
; dest then produces dest[0..3]=Y, dest[4..7]=X -- swapped from the
; expected ordering.  The same swap applies to SRCDATA / DSTDATA / DSTZ
; / SRCZINT / SRCZFRAC, but pattern_fill is the only test that uses
; *asymmetric* halves of those registers from CPU writes, so it's the
; only one that surfaces the bug.
;
; Removing the swap is non-trivial: the Gouraud reads in
; src/tom/blitter.c (gd_c[]/gd_i[] near line 945) read PATTERNDATA in
; reverse byte order -- pixel 0 reads PATD+6, pixel 3 reads PATD+0 --
; which only produces correct per-pixel data because of the swap.  A
; coordinated fix needs to drop the swap *and* reverse the Gouraud
; index mapping, with regression coverage for games that depend on
; Gouraud (Tempest 2000, Atari Karts) before it can ship.
;
; Detail codes:
;   1 = blitter never finished, OR PATD high/low swap (current bug)
;   N = first mismatched longword (1-based, 1..2)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

;; B_A1_BASE / B_A1_FLAGS / B_A1_PIXEL / B_COMMAND / B_PATTERNDATA all
;; come from jaguar_regs.s.  Don't redefine them locally -- the oracle
;; is generated from src/tom/blitter.c and stays in sync.
B_PATD_HI       equ     B_PATTERNDATA
B_PATD_LO       equ     B_PATTERNDATA + 4
B_COUNT         equ     B_PIXLINECOUNTER

DST             equ     $00090000
PAT_HI          equ     $DEADBEEF
PAT_LO          equ     $CAFEBABE
SPIN_LIMIT      equ     1000000

                org     $802000
entry:
                ACID_INIT

                lea     DST.l,a0
                clr.l   (a0)+
                clr.l   (a0)+

                ;; Load pattern into B_PATD (64-bit; hi long then lo long).
                move.l  #PAT_HI,B_PATD_HI
                move.l  #PAT_LO,B_PATD_LO

                move.l  #DST,B_A1_BASE
                move.l  #$00001020,B_A1_FLAGS   ; 16bpp phrase
                move.l  #0,B_A1_PIXEL

                move.l  #$00010004,B_COUNT      ; 4 px = 1 phrase
                ;; Command:
                ;;   PATDSEL = bit 16 = $00010000
                ;;   No SRCEN (we're filling from pattern).
                move.l  #$00010000,B_COMMAND

                ;; Blitter is synchronous in this emulator; no wait needed.

.done:
                ;; Compare DST against pattern.
                move.l  DST.l,d5
                cmp.l   #PAT_HI,d5
                bne.s   .bad1
                move.l  DST+4.l,d5
                cmp.l   #PAT_LO,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#PAT_HI
.bad2:          ACID_FAIL #2,d5,#PAT_LO
