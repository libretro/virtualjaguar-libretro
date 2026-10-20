;
; ZMODE greater-than report, AtariAge #1046. Original synthetic test.
; JTRM v8 p.74: bit 20 INHIBITS when unsigned source Z > destination Z.
; Source Z=$8000. Destination lanes=$7FFF,$8000,$8001,$FFFF: only the
; first lane must keep its old colour/Z; equal and both less lanes draw.
; Four axes: pixel/phrase, BKGWREN, GOURZ, DSTA2. Pixel cases always use
; BKGWREN, avoiding Atari's known pixel-Z comparator erratum (v8 p.139).
; Twelve cases; no source-data reads. DSTEN supplies preserved colour
; when BKGWREN keeps a rejected write cycle active. Waits for hardware
; IDLE instead of assuming the emulator's synchronous blitter.
; Detail = 16*case + 1..4 for a mismatched destination longword;
;          16*case + 15 for a blitter timeout. Observed/expected are
; reported through the standard acid signature at $100000.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

ZGT_SRC         equ     $00080000
ZGT_DST         equ     $00090000
ZGT_FLAGS       equ     $00001861 ; pitch 2 phrases, 16bpp, ZOFFS 1, width 8
ZGT_COMMAND     equ     $00110038 ; GT | PATDSEL | DSTEN | DSTENZ | DSTWRZ

                org     $802000
entry:
                ACID_INIT
                moveq   #0,d6
.next_case:
                move.l  d6,d0
                and.l   #3,d0
                cmp.l   #1,d0    ; pixel without BKGWREN: known chip erratum
                beq     .advance

                move.l  #$11111111,ZGT_DST.l
                move.l  #$11111111,ZGT_DST+4.l
                move.l  #$7FFF8000,ZGT_DST+8.l
                move.l  #$8001FFFF,ZGT_DST+12.l
                move.l  #$ABCDABCD,B_PATTERNDATA
                move.l  #$ABCDABCD,B_PATTERNDATA+4
                move.l  #$80008000,B_SRCZINT
                move.l  #$80008000,B_SRCZINT+4
                clr.l   B_SRCZFRAC
                clr.l   B_SRCZFRAC+4
                clr.l   B_ZINC
                clr.l   B_A1_PIXEL
                clr.l   B_A1_FPIXEL
                clr.l   B_A2_PIXEL
                move.l  #ZGT_FLAGS,d0
                btst    #0,d6
                beq.s   .phrase
                or.l    #$00010000,d0
.phrase:
                move.l  d0,B_A1_FLAGS
                move.l  d0,B_A2_FLAGS
                move.l  #ZGT_DST,B_A1_BASE
                move.l  #ZGT_SRC,B_A2_BASE
                move.l  #ZGT_COMMAND,d1
                btst    #1,d6
                beq.s   .no_background
                or.l    #BKGWREN,d1
.no_background:
                btst    #2,d6
                beq.s   .no_gourz
                or.l    #GOURZ,d1
.no_gourz:
                btst    #3,d6
                beq.s   .destination_ready
                move.l  #ZGT_SRC,B_A1_BASE
                move.l  #ZGT_DST,B_A2_BASE
                or.l    #DSTA2,d1
.destination_ready:
                move.l  #$00010004,B_PIXLINECOUNTER
                move.l  d1,B_COMMAND
                move.l  #1000000,d7
.wait:
                move.l  B_COMMAND,d0
                btst    #0,d0
                bne.s   .finished
                subq.l  #1,d7
                bne.s   .wait
                move.l  d6,d3
                lsl.l   #4,d3
                add.l   #15,d3
                ACID_FAIL d3,d0,#1
.finished:
                lea     ZGT_DST.l,a0
                lea     .expected(pc),a1
                moveq   #3,d2
                move.l  d6,d3
                lsl.l   #4,d3
                addq.l  #1,d3
.compare:
                move.l  (a0)+,d5
                move.l  (a1)+,d4
                cmp.l   d4,d5
                bne.s   .mismatch
                addq.l  #1,d3
                dbra    d2,.compare
.advance:
                addq.l  #1,d6
                cmp.l   #16,d6
                bne     .next_case
                ACID_PASS
.mismatch:
                ACID_FAIL d3,d5,d4
.expected:
                dc.l    $1111ABCD,$ABCDABCD,$7FFF8000,$80008000
