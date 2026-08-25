; test/microbench/cartboot.inc
;
; Shared cartridge bootstrap + sentinel convention for every #536
; microbenchmark ROM.  Include this FIRST in the .text section; the
; including source continues immediately at the cart entry point.
;
;   .68000
;   .text
;   .include "cartboot.inc"
; start:                    ; <-- lands at $802000, the cart entry
;
; Link with:  rln -n -a 800000 x x -o out.j64 out.o
;
; ---------------------------------------------------------------------
; Why this header, byte for byte
; ---------------------------------------------------------------------
; Verified against the two public ROMs already in this repo -- both
; `test/roms/yarc.j64` and `test/roms/jagniccc.j64` carry exactly these
; eight bytes at file offset $400:
;
;   00000400: 0404 0404 0080 2000    ...... .
;
;   $800400  dc.l $04040404  universal marker.  THREE separate jobs:
;              * src/core/file.c ParseFileType() accepts a sub-1 MiB
;                image as JST_ROM only if this longword is here
;                (CART_UNIVERSAL_MARKER at CART_UNIVERSAL_MARKER_OFFSET);
;              * DetectPrependedHeaderSize() uses its absence at file
;                $600 to conclude nothing is prepended;
;              * its FIRST BYTE ($04) is the cart-type byte the HLE BIOS
;                folds into MEMCON1 (jaguar.c: CART_HEADER_BASE,
;                MEMCON1_BASE | (byte & MEMCON1_CART_MASK) -> $1865),
;                so we get the same MEMCON1 a commercial cart gets.
;   $800404  dc.l $00802000  the 68K entry vector.  src/core/file.c
;              reads cart+$404, range-checks it, and stores it in
;              jaguarRunAddress; JaguarReset() copies it to main RAM $4,
;              which is where the 68K fetches its reset PC.
;   $800408  dc.l 0          matches the real ROMs; not read by anything.
;
; $800000-$8003FF is the encrypted "BootIntro" block on a retail cart.
; We fill it with zeros: the HLE boot path never looks at it.  See the
; README's "Real-BIOS mode does not work" section -- that is a measured
; limitation, not an oversight.
;
; ---------------------------------------------------------------------
; Sentinel convention -- ALL FIVE microbenchmarks use this
; ---------------------------------------------------------------------
; $010000-$01000F is the reserved sentinel block.  Benchmark working
; buffers (blitter destinations, GPU scratch) must live at $020000 or
; above so they can never stomp it.
;
; $010000 chosen over the plan's sketched $001000 because HLE_SSP_CART
; is $4000 (src/core/jaguar.c) -- the 68K stack grows down from there,
; straight through $1000.  $010000 is above the stack, above the HLE
; vector table ($0-$3FF) and its RTE stubs ($400/$404), and well inside
; the 2 MB of main RAM.

MB_SENT_BASE    .equ    $00010000
MB_SENT_START   .equ    MB_SENT_BASE+0      ; "the ROM booted" marker
MB_SENT_DONE    .equ    MB_SENT_BASE+4      ; per-engine completion magic
MB_SENT_COUNT   .equ    MB_SENT_BASE+8      ; iterations actually executed
MB_SENT_SPARE   .equ    MB_SENT_BASE+12     ; reserved
MB_WORKBUF      .equ    $00020000           ; floor for benchmark buffers

MB_MAGIC_START  .equ    $C0DE57A7           ; written by every ROM at entry

; Per-engine completion magics.  Distinct so a harness that reads a DONE
; value can say WHICH ROM produced it.
MB_MAGIC_68K    .equ    $C0DE0068
MB_MAGIC_GPUAR  .equ    $C0DE0A01           ; task 2: GPU arithmetic
MB_MAGIC_GPUBR  .equ    $C0DE0B02           ; task 3: GPU branch-heavy
MB_MAGIC_DSP    .equ    $C0DE0D53           ; task 4: DSP
MB_MAGIC_BLIT   .equ    $C0DE0B17           ; task 5: blitter

MB_CART_BASE    .equ    $00800000
MB_CART_ENTRY   .equ    $00802000

; --- the header itself ---------------------------------------------
        dcb.b   $400,0                      ; $800000: BootIntro slot, unused
        dc.l    $04040404                   ; $800400: universal marker
        dc.l    MB_CART_ENTRY               ; $800404: 68K entry vector
        dc.l    $00000000                   ; $800408
        dcb.b   $2000-$40C,$FF              ; pad to the entry point
