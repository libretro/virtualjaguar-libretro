# JTRM Blitter Reference (Distilled)

> **This is NOT a verbatim copy of the Jaguar Technical Reference Manual.**
> It is a distilled, reorganized reference synthesized from the JTRM,
> optimized for emulation developers and LLM consumption. Register
> addresses, bit layouts, and behavioral notes are sourced from the
> official Atari documentation and cross-referenced with this codebase.

---

## Architecture Overview

The Blitter is a 64-bit DMA engine in TOM that performs pixel-level operations. It has:
- Two address generators: A1 (full-featured: fractional addressing, clipping, incrementing) and A2 (simpler: no fractional, no clipping)
- A 64-bit data path with source, destination, and pattern data registers
- A Logic Function Unit (LFU) for combining source and destination
- Gouraud shading and Z-buffer hardware
- Pixel-level transparency and comparator logic

The blitter operates in an inner/outer loop pattern. The inner loop processes pixels across a line; the outer loop steps to the next line. B_COUNT register sets both counts.

Derived from: `src/tom/blitter.c` -- NOT verified against the JTRM, EXCEPT the parts below carrying their own
`Source: JTRM Rev 8 p.N` tag (PITCH, window width, B_CMD bit list, LFU, B_SRCZ1/2, B_COUNT), which were re-read
against the PDF in the #820 docs pass. The rest of the file is still unverified.

## Address Generators

### A1 (Full-featured)
- Base address: A1_BASE ($F02200) -- phrase-aligned
- Pixel pointer: A1_PIXEL ($F0220C) -- X[0-15] (with fraction), Y[16-31]
- Step: A1_STEP ($F02210) -- per-outer-loop step
- Fractional step: A1_FSTEP ($F02214) -- for sub-pixel precision (rotation, scaling)
- Fractional pixel: A1_FPIXEL ($F02218) -- current fractional position
- Increment: A1_INC ($F0221C) -- per-inner-loop increment (when UPDA1 set)
- Fractional increment: A1_FINC ($F02220)
- Clipping: A1_CLIP ($F02208) -- window clipping (when CLIP_A1 set in B_CMD)

### A2 (Simple)
- Base address: A2_BASE ($F02224)
- Pixel pointer: A2_PIXEL ($F02230) -- X[0-15], Y[16-31]
- Step: A2_STEP ($F02234) -- per-outer-loop step
- Mask: A2_MASK ($F0222C) -- address mask for texture wrapping

### A1_FLAGS ($F02204) Bit Layout

| Bits | Name | Description |
|------|------|-------------|
| 0-1 | PITCH | Distance between successive phrases of pixel data: 0=contiguous (distance 1), 1=1 phrase gap (distance 2), 2=3 phrase gaps (distance 4), 3=**2** phrase gaps (distance 3 — special case, for double-buffered Z displays / interleaved buffers) |
| 3-5 | PIXEL | Pixel size: 0=1bpp, 1=2bpp, 2=4bpp, 3=8bpp, 4=16bpp, 5=32bpp |
| 6-8 | ZOFFS | Z data offset within phrase |
| 9-14 | WIDTH | Window width in 6-bit floating point (see below) |
| 16-17 | XADDCTL | X add control: 0=add phrase width, truncate to phrase boundary (sets phrase mode), 1=add pixel size (+1), 2=add zero, 3=add the increment |
| 18 | YADDCTL | Y add control: 0=+0, 1=+1 (overridden by X control in add-increment mode) |
| 19 | XSIGNSUB | X addition is subtraction (for right-to-left; only valid with X add-pixel-size mode) |
| 20 | YSIGNSUB | Y addition is subtraction (for bottom-to-top) |

Source: JTRM Rev 8 pp.70-71 (A1 flags register: pitch 3 = 2 phrase gaps, X/Y add control, sign bits).

### Window Width Encoding (6-bit Floating Point)

The WIDTH field in A1_FLAGS/A2_FLAGS is a 6-bit float giving the window
width in pixels (JTRM v8 pp. 66, 70):
- Bits 14-11 = exponent E3..E0 (4 bits, unsigned; valid values 0-11)
- Bits 10-9 = stored mantissa M1 M0 (2 bits)
- The mantissa is effectively 3 bits: its top bit is implicit 1, with the
  binary point after it — width = (1.M1M0)₂ × 2^E

