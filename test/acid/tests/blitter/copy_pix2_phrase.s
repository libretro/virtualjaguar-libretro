;
; tests/blitter/copy_pix2_phrase.s - 2bpp phrase-mode copy.
;
; **DELIBERATE FAIL PLACEHOLDER**: any actual 2bpp blit (pixsize=1)
; on the accurate blitter hangs forever inside BlitterMidsummer2 --
; tested with inner counts of 4, 16, 64, and 256 pixels; all hang
; the runner indefinitely.  This is a real emulator bug surfaced by
; the acid suite.  Until it's fixed, this test reports FAIL
; immediately so the rest of the suite can complete without hanging.
;
; To turn this into a real test once the blitter bug is fixed:
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
