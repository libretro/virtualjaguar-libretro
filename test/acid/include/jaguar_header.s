;
; jaguar_header.s - minimal Jaguar cart header.
;
; Layout:
;   $800000  ATARI tag                    ; cosmetic; emulator's HLE-BIOS
;                                          path skips signature check
;   $800404  dc.l  entry                  ; ROM-loader reads this 32-bit
;                                          word as the cart entry point
;                                          (see src/core/file.c:140
;                                          jaguarRunAddress = GET32(
;                                          jagMemSpace, 0x800404)).  HLE
;                                          BIOS init then writes that
;                                          value to the 68K reset PC
;                                          vector at $00000004 before
;                                          m68k_pulse_reset(), so the CPU
;                                          starts execution at `entry`.
;   $802000  user code begins here        ; conventional cart entry org
;
; Each test should:
;   include "include/jaguar_header.s"     ; this file
;   include "include/acid_test.s"         ; pass/fail macros
;   org     $802000
; entry:    ; <-- 68K starts execution here after reset
;     ACID_INIT
;     ; ... your test code ...
;     ACID_PASS                            ; or ACID_FAIL ...,...,...
;

                ;; ROM origin
                org     $800000

                ;; Cosmetic ATARI tag.  Real cart loader validates this
                ;; against the boot ROM's expected hash; our emulator's
                ;; HLE BIOS path skips that check entirely, so any
                ;; non-zero text works here.
                dc.b    "ATARI APPROVED DATA HEADER ATRI ",0
                ds.b    $800404-*,0

                ;; Cart entry point: a literal 32-bit big-endian address
                ;; that file.c picks up via GET32(jagMemSpace, 0x800404)
                ;; and uses as the 68K's initial PC.
                dc.l    entry

                ;; Pad to the user code area at $802000.
                ds.b    $802000-*,0
