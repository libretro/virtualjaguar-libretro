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

Source: `src/tom/blitter.c`

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
| 0-1 | PITCH | Pixel pitch: 0=1 phrase, 1=2 phrases, 2=4, 3=8 |
| 3-5 | PIXEL | Pixel size: 0=1bpp, 1=2bpp, 2=4bpp, 3=8bpp, 4=16bpp, 5=32bpp |
| 6-8 | ZOFFS | Z data offset within phrase |
| 9-14 | WIDTH | Window width in 6-bit floating point |
| 16-17 | XADDCTL | X add control: 0=phrase, 1=pixel, 2=+0 (no change), 3=+0 |
| 18 | YADDCTL | Y add control: 0=+0, 1=+1 |
| 19 | XSIGNSUB | X addition is subtraction (for right-to-left) |
| 20 | YSIGNSUB | Y addition is subtraction (for bottom-to-top) |

### Window Width Encoding (6-bit Floating Point)

The WIDTH field in A1_FLAGS/A2_FLAGS uses a custom 6-bit float:
- Bits [2:0] = mantissa (3 bits)
- Bits [5:3] = exponent (3 bits)
- Implicit leading 1 bit in mantissa

```
value = (0b1000 | mantissa) << exponent

Examples:
  WIDTH=0b000_000: (8|0)<<0 = 8
  WIDTH=0b001_000: (8|0)<<1 = 16
  WIDTH=0b001_100: (8|4)<<1 = 24
  WIDTH=0b010_000: (8|0)<<2 = 32
  WIDTH=0b010_100: (8|4)<<2 = 48
  WIDTH=0b011_000: (8|0)<<3 = 64
  WIDTH=0b100_000: (8|0)<<4 = 128
  WIDTH=0b101_000: (8|0)<<5 = 256
  WIDTH=0b110_000: (8|0)<<6 = 512
  WIDTH=0b111_000: (8|0)<<7 = 1024
```

Not all widths are representable. Widths must be of the form (8+m)*2^e where m is 0-7.

Source: `src/tom/blitter.c` (search for `blitter_scanline_width`)

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
| 13 | ZBUFF | Enable Z-buffer comparison |
| 14 | TOPBEN | Top byte transparency enable |
| 15 | TOPNEN | Top nibble transparency enable |
| 16 | PATDSEL | Use pattern data (B_PATD) instead of LFU output |
| 17 | ADDDSEL | Add source and destination pixel values |
| 18-20 | ZMODE | Z compare mode: 0=never, 1=less, 2=equal, 3=lequal, 4=greater, 5=nequal, 6=gequal, 7=always |
| 21-24 | LFU | Logic Function Unit control (4-bit truth table) |
| 25 | CMPDST | Compare with destination (else source) for transparency |
| 26 | BCOMPEN | Bit comparator enable (1bpp transparency) |
| 27 | DCOMPEN | Data comparator enable (pixel-level transparency) |
| 28 | BKGWREN | Allow writes to background (transparent) pixels |
| 29 | BUSHI | High bus priority (moves blitter up in priority chain) |
| 30 | SRCSHADE | Apply Gouraud shading to source data |

### B_CMD Status (read from same address $F02238)

When read, returns blitter status:
- Bit 0: IDLE (1 = blitter is idle/done)
- Bits 1-15: Internal state machine status (for debugging)

## Logic Function Unit (LFU)

4-bit truth table encoding for combining source (S) and destination (D):

| LFU[3:0] | S=0,D=0 | S=0,D=1 | S=1,D=0 | S=1,D=1 | Function |
|-----------|---------|---------|---------|---------|----------|
| 0000 | 0 | 0 | 0 | 0 | ZERO |
| 0001 | 1 | 0 | 0 | 0 | ~S & ~D (NOR) |
| 0010 | 0 | 0 | 1 | 0 | S & ~D |
| 0011 | 1 | 0 | 1 | 0 | S (REPLACE) |
| 0100 | 0 | 1 | 0 | 0 | ~S & D |
| 0101 | 1 | 1 | 0 | 0 | D (NOP) |
| 0110 | 0 | 1 | 1 | 0 | S ^ D (XOR) |
| 0111 | 1 | 1 | 1 | 0 | S | D (OR) |
| 1000 | 0 | 0 | 0 | 1 | ~S & D (NAND complement?) |
| 1001 | 1 | 0 | 0 | 1 | ~(S ^ D) (XNOR) |
| 1100 | 0 | 0 | 1 | 1 | S & D (AND) |
| 1111 | 1 | 1 | 1 | 1 | ONE |

