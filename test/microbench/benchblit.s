; test/microbench/benchblit.s -- blitter fill/copy-throughput microbenchmark
; ROM (#536, task 5).
;
; No GPU/DSP program needed: the blitter is a DMA engine driven entirely by
; 68K writes to its MMIO register file at $F02200-$F0229F (docs/jtrm-blitter.md;
; register offsets cross-checked against src/tom/blitter.c, the authoritative
; source -- JTRM naming drifts slightly, e.g. blitter.c's PIXLINECOUNTER is
; the JTRM's B_COUNT). This is why the shape is closer to Task 1 (bench68k.s,
; pure 68K loop) than Tasks 2-4 (68K bootstrap + separate GPU/DSP payload):
; there is no second program to embed, just register writes in a loop.
;
; The loop launches a fixed count of small, identical PATDSEL solid-fill
; blits (32x32 pixels, 8bpp) at a fixed destination, polling the B_CMD
; status read (bit 0 = IDLE) between launches before relaunching -- the
; hardware-programming idiom from docs/jtrm-blitter.md's "B_CMD Status"
; section. See the "IDLE poll" comment below for why this poll is a no-op
; in THIS emulator specifically (not a general Jaguar fact).
;
; Build (from this directory, toolchain on PATH):
;   eval "$(tools/jaguar-toolchain/setup.sh env)"
;   ./build.sh                        # or, by hand:
;   rmac -fb -o benchblit.o benchblit.s
;   rln  -n -a 800000 x x -o benchblit.j64 benchblit.o
;
; See README.md for the header/entry-vector mechanics and the sentinel
; convention; cartboot.inc carries the header bytes and the MB_* equates.

        .68000
        .text

        .include "cartboot.inc"

; --- Blitter registers ($F02200-$F0229F) -------------------------------
; Offsets verified against src/tom/blitter.c's #define block (the
; implementation, not just the JTRM prose -- that file's own comment notes
; "Blitter registers (offsets from F02200)").
BLIT_BASE       .equ    $00F02200
A1_BASE         .equ    BLIT_BASE+$00
A1_FLAGS        .equ    BLIT_BASE+$04
A1_PIXEL        .equ    BLIT_BASE+$0C
A1_FPIXEL       .equ    BLIT_BASE+$18
A1_STEP         .equ    BLIT_BASE+$10
B_COUNT         .equ    BLIT_BASE+$3C   ; blitter.c: PIXLINECOUNTER
B_PATD          .equ    BLIT_BASE+$68   ; blitter.c: PATTERNDATA
B_CMD           .equ    BLIT_BASE+$38   ; blitter.c: COMMAND

; --- Fill geometry: 32x32 pixels, 8bpp, at the benchmark work buffer ----
; A1_FLAGS bit layout (docs/jtrm-blitter.md):
;   0-1   PITCH    = 0 (contiguous phrases)
;   3-5   PIXEL    = 3 (8bpp)
;   9-14  WIDTH    = 6-bit float window width, 32 pixels -> E=5,M=00
;                    (docs/jtrm-blitter.md "Window Width Encoding": bits
;                    14-9 = 010100, i.e. $14 << 9 = $2800; the doc's own
;                    worked example confirms E=5,M=00 -> 32)
;   16-17 XADDCTL  = 1 (XADDPIX: advance X by one pixel per inner-loop step;
;                    src/tom/blitter.c's XADDPIX enum, not phrase mode --
;                    simpler addressing for a plain solid fill)
; All other bits (ZOFFS, YADDCTL, XSIGNSUB, YSIGNSUB) stay 0.
FLAGS_PITCH0    .equ    $00000000
FLAGS_PIX8BPP   .equ    (3<<3)
FLAGS_WIDTH32   .equ    ($14<<9)
FLAGS_XADDPIX   .equ    (1<<16)
A1_FLAGS_VAL    .equ    FLAGS_PITCH0|FLAGS_PIX8BPP|FLAGS_WIDTH32|FLAGS_XADDPIX

; B_CMD: PATDSEL (bit16, use B_PATD as write data -- the "Block Move (Fill)"
; mode from docs/jtrm-blitter.md's "Modes of Operation") | UPDA1 (bit9,
; step A1 to the next line each outer-loop iteration). SRCEN/DSTEN/GOURD/
; etc all clear -- this also lands the request on blitter.c's "COLLAPSED
; INNER LOOP -- PATTERN FILL" fast path (line ~2507: "PATDSEL set, SRCEN/
; SRCENX/DSTEN/DSTENZ/DSTWRZ off, no GOURD/GOURZ/SRCSHADE"), i.e. the
; intended common case for a solid fill, not an edge case.
CMD_PATDSEL     .equ    $00010000
CMD_UPDA1       .equ    $00000200
B_CMD_VAL       .equ    CMD_PATDSEL|CMD_UPDA1

