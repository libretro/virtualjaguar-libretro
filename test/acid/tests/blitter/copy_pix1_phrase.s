;
; tests/blitter/copy_pix1_phrase.s - 1bpp phrase-mode copy.
;
; **DELIBERATE FAIL PLACEHOLDER**: any actual 1bpp blit (pixsize=0)
; on the accurate blitter hangs forever inside BlitterMidsummer2.
; Same root cause as copy_pix2_phrase -- low pixsizes wedge the
; state machine.  Documented as a real emulator bug.
;
; To turn this into a real test once the blitter bug is fixed,
; replace the ACID_FAIL with the SRC fill / blit / verify pattern
; from copy_pix4_phrase.s (which works correctly for 4bpp).
;
; Detail codes:
;   99 = placeholder, real test pending blitter fix
;
                include "include/jaguar_header.s"
                include "include/acid_test.s"
                include "include/jaguar_regs.s"

                org     $802000
entry:
                ACID_INIT
                ACID_FAIL #99,#0,#0
