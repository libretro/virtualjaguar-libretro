; test/microbench/benchgpu_arith.s -- 68K bootstrap for the GPU
; arithmetic-loop microbenchmark ROM (#536, task 2).
;
; The 68K's only job here is: prove the cart booted (START sentinel),
; copy the GPU program into GPU local RAM, kick GPUGO, then spin.  All of
; the measured work -- and the DONE/COUNT sentinel writes -- happens on
; the GPU (benchgpu_arith_gpu.s), which can write main RAM directly (see
; the GPU source for why: STORE to an external address is a normal GPU
; instruction, no 68K involvement needed).
;
; Build (from this directory, toolchain on PATH):
;   eval "$(tools/jaguar-toolchain/setup.sh env)"
;   ./build.sh                        # or, by hand:
;   lyxass -o benchgpu_arith_gpu.o benchgpu_arith_gpu.s
;   tail -c +13 benchgpu_arith_gpu.o > benchgpu_arith_gpu.bin   ; strip BS94 header
;   rmac -fb -o benchgpu_arith.o benchgpu_arith.s
;   rln  -n -a 800000 x x -o benchgpu_arith.j64 benchgpu_arith.o
;
; See README.md ("For tasks 2-5: embedding a lyxass GPU/DSP blob") for why
; the header must be stripped: a naive .incbin of the lyxass .o embeds a
; 12-byte BS94 header (magic + run address + code length) that the GPU
; would otherwise try to execute as instructions.
;
; See README.md and cartboot.inc for the header/entry-vector mechanics and
; the sentinel convention.

        .68000
        .text

        .include "cartboot.inc"

; --- GPU control registers ($F02100-$F0211F, docs/jtrm-register-map.md) -
G_FLAGS         .equ    $00F02100
G_PC            .equ    $00F02110
G_CTRL          .equ    $00F02114
GPU_RAM_BASE    .equ    $00F03000       ; GPU local RAM, 4 KB
GPUGO           .equ    $00000001       ; G_CTRL bit 0: start/stop GPU

start:
        ; Mask every interrupt level, same rationale as bench68k.s: the
        ; HLE BIOS leaves video/timer IRQs live, and 68K ISR cycles here
        ; would only add noise around the copy loop (the GPU is what's
        ; being measured, but a slow/interrupted copy would still delay
        ; when GPUGO fires and therefore the completion frame).
        move.w  #$2700,sr

        ; "the cart booted" -- lets a harness tell a ROM that never
        ; started from one that started and whose GPU never finished.
        move.l  #MB_MAGIC_START,MB_SENT_START

        ; Copy the GPU program (raw code, BS94 header already stripped at
        ; build time) from cart ROM into GPU local RAM, byte by byte --
        ; simplest correct thing for a ~110-byte blob; no alignment
        ; assumptions needed either direction.
        lea     gpu_code(pc),a0
        movea.l #GPU_RAM_BASE,a1
        move.w  #GPU_CODE_LEN-1,d0
copyloop:
        move.b  (a0)+,(a1)+
        dbf     d0,copyloop

        ; Clear GPU flags (no interrupt sources enabled), point G_PC at
        ; the code we just copied, then kick GPUGO.  G_PC must be written
        ; before GPUGO per docs/jtrm-register-map.md's G_CTRL bit layout.
        move.l  #0,G_FLAGS
        move.l  #GPU_RAM_BASE,G_PC
        move.l  #GPUGO,G_CTRL

halt:
        bra.s   halt

; --- embedded GPU code ------------------------------------------------
; benchgpu_arith_gpu.bin is the lyxass output for benchgpu_arith_gpu.s
; with its 12-byte BS94 header already stripped (see build.sh) -- raw
; GPU RISC code, ready to run from wherever the 68K copies it.
gpu_code:
        .incbin "benchgpu_arith_gpu.bin"
gpu_code_end:

GPU_CODE_LEN    .equ    gpu_code_end-gpu_code

        .end
