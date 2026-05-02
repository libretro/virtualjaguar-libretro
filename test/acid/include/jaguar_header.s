;
; jaguar_header.s - minimal Jaguar cart header + entry vector.
;
; Layout:
;   $800000  ATARI tag           ; bypassed by emulators that skip auth
;   $800400  jump table to entry ; standard Universal Header offset
;   $802000  user code begins here
;
; The harness loads the .jag at $800000 and the BIOS jumps to $802000
; via the universal header at $800400.  This is the same layout used by
; Atari's tools and most homebrew.  Authentication is bypassed inside
; the core (the BIOS auth-loop handler in src/core/jaguar.c short-
; circuits when a cart is present), so we don't need a real cart
; signature.
;
; Each test should:
;   include "jaguar_header.s"          ; this file
;   include "acid_test.s"              ; pass/fail macros
;   org     $802000
; entry:    ; <-- BIOS jumps here
;     ACID_INIT
;     ; ... your test code ...
;     ACID_PASS                         ; or ACID_FAIL ...,...,...
;

                ;; ROM origin
                org     $800000

                ;; Skunkboard / Universal Header preamble.  Real carts
                ;; have an "ATARI" tag and licence text here that the
                ;; BIOS validates; we rely on the emulator skipping
                ;; that check, so just pad to the entry vector.
                dc.b    "ATARI APPROVED DATA HEADER ATRI ",0
                ds.b    $800400-*,0

                ;; Universal Header entry vector at $800400.
                ;; The Jaguar BIOS jumps through this to start the cart.
                jmp     entry

                ;; Pad to the user code area.
                ds.b    $802000-*,0