; A1_STEP: per-outer-loop (per-line) step, 16.16 fixed point in the upper/
; lower halves (Y in bits 31-16, X in bits 15-0 -- src/tom/blitter.c:
; "a1_step_y = REG(A1_STEP) & 0xFFFF0000"). Y step of 1.0, X step of 0.
A1_STEP_VAL     .equ    $00010000

; B_COUNT (PIXLINECOUNTER): inner (pixels/line) in bits 0-15, outer
; (lines) in bits 16-31 -- 32x32.
B_COUNT_VAL     .equ    (32<<16)|32

; Fill colour: one byte value replicated across all 8 bytes of B_PATD.
; docs/jtrm-blitter.md gotcha #1: pattern data must repeat the pixel
; across the full phrase for a solid 8bpp fill, or only some pixels in
; the phrase get painted.
PATD_VAL        .equ    $2A2A2A2A

; ITERATIONS: 150,000 launches of a 32x32 (1024-pixel) fill.
; 150,000 x 1024 = 153,600,000 pixel writes total. Chosen (after an
; empirical pass at 4,000, which completed in 3 frames -- far too little
; runtime to be a meaningful throughput signal or to show any response to
; the timing-model core options) to land in the same tens-to-hundreds of
; frames range as the other four #536 ROMs. Measured completion: see
; README.md's frame-budget table for this ROM.
ITERATIONS      .equ    150000

start:
        ; Mask every interrupt level -- same rationale as every other
        ; #536 ROM (see bench68k.s): the HLE BIOS leaves video/timer IRQs
        ; live with RTE stubs installed, and letting them fire would put
        ; BIOS-path 68K cycles inside the measured window.
        move.w  #$2700,sr

        ; "I booted and reached my entry point."
        move.l  #MB_MAGIC_START,MB_SENT_START

        ; Fixed setup, done once: destination base, addressing flags, the
        ; per-line step, the fill colour, and the fixed 32x32 count. Real
        ; games re-issue some of these per blit because A1_PIXEL/A1_BASE
        ; often change between blits (different regions); here the
        ; destination never moves, so only A1_PIXEL needs resetting each
        ; launch (see the loop body -- A1_PIXEL walks to the far corner
        ; after each fill per docs/jtrm-blitter.md gotcha #2).
        move.l  #MB_WORKBUF,A1_BASE
        move.l  #A1_FLAGS_VAL,A1_FLAGS
        move.l  #A1_STEP_VAL,A1_STEP
        move.l  #B_COUNT_VAL,B_COUNT
        move.l  #PATD_VAL,B_PATD
        move.l  #PATD_VAL,B_PATD+4

        moveq   #0,d0                   ; blits launched
        move.l  #ITERATIONS,d1          ; countdown

blitloop:
        ; Reset A1's pixel pointer to the fill's top-left corner (0,0)
        ; before every launch -- UPDA1 walks it to the last line's start
        ; over the course of the blit, and the fractional half must be
        ; zero too or the first line's addressing would inherit stale
        ; fractional bits from a previous configuration.
        move.l  #0,A1_PIXEL             ; X.i=0 (low word), Y.i=0 (high word)
        move.l  #0,A1_FPIXEL

        ; Launch: this write (the low word, which lands on COMMAND+2/+3 --
        ; see blitter_mmio.c's BlitterWriteWord, "offset & 0xFF == 0x3A")
        ; is what dispatches the blit. In this emulator the blitter is
        ; not cycle-scheduled: BlitterWriteWord calls straight into
        ; blitter_blit()/BlitterMidsummer2() synchronously, so by the
        ; time this instruction retires the fill has already happened --
        ; there is no separate "blitter running in the background" state
        ; to wait out.
        move.l  #B_CMD_VAL,B_CMD

        ; IDLE poll: real Jaguar software polls B_CMD's bit 0 before
        ; reusing the blitter (docs/jtrm-blitter.md "B_CMD Status", read
        ; from the same $F02238 address). Included here for the same
        ; hardware-programming shape real code uses -- but per
        ; src/tom/blitter_mmio.c's BlitterReadByte (COMMAND+3 always
        ; returns $05, commented "always idle/never stopped (collision
        ; detection ignored!)"), THIS core's blitter reports idle
        ; unconditionally, consistent with the synchronous dispatch
        ; above: there is nothing left to wait for by the time the poll
        ; runs. This loop always falls through on its first read here;
        ; it is not exercising a real wait path in this emulator.
idlepoll:
        btst.b  #0,B_CMD+3
        beq.s   idlepoll

        addq.l  #1,d0
        subq.l  #1,d1
        bne.s   blitloop

        ; Count first, magic second: a reader that sees DONE is then
        ; guaranteed to see a settled count as well.
        move.l  d0,MB_SENT_COUNT
        move.l  #MB_MAGIC_BLIT,MB_SENT_DONE

halt:
        bra.s   halt

        .end
