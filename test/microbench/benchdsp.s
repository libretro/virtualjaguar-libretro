; test/microbench/benchdsp.s -- 68K bootstrap for the DSP
; arithmetic-loop microbenchmark ROM (#536, task 4).
;
; The 68K's only job here is: prove the cart booted (START sentinel),
; copy the DSP program into DSP local RAM, kick DSPGO, then spin.  All of
; the measured work -- and the DONE/COUNT sentinel writes -- happens on
; the DSP (benchdsp_dsp.s), which can write main RAM directly (same as
; the GPU: STORE to an external address is a normal DSP instruction, no
; 68K involvement needed -- the DSP shares the GPU's RISC ISA per
; CLAUDE.md's hardware-model note).
;
; Build (from this directory, toolchain on PATH):
;   eval "$(tools/jaguar-toolchain/setup.sh env)"
;   ./build.sh                        # or, by hand:
;   lyxass -o benchdsp_dsp.o benchdsp_dsp.s
;   tail -c +13 benchdsp_dsp.o > benchdsp_dsp.bin   ; strip BS94 header
;   rmac -fb -o benchdsp.o benchdsp.s
;   rln  -n -a 800000 x x -o benchdsp.j64 benchdsp.o
;
; See README.md ("For tasks 2-5: embedding a lyxass GPU/DSP blob") for why
; the header must be stripped: a naive .incbin of the lyxass .o embeds a
; 12-byte BS94 header (magic + run address + code length) that the DSP
; would otherwise try to execute as instructions.
;
; See README.md and cartboot.inc for the header/entry-vector mechanics and
; the sentinel convention.

        .68000
        .text

        .include "cartboot.inc"

; --- DSP control registers ($F1A100-$F1A120, docs/jtrm-register-map.md) -
; DSP is structurally similar to the GPU but lives inside Jerry, with its
; own control register block and 8 KB (not 4 KB) of local RAM -- NOT a
; 1:1 mirror of the GPU's $F02100/$F03000 addresses.
D_FLAGS         .equ    $00F1A100
D_PC            .equ    $00F1A110
D_CTRL          .equ    $00F1A114
DSP_RAM_BASE    .equ    $00F1B000       ; DSP local RAM, 8 KB
DSPGO           .equ    $00000001       ; D_CTRL bit 0: start/stop DSP
                                         ; (same layout as G_CTRL)

start:
        ; Mask every interrupt level, same rationale as bench68k.s: the
        ; HLE BIOS leaves video/timer IRQs live, and 68K ISR cycles here
        ; would only add noise around the copy loop (the DSP is what's
        ; being measured, but a slow/interrupted copy would still delay
        ; when DSPGO fires and therefore the completion frame).
        move.w  #$2700,sr

        ; "the cart booted" -- lets a harness tell a ROM that never
        ; started from one that started and whose DSP never finished.
        move.l  #MB_MAGIC_START,MB_SENT_START

        ; Copy the DSP program (raw code, BS94 header already stripped at
        ; build time) from cart ROM into DSP local RAM, byte by byte --
        ; simplest correct thing for a ~100-byte blob; no alignment
        ; assumptions needed either direction.
        lea     dsp_code(pc),a0
        movea.l #DSP_RAM_BASE,a1
        move.w  #DSP_CODE_LEN-1,d0
copyloop:
        move.b  (a0)+,(a1)+
        dbf     d0,copyloop

        ; Clear DSP flags (no interrupt sources enabled), point D_PC at
        ; the code we just copied, then kick DSPGO.  D_PC must be written
        ; before DSPGO per docs/jtrm-register-map.md's D_CTRL bit layout
        ; ("same layout as G_CTRL").
        move.l  #0,D_FLAGS
        move.l  #DSP_RAM_BASE,D_PC
        move.l  #DSPGO,D_CTRL

halt:
        bra.s   halt

; --- embedded DSP code ------------------------------------------------
; benchdsp_dsp.bin is the lyxass output for benchdsp_dsp.s with its
; 12-byte BS94 header already stripped (see build.sh) -- raw DSP RISC
; code, ready to run from wherever the 68K copies it.
dsp_code:
        .incbin "benchdsp_dsp.bin"
dsp_code_end:

DSP_CODE_LEN    .equ    dsp_code_end-dsp_code

        .end
