;
; tests/blitter/copy_pix4_phrase.s - 4bpp phrase-mode copy.
;
; **DELIBERATE FAIL PLACEHOLDER**: 4bpp phrase blits with the full
; 128-pixel inner count hang BlitterMidsummer2.  Same root cause as
; copy_pix1_phrase / copy_pix2_phrase -- low-pixsize phrase blits
; wedge the state machine.  Test deferred until the blitter loop is
; fixed.  copy_pix8_phrase / copy_pix16_phrase / copy_pix32_phrase
; all PASS, so the issue is specifically with pixsize <= 2 (= 4bpp,
; 2bpp, 1bpp).
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
