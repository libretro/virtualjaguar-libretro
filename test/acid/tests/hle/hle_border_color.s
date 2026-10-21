;
; HLE border-register bus readback (#1045). BORD1/BORD2 are WO, so
; reads return MEMCON1, not the zero-initialized border latch.
; Primary source: jag_sim tom/IODEC.NET Float1-5. Latch initialization
; remains covered by test_hle_bios.c; it cannot be tested by bus reads.
; Detail 1/2 identifies BORD1/BORD2, observed/expected report the words.
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_MEMCON1     equ     $F00000
TOM_BORD1       equ     $F0002A
TOM_BORD2       equ     $F0002C

                org     $802000
entry:
                ACID_INIT

                moveq   #0,d7
                move.w  TOM_MEMCON1.l,d7
                move.w  TOM_BORD1.l,d5
                and.l   #$FFFF,d5
                cmp.l   d7,d5
                bne.s   .bad1

                move.w  TOM_BORD2.l,d5
                and.l   #$FFFF,d5
                cmp.l   d7,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,d7
.bad2:          ACID_FAIL #2,d5,d7