```
width_pixels = ((4 | M) << E) >> 2        (M = 2 stored bits, E = 4-bit exp)

Field layout (within A1_FLAGS):
  Bit:  14  13  12  11  10   9
        E3  E2  E1  E0  M1  M0

Examples (JTRM's own: 640 = 1.01 x 2^9 -> E=1001, M=01 -> 100101):
  E=3  M=00 (001100):  1.00 x 2^3  = 8
  E=5  M=00 (010100):  1.00 x 2^5  = 32
  E=6  M=10 (011010):  1.10 x 2^6  = 96
  E=8  M=00 (100000):  1.00 x 2^8  = 256
  E=8  M=01 (100001):  1.01 x 2^8  = 320
  E=9  M=01 (100101):  1.01 x 2^9  = 640
  E=10 M=00 (101000):  1.00 x 2^10 = 1024
```

Only widths of the form (4+M)×2^(E-2) — i.e. 1, 1.25, 1.5, or 1.75 times a
power of two — are representable, and the width must give a whole number of
phrases in the current pixel size.

Source: JTRM v8 pp. 66, 70-71 (`docs/atari-jaguar-1999/Technical Reference
v8.pdf`); implementation `src/tom/blitter.c` (fast path `((0x04|m)<<e)>>2`,
accurate path `addrgen_ya`).

## B_CMD Command Register ($F02238)

Writing B_CMD starts a blitter operation. 31 control bits:

