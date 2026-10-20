; GT with distinct computed-Z lanes and an unaligned A2 destination.
; JTRM v8 pp.74,81-82: DSTA2 reverses the address channels; phrase
; computed Z remains attached to the destination phrase's pixel lanes.
; Source A1 starts at X=0, destination A2 at X=1. The three destination
; lanes have Z=$8000,$A000,$C000, all greater than the existing $7000.
; GT must inhibit all three writes. Lane 0 ($4000) is outside the strip.
; Visible result: green = pass, red = fail. RAM $100000 holds details.
; RGB16/BGEN colours: JTRM v8 pp.12-13. HLE/direct-cart entry required.
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

GT_DST          equ $90000
                org $802000
entry:
                move.w #$2700,sr
                ACID_INIT
                clr.l $50000.l
                move.l #4,$50004.l ; STOP object: show background only
                move.w #0,TOM_OLP_LO
                move.w #5,TOM_OLP_HI
                move.w #$06C7,$F00028.l ; VMODE: RGB16, BGEN, 4 clocks/pixel
                clr.w TOM_BG
                clr.w TOM_BORD1
                clr.w TOM_BORD2
                move.l #$11111111,GT_DST.l
                move.l #$11111111,GT_DST+4.l
                move.l #$70007000,GT_DST+8.l
                move.l #$70007000,GT_DST+12.l
                move.l #$80000,B_A1_BASE
                move.l #GT_DST,B_A2_BASE
                move.l #$1861,B_A1_FLAGS
                move.l #$1861,B_A2_FLAGS
                clr.l B_A1_PIXEL
                clr.l B_A1_FPIXEL
                move.l #1,B_A2_PIXEL
                move.l #$A000C000,B_SRCZINT ; low 32 bits at low address
                move.l #$40008000,B_SRCZINT+4
                clr.l B_SRCZFRAC
                clr.l B_SRCZFRAC+4
                clr.l B_ZINC
                move.l #$ABCDABCD,B_PATTERNDATA
                move.l #$ABCDABCD,B_PATTERNDATA+4
                move.l #$10003,B_PIXLINECOUNTER
                move.l #$00112838,B_COMMAND ; GT|GOURZ|DSTA2|PATDSEL|DSTEN|DSTENZ|DSTWRZ
                move.l #1000000,d7
.wait:
                move.l B_COMMAND,d0
                btst #0,d0
                bne.s .check
                subq.l #1,d7
                bne.s .wait
                move.w #$F800,TOM_BG
                move.w #$00FF,TOM_BORD1
                ACID_FAIL #15,d0,#1
.check:
                lea GT_DST.l,a0
                lea .expected(pc),a1
                moveq #3,d2
                moveq #1,d3
.compare:
                move.l (a0)+,d5
                move.l (a1)+,d4
                cmp.l d4,d5
                bne.s .fail
                addq.l #1,d3
                dbra d2,.compare
                move.w #$003F,TOM_BG
                move.w #$FF00,TOM_BORD1
                ACID_PASS
.fail:
                move.w #$F800,TOM_BG
                move.w #$00FF,TOM_BORD1
                ACID_FAIL d3,d5,d4
.expected:
                dc.l $11111111,$11111111,$70007000,$70007000