In B_CMD encoding: LFU bits are at positions [24:21], so:
- LFU_REPLACE (S copy) = 0b0011 << 21 = $00600000
- LFU_OR = 0b0111 << 21 = $00E00000
- LFU_XOR = 0b0110 << 21 = $00C00000
- LFU_AND = 0b1100 << 21 = $01800000
- LFU_ZERO = 0b0000 << 21 = $00000000

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
- ZBUFF bit in B_CMD
- ZMODE[18:20] in B_CMD selects comparison
- B_Z0-B_Z3: Corner Z values
- B_ZINC: Z increment per pixel
- B_SRCZ1/B_SRCZ2: Source Z values (fractional/integer)
- DSTWRZ: write Z to destination Z buffer
- DSTENZ: read destination Z for comparison

Gouraud + Z-buffer example (from JTRM):
1. Set A1 = colour buffer, A2 = source (or pattern for solid polygons)
2. Set Z buffer in a separate pass or use interleaved Z
3. Enable GOURD | ZBUFF | DSTWRZ
4. Set intensities and Z values at corners, increments per pixel

## Data Registers

| Address | Name | Size | Description |
|---------|------|------|-------------|
| $F02240 | B_SRCD | 64-bit | Source data (2 x 32-bit writes) |
| $F02248 | B_DSTD | 64-bit | Destination data |
| $F02250 | B_DSTZ | 64-bit | Destination Z |
| $F02258 | B_SRCZ1 | 64-bit | Source Z fraction |
| $F02260 | B_SRCZ2 | 64-bit | Source Z integer |
| $F02268 | B_PATD | 64-bit | Pattern data |
| $F02270 | B_IINC | 32-bit | Intensity increment (16.16 fixed) |
| $F02274 | B_ZINC | 32-bit | Z increment |
| $F02278 | B_STOP | 32-bit | Collision/stop mask |
| $F0227C-$F02288 | B_I3-B_I0 | 32-bit each | Corner intensities |
| $F0228C-$F02298 | B_Z3-B_Z0 | 32-bit each | Corner Z values |

## B_COUNT Register ($F0223C)

| Bits | Name | Description |
|------|------|-------------|
| 0-15 | INNER | Inner loop count (pixels per line) |
| 16-31 | OUTER | Outer loop count (number of lines) |

## Known Emulation Gotchas

1. **PATD phrase alignment**: Pattern data is phrase-aligned. When PATDSEL is used with pixel sizes smaller than a phrase, the blitter selects the correct pixel from the phrase based on the destination X position. GOTCHA: the pattern data register may need to be written with the same pixel replicated across the full 64-bit phrase for solid fills.

2. **A1_PIXEL writeback**: After a blit completes, A1_PIXEL and A2_PIXEL are updated to point past the last written pixel. Games read these back to chain blits. The "fast" blitter in this emulator has a known divergence from the "accurate" blitter in A1_PIXEL writeback values.

3. **Gouraud intensity offset**: There is a known divergence between fast and accurate blitter modes in Gouraud shading output. See `test/tools/test_blitter_compare`.

4. **BUSHI priority**: When BUSHI (bit 29) is set, the blitter runs at elevated bus priority (level 5 instead of 10). This can starve the 68K. Games use this for time-critical blits.

5. **Phrase mode vs pixel mode**: The blitter can address in phrase units or pixel units (XADDCTL in FLAGS). Phrase mode is faster but less flexible. Some games mix modes within the same blit sequence.

6. **Clipping boundary**: A1_CLIP sets a clipping window. Pixels outside this window are not written. The clip is checked per-pixel and can significantly slow down blits that mostly clip.

7. **64-bit register writes**: All 64-bit registers (B_SRCD, B_DSTD, B_PATD, etc.) require two 32-bit writes. The order matters on some hardware (high word first, then low word). The emulator should accept either order.

Source files: `src/tom/blitter.c`, `src/tom/blitter_simd_sse2.c`, `src/tom/blitter_simd_neon.c`, `test/tools/test_blitter_compare`