| Bit | Name | Description |
|-----|------|-------------|
| 0 | SRCEN | Read source data from A2 (or A1 if DSTA2) |
| 1 | SRCENZ | Read source Z data |
| 2 | SRCENX | Source extra data read |
| 3 | DSTEN | Read destination data from A1 (or A2 if DSTA2) |
| 4 | DSTENZ | Read destination Z data |
| 5 | DSTWRZ | Write Z data to destination |
| 6 | CLIP_A1 | Enable A1 window clipping |
| 7 | -- | Reserved |
| 8 | UPDA1F | Update A1 fractional pointer each outer loop |
| 9 | UPDA1 | Update A1 pointer each outer loop |
| 10 | UPDA2 | Update A2 pointer each outer loop |
| 11 | DSTA2 | A2 is destination (normally A1 is dest) |
| 12 | GOURD | Enable Gouraud shading |
| 13 | GOURZ | Enable polygon (computed) Z updates within the inner loop; the Z comparator itself is ZMODE (bits 18-20) |
| 14 | TOPBEN | Enable carry into the top byte of the intensity integers in Gouraud updates (leave clear for CRY) |
| 15 | TOPNEN | Enable carry into the top nibble of the intensity integers in Gouraud updates (leave clear for CRY) |
| 16 | PATDSEL | Use pattern data (B_PATD) instead of LFU output |
| 17 | ADDDSEL | Add source and destination pixel values |
| 18-20 | ZMODE | Z comparator inhibit conditions, ORed: bit 0 = source < dest, bit 1 = source = dest, bit 2 = source > dest; 0 disables the comparator; 16bpp only |
| 21-24 | LFU | Logic Function Unit control: OR of minterms (bit 21 ~S&~D, 22 ~S&D, 23 S&~D, 24 S&D) |
| 25 | CMPDST | Compare with destination (else source) for transparency |
| 26 | BCOMPEN | Bit comparator enable (write inhibit on the bit-comparator output; whole phrases only for 8bpp) |
| 27 | DCOMPEN | Data comparator enable (pixel-level transparency) |
| 28 | BKGWREN | Allow writes to background (transparent) pixels |
| 29 | BUSHI | High bus priority (moves blitter up in priority chain) |
| 30 | SRCSHADE | Apply Gouraud shading to source data (hardware needs GOURZ set too: TOM #9) |

Source: JTRM Rev 8 pp.73-75 (Command Register, bits 0-30; bits 14/15 are carry enables, not transparency;
ZMODE is a 3-bit OR of conditions, not an enumerated compare). Bit positions agree with `src/tom/blitter.c`
(`UPDA1 0x200`, `Z_OP_INF 0x40000`, `LFU_NAN 0x200000`, `BCOMPEN 0x4000000`, ...).

### B_CMD Status (read from same address $F02238)

When read, returns blitter status:
- Bit 0: IDLE (1 = blitter is completely idle and its last bus transaction completed)
- Bit 1: STOPPED (stopped in collision-detection mode)
- Bits 2-15: Internal state machine status (diagnostic only)
- Bits 16-31: inner count (diagnostic only)

Source: JTRM Rev 8 p.75 "Status Register".

## Logic Function Unit (LFU)

The LFU output is the Boolean OR of four minterms of Source (S) and Destination (D), selected by B_CMD bits
21-24 (LFU[0] = bit 21 ... LFU[3] = bit 24):

| LFU bit | B_CMD bit | Minterm |
|---------|-----------|---------|
| 0 | 21 | ~S & ~D |
| 1 | 22 | ~S & D |
| 2 | 23 | S & ~D |
| 3 | 24 | S & D |

Source: JTRM Rev 8 p.74 (LFUFUNC). Code agrees: `src/tom/blitter.c` `LFU_NAN/LFU_NA/LFU_AN/LFU_A` =
bits 21/22/23/24; `test/acid/include/jaguar_regs.s` `LFU_FN_x`.

| LFU[3:0] | S=0,D=0 | S=0,D=1 | S=1,D=0 | S=1,D=1 | Function |
|----------|---------|---------|---------|---------|----------|
| 0000 | 0 | 0 | 0 | 0 | ZERO |
| 0001 | 1 | 0 | 0 | 0 | ~S & ~D (NOR) |
| 0010 | 0 | 1 | 0 | 0 | ~S & D |
| 0011 | 1 | 1 | 0 | 0 | ~S |
| 0100 | 0 | 0 | 1 | 0 | S & ~D |
| 0101 | 1 | 0 | 1 | 0 | ~D |
| 0110 | 0 | 1 | 1 | 0 | S ^ D (XOR) |
| 0111 | 1 | 1 | 1 | 0 | ~(S & D) (NAND) |
| 1000 | 0 | 0 | 0 | 1 | S & D (AND) |
| 1001 | 1 | 0 | 0 | 1 | ~(S ^ D) (XNOR) |
| 1010 | 0 | 1 | 0 | 1 | D (destination unchanged) |
| 1011 | 1 | 1 | 0 | 1 | ~S \| D |
| 1100 | 0 | 0 | 1 | 1 | S (REPLACE / copy source) |
| 1101 | 1 | 0 | 1 | 1 | S \| ~D |
| 1110 | 0 | 1 | 1 | 1 | S \| D (OR) |
| 1111 | 1 | 1 | 1 | 1 | ONE |

In B_CMD encoding the function nibble is shifted left by 21:
- LFU_REPLACE (S copy) = 0b1100 << 21 = $01800000
- LFU_OR = 0b1110 << 21 = $01C00000
- LFU_XOR = 0b0110 << 21 = $00C00000
- LFU_AND = 0b1000 << 21 = $01000000
- LFU_NOTS (~S) = 0b0011 << 21 = $00600000
- LFU_ZERO = 0b0000 << 21 = $00000000

An earlier version of this section gave LFU_REPLACE = $00600000 (that is ~S) and LFU_AND = $01800000 (that is a
copy of S), and a 12-row table that was not the p.74 encoding.

## Modes of Operation

### Block Move (Fill)
Simplest mode. Uses PATDSEL to fill a rectangle with a constant pattern.
- Set B_PATD with fill colour
- Set A1 as destination
- Set B_COUNT (inner=width, outer=height)
- B_CMD = PATDSEL | UPDA1 (+ appropriate addressing)

### Rectangle Copy (Blit)
Copy rectangular region from source to destination.
- A2 = source, A1 = destination (or reverse with DSTA2)
- B_CMD = SRCEN | LFU_REPLACE | UPDA1 | UPDA2
- Set appropriate widths, steps, pixel formats

### Character Painting (1bpp Font Rendering)
Source is 1bpp glyph data, destination gets painted with pattern colour where source bit=1.
- BCOMPEN: treats source as 1bpp bitmask
- PATDSEL: painted colour comes from B_PATD
- B_CMD = SRCEN | BCOMPEN | PATDSEL | UPDA1 | UPDA2

### Scaled/Rotated Blit
Uses A1 fractional addressing for sub-pixel precision.
- A1_FSTEP and A1_FINC provide fractional X/Y increments
- Enable UPDA1F in B_CMD for fractional updates
- Useful for sprite scaling, rotation effects

### Gouraud Shading
Per-pixel intensity interpolation for smooth shading.
- GOURD bit in B_CMD
- B_I0-B_I3: Corner intensities (16.16 fixed point)
- B_IINC: Intensity increment per pixel
- Works with CRY colour space (intensity is the "C" component)

### Z-Buffered Rendering
Per-pixel depth comparison.
- GOURZ bit in B_CMD (computed-Z updates), ZMODE enables/selects the comparison
- ZMODE[18:20] in B_CMD selects the inhibit conditions (bit 0 src<dst, bit 1 src=dst, bit 2 src>dst, ORed)
- B_Z0-B_Z3: Corner Z values
- B_ZINC: Z increment per pixel
- B_SRCZ1/B_SRCZ2: Source Z / computed Z: SRCZ1 = the four integer parts, SRCZ2 = the four fractional parts (JTRM Rev 8 p.76)
- DSTWRZ: write Z to destination Z buffer
- DSTENZ: read destination Z for comparison

Gouraud + Z-buffer example (from JTRM):
1. Set A1 = colour buffer, A2 = source (or pattern for solid polygons)
2. Set Z buffer in a separate pass or use interleaved Z
3. Enable GOURD | GOURZ | DSTWRZ
4. Set intensities and Z values at corners, increments per pixel

## Data Registers

| Address | Name | Size | Description |
|---------|------|------|-------------|
| $F02240 | B_SRCD | 64-bit | Source data (2 x 32-bit writes) |
| $F02248 | B_DSTD | 64-bit | Destination data |
| $F02250 | B_DSTZ | 64-bit | Destination Z |
| $F02258 | B_SRCZ1 | 64-bit | Source Z register 1: integer parts of computed Z (JTRM Rev 8 p.76; v10 p.4) |
| $F02260 | B_SRCZ2 | 64-bit | Source Z register 2: fractional parts of computed Z (JTRM Rev 8 p.76; v10 p.4) |
| $F02268 | B_PATD | 64-bit | Pattern data |
| $F02270 | B_IINC | 32-bit | Intensity increment (16.16 fixed) |
| $F02274 | B_ZINC | 32-bit | Z increment |
| $F02278 | B_STOP | 32-bit | Collision/stop mask |
| $F0227C-$F02288 | B_I3-B_I0 | 32-bit each | Corner intensities |
| $F0228C-$F02298 | B_Z3-B_Z0 | 32-bit each | Corner Z values |

## B_COUNT Register ($F0223C)

| Bits | Name | Description |
|------|------|-------------|
| 0-15 | INNER | Inner loop count (pixels per line); 1-65536, encoded 0 = 65536 (v8 p.75) |
| 16-31 | OUTER | Outer loop count (number of lines); 1-65536, encoded 0 = 65536 (v8 p.75) |

Source: JTRM Rev 8 p.75 "Counters Register". The accurate engine does NOT run inner 0 as 65536 steps (it does one
step of at most 64 px, because `BlitterMidsummer2` stops on the bit-15 crossing); real games rely on that (see
`src/core/crash_detect.c` `inframe_hang`, `docs/jtrm-errata.md` A5).

## Known Emulation Gotchas

1. **PATD phrase alignment**: Pattern data is phrase-aligned. When PATDSEL is used with pixel sizes smaller than a phrase, the blitter selects the correct pixel from the phrase based on the destination X position. GOTCHA: the pattern data register may need to be written with the same pixel replicated across the full 64-bit phrase for solid fills.

2. **A1_PIXEL writeback**: After a blit completes, A1_PIXEL and A2_PIXEL are updated to point past the last written pixel. Games read these back to chain blits. The "fast" blitter in this emulator has a known divergence from the "accurate" blitter in A1_PIXEL writeback values.

3. **Gouraud intensity offset**: There is a known divergence between fast and accurate blitter modes in Gouraud shading output. See `test/tools/test_blitter_compare`.

4. **BUSHI priority (do not set)**: When BUSHI (bit 29) is set, the blitter runs at elevated bus priority (above the OP, v8 p.75). The manual documents it as a speed-up for many short blits, but v8's own bug list (TOM #24, p.138), Software Reference v2.4 (PDF p.63) and Hardware Bugs & Warnings p.2 #5 all say it must not be set: it can corrupt the OP line-buffer address (horizontal black stripes). The emulator does not model priority escalation (`src/tom/blitter.c`: "Missing: BUSHI"). Earlier text here claimed "games use this for time-critical blits"; that is unverified. See `docs/jtrm-errata.md` A3.

5. **Phrase mode vs pixel mode**: The blitter can address in phrase units or pixel units (XADDCTL in FLAGS). Phrase mode is faster but less flexible. Some games mix modes within the same blit sequence.

6. **Clipping boundary**: A1_CLIP sets a clipping window. Pixels outside this window are not written. The clip is checked per-pixel and can significantly slow down blits that mostly clip.

7. **64-bit register writes**: All 64-bit registers (B_SRCD, B_DSTD, B_PATD, etc.) require two 32-bit writes. The order matters on some hardware (high word first, then low word). The emulator should accept either order.

Source files: `src/tom/blitter.c`, `src/tom/blitter_simd_sse2.c`, `src/tom/blitter_simd_neon.c`, `test/tools/test_blitter_compare`
