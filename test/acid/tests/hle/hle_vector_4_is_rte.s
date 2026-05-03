;
; tests/hle/hle_vector_4_is_rte.s - HLE BIOS fills exception vectors with RTE.
;
; HLE init writes a single RTE handler somewhere in low memory and
; points vectors 4..255 at it.  The handler word at the destination
; address must be the 68K RTE opcode ($4E73) so a stray exception
; safely returns.
;
; Reads vector 4 (long at $00000010), follows the pointer, then reads
; the 16-bit opcode at that address.  Verifies it is $4E73.
;
; Detail codes:
;   1 = vector 4 points at zero (no handler installed)
;   2 = handler opcode is not RTE ($4E73)
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"

V4_ADDR         equ     $00000010
RTE_OPCODE      equ     $4E73

                org     $802000
entry:
                ACID_INIT

                move.l  V4_ADDR.l,d5            ; handler address
                tst.l   d5
                beq.s   .bad1

                move.l  d5,a0
                move.w  (a0),d5
                and.l   #$FFFF,d5
                cmp.l   #RTE_OPCODE,d5
                bne.s   .bad2

                ACID_PASS

.bad1:          ACID_FAIL #1,#0,#RTE_OPCODE
.bad2:          ACID_FAIL #2,d5,#RTE_OPCODE
