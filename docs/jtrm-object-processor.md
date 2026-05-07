# JTRM Object Processor Reference (Distilled)

> **This is NOT a verbatim copy of the Jaguar Technical Reference Manual.**
> It is a distilled, reorganized reference synthesized from the JTRM,
> optimized for emulation developers and LLM consumption. Register
> addresses, bit layouts, and behavioral notes are sourced from the
> official Atari documentation and cross-referenced with this codebase.

---

## Overview

The Object Processor is a DMA-driven display engine in TOM. It traverses a linked list of "objects" in main RAM during each scanline's active display period, fetching and rendering bitmap data to the line buffer. It's the primary mechanism for displaying graphics on the Jaguar.

The OP runs at the system bus clock and processes objects between HDB (horizontal display begin) and HDE (horizontal display end) for each scanline between VDB (vertical display begin) and VDE (vertical display end).

Source: `src/tom/op.c`

## Object Types

All objects are 64-bit (one phrase) or 128-bit (two phrases). The type field is in bits [2:0] of the first phrase.

| Type | Name | Phrases | Description |
|------|------|---------|-------------|
| 0 | BITOBJ | 2 | Bitmap object -- renders a horizontal strip of pixel data |
| 1 | SCBITOBJ | 2+ | Scaled bitmap object -- like BITOBJ with hardware X/Y scaling |
| 2 | GPUOBJ | 1 | GPU interrupt object -- triggers GPU interrupt when OP reaches it |
| 3 | BRANCHOBJ | 1 | Branch object -- conditional linked-list branch |
| 4 | STOPOBJ | 1 | Stop object -- ends OP processing for this scanline |

## BITOBJ (Type 0) -- Bitmap Object

Two phrases (128 bits total).

### First Phrase (bits 63-0):
| Bits | Name | Description |
|------|------|-------------|
| 0-2 | TYPE | Object type = 0 |
| 3-13 | YPOS | Vertical position (first visible line, in half-lines) |
| 14-23 | HEIGHT | Object height in scanlines (number of lines of data) |
| 24-42 | LINK | Address of next object in list (phrase-aligned, bits [21:3] of address) |
| 43-63 | DATA | Address of pixel data (phrase-aligned, bits [21:3] of address) |

### Second Phrase (bits 63-0):
| Bits | Name | Description |
|------|------|-------------|
| 0-11 | XPOS | Horizontal position in pixels (signed 12-bit, can be negative for partial offscreen) |
| 12-14 | DEPTH | Pixel depth: 0=1bpp, 1=2bpp, 2=4bpp, 3=8bpp, 4=16bpp, 5=24bpp |
| 15-17 | PITCH | Data phrase spacing: distance in phrases between successive pixels |
| 18-27 | DWIDTH | Data width: number of phrases per scanline of image data |
| 28-37 | IWIDTH | Image width: number of phrases to render (can differ from DWIDTH for clipping) |
| 38-44 | INDEX | Colour index offset (added to pixel value for palette lookup) |
| 45 | REFLECT | Mirror horizontally (1=right-to-left rendering) |
| 46 | RMW | Read-modify-write mode (reads line buffer, combines with object data) |
| 47 | TRANS | Transparency enable (pixel value 0 = transparent, not written to line buffer) |
| 48 | RELEASE | Release bus between phrases (allows other bus masters to interleave) |
| 49-54 | FIRSTPIX | First pixel to display within the first phrase (for horizontal fine scrolling) |

### How BITOBJ Rendering Works
1. OP checks if current scanline is within [YPOS, YPOS+HEIGHT)
2. If yes, reads DATA pointer to fetch pixel data phrases
3. Renders IWIDTH phrases of pixels starting at XPOS
4. DATA pointer auto-advances by DWIDTH phrases each scanline
5. After rendering, follows LINK to next object

## SCBITOBJ (Type 1) -- Scaled Bitmap Object

Same as BITOBJ but adds a third phrase for scaling parameters:

### Third Phrase (scaling, bits 63-0):
| Bits | Name | Description |
|------|------|-------------|
| 0-7 | HSCALE | Horizontal scale factor (3.5 fixed point: integer[7:5], fraction[4:0]) |
| 8-15 | VSCALE | Vertical scale factor (3.5 fixed point) |
| 16-31 | REMAINDER | Vertical remainder (fractional accumulator for Y scaling) |

Scale factor encoding (3.5 fixed point):
- $20 (0b001_00000) = 1.0x (no scaling)
- $40 (0b010_00000) = 2.0x (double size)
- $10 (0b000_10000) = 0.5x (half size)

For vertical scaling, the OP uses REMAINDER to accumulate fractional lines. When REMAINDER overflows, the same DATA line is repeated (for scaling > 1x) or a line is skipped (for scaling < 1x).

## GPUOBJ (Type 2) -- GPU Interrupt Object

Single phrase. When the OP encounters this object during scanline processing, it triggers a GPU interrupt (interrupt source 3 -- Object Processor).

| Bits | Name | Description |
|------|------|-------------|
| 0-2 | TYPE | Object type = 2 |
| 3-13 | YPOS | Line at which to trigger |
| 14-42 | -- | Unused |
| 43-63 | LINK | Next object address (NOT a data pointer) |

Used for: mid-frame effects, palette changes, display list modifications during vblank.

