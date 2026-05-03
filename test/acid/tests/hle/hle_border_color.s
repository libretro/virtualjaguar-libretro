;
; tests/hle/hle_border_color.s - HLE BIOS clears TOM border-color regs.
;
; HLE init zeros the two 16-bit border-color registers at TOM_BORD1
; ($F0002A, green/red) and TOM_BORD2 ($F0002C, blue).  Verify both
; read back as zero.  (Note: $F00040/$F00042 are VBB/VBE, not the
; border-color regs -- the prompt's address was wrong.)
;
; Detail codes:
;   1 = TOM_BORD1 ($F0002A) nonzero
;   2 = TOM_BORD2 ($F0002C) nonzero
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

TOM_BORD1       equ     $F0002A
TOM_BORD2       equ     $F0002C

                org     $802000
entry:
                ACID_INIT

                move.w  TOM_BORD1.l,d5
                and.l   #$FFFF,d5
                tst.l   d5
                bne.s   .bad1

                move.w  TOM_BORD2.l,d5
                and.l   #$FFFF,d5
                tst.l   d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,d5,#0
.bad2:          ACID_FAIL #2,d5,#0
