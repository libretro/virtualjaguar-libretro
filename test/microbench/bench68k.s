; test/microbench/bench68k.s -- 68K-only microbenchmark ROM (#536)
;
; Isolates the 68000 interpreter: no GPU, no DSP, no blitter, no OP list,
; no interrupts.  The whole payload is a fixed-count register loop, so the
; work is byte-identical on every run and on every host.
;
; Build (from this directory, toolchain on PATH):
;   eval "$(tools/jaguar-toolchain/setup.sh env)"
;   ./build.sh                        # or, by hand:
;   rmac -fb -o bench68k.o bench68k.s
;   rln  -n -a 800000 x x -o bench68k.j64 bench68k.o
;
; See README.md for the header/entry-vector mechanics and the sentinel
; convention; cartboot.inc carries the header bytes and the MB_* equates.
;
; --- syntax notes, learned from the tools, not assumed -----------------
;
;   * NO `.org`.  rmac refuses it in a 68K section without -fr ("Error:
;     .org permitted only in GPU/DSP/OP, 56001, 6502 and 68k (with -fr
;     switch) sections").  The load address goes on `rln -a` instead, so
;     the header below is laid out with `dcb.b` fill from offset 0 and the
;     linker places the whole thing at $800000.
;   * `-fb` (BSD object format) is what rmac's own usage text labels "use
;     this for Jaguar", and is what rln consumes.
;   * `rln -n` suppresses rln's file header, so the output is the raw
;     big-endian cart image.  rln pads the tail to an 8-byte phrase.
;   * `rln` needs the two positional `x x` placeholders (data/bss segment
;     addresses) after `-a <text>`.

        .68000
        .text

        .include "cartboot.inc"

; ITERATIONS: 1,000,000 passes of a 3-instruction body.
;
; 68000 nominal cost per pass = addq.l 8 + subq.l 8 + bne.s taken 10 = 26
; cycles, so ~26 M cycles at the Jaguar's ~13.3 MHz 68K clock ~= 1.95 s of
; emulated time.  Measured completion: frame 115 (see README).  Change
; this and you MUST re-measure the harness frame budget.
ITERATIONS      .equ    1000000

start:
        ; Mask every interrupt level.  The HLE BIOS leaves video/timer
        ; IRQs live and installs RTE stubs for them; letting them fire
        ; would put BIOS-path 68K cycles inside the measured window and
        ; make the loop's cost depend on video timing.
        move.w  #$2700,sr

        ; "I booted and reached my entry point" -- lets a harness tell a
        ; ROM that never started from one that started and never finished.
        move.l  #MB_MAGIC_START,MB_SENT_START

        moveq   #0,d0                   ; iterations completed
        move.l  #ITERATIONS,d1          ; countdown
loop:
        addq.l  #1,d0
        subq.l  #1,d1
        bne.s   loop

        ; Count first, magic second: a reader that sees DONE is then
        ; guaranteed to see a settled count as well.
        move.l  d0,MB_SENT_COUNT
        move.l  #MB_MAGIC_68K,MB_SENT_DONE

halt:
        bra.s   halt

        .end