Note: Despite the name, GPUOBJ doesn't run GPU code directly. It fires an interrupt; the GPU ISR at vector offset $30 (interrupt 3) does the actual work.

## BRANCHOBJ (Type 3) -- Branch Object

Single phrase. Conditionally follows an alternate link based on a comparison.

| Bits | Name | Description |
|------|------|-------------|
| 0-2 | TYPE | Object type = 3 |
| 3-13 | YPOS | Y position for comparison |
| 14-15 | CC | Condition code: 0=YPOS > VC, 1=YPOS = VC, 2=YPOS < VC, 3=flag set |
| 16-23 | -- | Unused |
| 24-42 | LINK | Branch target address (taken if condition true) |

If the condition is false, the OP falls through to the next phrase in memory (i.e., LINK is only followed on branch-taken).

BRANCHOBJ enables: Y-sorted display lists, scanline-conditional rendering, skip-over of off-screen objects for performance.

CC=3 (flag set): branches if the OP flag (OBF register) is set. Used for double-buffering display lists.

## STOPOBJ (Type 4) -- Stop Object

Single phrase. Halts OP processing for the current scanline.

| Bits | Name | Description |
|------|------|-------------|
| 0-2 | TYPE | Object type = 4 |
| 3-63 | -- | Unused (but bit 3 may optionally trigger an interrupt) |

The OP restarts from OLP (Object List Pointer) on the next scanline.

## Display Pipeline

1. At the start of each scanline (after HDB), the OP begins traversing the object list from OLP
2. For each object: check type, evaluate conditions (YPOS vs current VC), render if applicable
3. Pixel data is written to the line buffer (internal to TOM)
4. At HDE, the line buffer is shifted out through the pixel path to video output
5. The pixel path applies colour lookup (CRY/RGB conversion) and border colour

### Line Buffer
The OP renders into a line buffer, not directly to the framebuffer. The line buffer is double-buffered: one is being filled by the OP while the other is being displayed. This is transparent to software.

Line buffer width = HP (horizontal period) worth of pixels. For NTSC at divisor 4: ~332 displayable pixels.

## Colour Space

The Jaguar supports two primary colour modes:

### CRY (Cyan-Red-Yellow)
16-bit per pixel: C[15:8] (8-bit intensity), R[7:4] (4-bit red-yellow), Y[3:0] (4-bit cyan-green)
- C component is intensity (0=black, $FF=full brightness)
- R and Y select a chrominance (hue + saturation) from a 16x16 lookup table
- The actual RGB values are computed from CRY via lookup tables in hardware

CRY advantages: smooth Gouraud shading (just interpolate C), compact colour space.

### RGB16
16-bit per pixel: R[15:11] (5-bit), G[10:5] (6-bit?), B[4:0] (5-bit)
Standard 5-6-5 or 5-5-5+1 format depending on VMODE settings.

### Indexed (1/2/4/8 bpp)
Lower bit depths use a CLUT (colour lookup table) in TOM. The INDEX field in BITOBJ adds an offset to the pixel value before CLUT lookup, allowing multiple objects to share the same CLUT with different palettes.

## OP Control Registers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F00020 | OLP | WO | Object list pointer (24-bit, phrase-aligned) |
| $F00026 | OBF | WO | Object flag (for BRANCHOBJ CC=3) |
| $F00010-$F0001E | OB[0-3] | WO | Current object data (debug, 4 x 16-bit) |

## Known Emulation Gotchas

1. **YPOS is in half-lines**: On interlaced displays, YPOS counts half-lines, not full scanlines. For non-interlaced (most games), YPOS effectively counts scanlines but the numbering may be offset from what you expect. The OP compares YPOS against VC (vertical count register).

2. **LINK is a phrase address**: LINK field bits [42:24] map to physical address bits [21:3]. The link is phrase-aligned (8-byte aligned). Forgetting the shift gives wrong addresses.

3. **DATA pointer auto-advance**: After each scanline, DATA advances by DWIDTH phrases. This happens automatically in hardware. The emulator must replicate this in the OP's per-scanline processing.

4. **Object list must end with STOPOBJ**: If the OP doesn't find a STOPOBJ, it keeps fetching objects until HDE, potentially reading garbage. Well-formed display lists always end with STOPOBJ.

5. **OP vs blitter rendering**: The OP renders to the line buffer; the blitter renders to main RAM. They can be used together (OP for sprites/backgrounds, blitter for framebuffer effects) or separately (some games use only the blitter with a single fullscreen BITOBJ to display the framebuffer).

6. **TRANS flag and colour 0**: When TRANS is set, pixels with value 0 are not written to the line buffer (transparent). This is the standard transparency mechanism. RMW mode allows reading the existing line buffer value for blending effects.

7. **FIRSTPIX for fine scrolling**: FIRSTPIX specifies which pixel within the first data phrase to start rendering from. This enables smooth horizontal scrolling at sub-phrase granularity without needing to shift the entire data buffer.

8. **VDE comparison bug**: On some hardware revisions, VDE comparison against VC doesn't work correctly unless VC is first written with $FFFF. See the VC register workaround in `src/tom/tom.c`.

9. **Scaled objects and REMAINDER**: For scaled objects, the REMAINDER field must be managed correctly. The OP writes back the updated REMAINDER to the object in RAM after each scanline. This means objects in ROM won't scale correctly (they need to be in RAM for writeback).

Source: `src/tom/op.c`, `src/tom/tom.c`
