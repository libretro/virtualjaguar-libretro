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
| 0-7 | HSCALE | Horizontal scale (3.5 fixed point). **Pixels written into the line buffer per _source_ pixel.** |
| 8-15 | VSCALE | Vertical scale (3.5 fixed point). **Display lines drawn per _source_ line.** Equals HSCALE for an object to keep its aspect ratio. |
| 16-23 | REMAINDER | Vertical remainder (3.5 fixed point, **8 bits**) |
| 24-63 | -- | Unused, write zeroes |

Scale factor encoding (3.5 fixed point):
- $20 (0b001_00000) = 1.0x (no scaling)
- $40 (0b010_00000) = 2.0x (double size)
- $10 (0b000_10000) = 0.5x (half size)

**Direction matters and is easy to get backwards.** HSCALE/VSCALE are *destination
per source*, so a value below $20 SHRINKS the object and a value above $20 magnifies
it. An object with IWIDTH=40 phrases at 8bpp (320 source pixels) and HSCALE=$10
renders 320 x 0.5 = **160** pixels wide, not 640. `OPProcessScaledBitmap()` in
`src/tom/op.c` implements this as `scaledWidthInPixels = (iwidth *
phraseWidthToPixels[depth] * hscale) >> 5`, which is correct.

REMAINDER algorithm (JTRM, verbatim intent): after each display line is drawn REMAINDER
is decremented by one (i.e. $20). If it becomes negative, VSCALE is added until it is
positive again, and HEIGHT is decremented every time VSCALE is added. The new REMAINDER
is written back to the object. So the source line advances `ceil($20 / VSCALE)` times
per display line -- VSCALE < 1.0 shrinks. The horizontal loop in `op.c` is the exact
analogue of this, which is the cross-check that pins the HSCALE direction.

> Sourced verbatim from the Jaguar Technical Reference Manual Revision 8,
> "Scaled Bit Mapped Object" (Software Reference, p.20). An earlier version of this
> file listed REMAINDER as bits 16-31; that was wrong -- it is 8 bits at 16-23, and
> `op.c`'s `(p2 >> 16) & 0xFF` was right all along. Verified 2026-08-12 while
> investigating issue #354; the incorrect entry actively misled that investigation.

## GPUOBJ (Type 2) -- GPU Interrupt Object

Single phrase. When the OP encounters this object during scanline processing, it triggers a GPU interrupt (interrupt source 3 -- Object Processor).

| Bits | Name | Description |
|------|------|-------------|
| 0-2 | TYPE | Object type = 2 |
| 3-13 | YPOS | Active when VC matches YPOS, unless YPOS = $7FF (active for all VC) |
| 14-63 | DATA | Free for the GPU ISR. Memory-mapped as OB0-3, so the ISR can use it as data or as a pointer to further parameters. |

**There is no LINK field.** Execution continues with the object in the
*next phrase*; the object is a single phrase with no link. (An earlier
revision of this file listed bits 14-42 as unused and 43-63 as LINK --
both wrong. JTRM Rev 8, "Graphics Processor Object".)

Used for: mid-frame effects, palette changes, display list modifications during vblank.

Note: Despite the name, GPUOBJ doesn't run GPU code directly. It fires an interrupt; the GPU ISR at vector offset $30 (interrupt 3) does the actual work.

### Reading DATA back through OB ($F00010-$F00017)

The OP latches the whole phrase into OB before raising IRQ3, exposed as
**four 16-bit registers with the phrase's least significant word at the
lowest address**, each register big-endian internally:

| Register | Address   | Phrase bits |
|----------|-----------|-------------|
| OB0      | `$F00010` | `[15:0]`    |
| OB1      | `$F00012` | `[31:16]`   |
| OB2      | `$F00014` | `[47:32]`   |
| OB3      | `$F00016` | `[63:48]`   |

So TYPE and YPOS (phrase bits 0-13) read back at **`$F00010`**, and a
32-bit read at `$F00014` returns `(phrase[47:32] << 16) | phrase[63:48]`
-- entirely within the GPUOBJ DATA range, never TYPE. This is what
`OPSetCurrentObject()` (`src/tom/op.c`) implements.

**Source: the original Flare/Atari TOM design netlists**
(`jag_sim/netlists/tom/OB.NET:55-67`), under the comment *"the first
phrase can be read as four words"*:

```
Ob0rd[0-2]   := TS (dr[0-2],  type[0-2],      ob0r);
Ob0rd[3-13]  := TS (dr[3-13], ypos[0-10],     ob0r);
Ob0rd[14-15] := TS (dr[14-15],newheight[0-1], ob0r);
Ob1rd[0-7]   := TS (dr[0-7],  newheight[2-9], ob1r);
Ob1rd[8-15]  := TS (dr[8-15], link[0-7],      ob1r);
Ob2rd[0-10]  := TS (dr[0-10], link[8-18],     ob2r);
Ob2rd[11-15] := TS (dr[11-15],data[0-4],      ob2r);
Ob3rd[0-15]  := TS (dr[0-15], data[5-20],     ob3r);
```

Three checks pin the reading, none of them circular:

1. **Which strobe is which address.** `IODEC.NET:85-88` gives
   `ob0r..ob3r` at `axxx0/axxx2/axxx4/axxx6`, and those terms decode
   from the inverted address lines as low-nibble `0/2/4/6`
   (`Axxx0 := !ND3(al[3],al[2],al[1])`, etc.). The same decode file
   places `Hcr_` at `$F00004`, `Vcr_` at `$F00006` and `Obfw_` at
   `$F00026` -- all matching the JTRM register map exactly.
2. **Which end of a bus is bit 0.** `dr[0]` is D0, not D15:
   `Vc[0] := UPCNTS(vc[0],vco[0],...)` with the carry chain `vco[0-9]`
   feeding `Vc[1-10]` makes `vc[0]` the counter LSB, and
   `Vcd[0-11] := TS(dr[0-11],vc[0-11],vcrd)` puts that LSB on `dr[0]`.
   The JTRM has VC occupying bits 0-10 of `$F00006`. Same argument for
   HC.
3. **Which phrase bit is `type[0]`.** The write-back block a few lines
   down (`OB.NET:71-75`) drives `type[0-2] -> wd[0-2]`,
   `ypos[0-10] -> wd[3-13]`, `newheight[0-9] -> wd[14-23]`,
   `link[0-18] -> wd[24-42]`, `newdata[0-20] -> wd[43-63]` -- exactly
   the JTRM phrase layout, so `type[n]` is phrase bit `n`.

**Prior claim retracted.** This section previously asserted straight
big-endian (bit 63 at `$F00010`) on the strength of MAME's register
*naming* in `src/mame/atari/jaguar_v.cpp`
(`OB_HH(0x10) ... OB_LL(0x16)`). That is an enum label in a
reimplementation, not a hardware observation, and the design source
above contradicts it. The warning that formerly stood here -- "do not
flip this order" -- is withdrawn; the flip is correct, and the four
in-tree tests that asserted the old layout have been rederived from
`OB.NET` (see `test/acid/tests/op/op_gpu_int_object{,_halted}.s`,
`op_short_branch.s`, `test/tools/test_op_gpu_object.c`).

**Known divergence from hardware.** Real OB returns latched control
fields mixed with *live* counters: `data[0-20]` is a `UPCNT` that
advances as the object is drawn, and `newheight` comes from the
write-back path rather than the original phrase. We latch the verbatim
phrase instead. No title in the corpus can observe the difference --
every commercial GPU-object phrase carries DATA == 0 -- and modelling
the counters is a much larger change than this.

Rev 8's complete set of statements about OB is:

* Register table: `OB[0-3]  Object Code  F00010-16  RO`
* "These four registers allow the graphics processor to read the current
  object. This allows the graphics processor object to pass parameters
  to the GPU interrupt service routine."
* GPU object DATA bits are "memory mapped as the object code registers
  OB0-3, so the GPU can use them as data or as a pointer to additional
  parameters."
* "If the interrupt source was the Object Processor, then the interrupt
  service routine should read the Object Code registers, if required..."

That is all of it. **Rev 8 never says whether OB0 (`$F00010`) holds
phrase bits 63-48 or bits 15-0**, and its sample GPU ISR does not read
OB. Rev 10 repeats only the table row. Its general convention (p.131,
"Data Organisation - Big and Little Endian") -- the document "adopts the
big-endian convention", a big-endian system "will see the high word of
long-word at the low address" -- is about operands in memory, not about
how a four-register group is mapped, and the netlist shows it does not
carry over to OB. This is why the byte order had to be settled against
the design source rather than against the manual.

### Why this matters: Val d'Isere's IRQ3 gate (issue #354)

Val d'Isere Skiing's IRQ3 handler (vector `$F03030` -> `$F030C2`) does:

```
movei #$00F00014, r0
load  (r0), r0          ; OB2:OB3 -- DATA, per OB.NET
cmpq  #0, r0            ; signed 5-bit imm -- field 0 means 0, not 32
jump  Z, ($F03150)      ; -> per-scanline ground generator
```

`$F03150` reads VC and the table at `$001368E0`, does the per-line
perspective maths, and writes the per-scanline scaled-bitmap objects
into the display list at `[$F03094]` -- i.e. it *generates* the objects
`OPProcessScaledBitmap` then draws, which is consistent with the
measurement on #354 that the ground comes from the OP scaled-bitmap path
and barely touches the blitter.

The game's GPU object phrase is `$00000000000008EA` (TYPE = 2,
YPOS = 285, DATA = 0), so under the netlist mapping `$F00014` reads
**0** and the gate opens. Under the old straight-big-endian store it
returned the phrase's low long, which necessarily carries TYPE in bits
2-0 and so could never be zero: the handler read `$8EA`, the compare
never matched, and every IRQ3 (~72.8k per run) fell through to a
do-nothing epilogue.

Note `$F03098`, tested a few instructions later, is **not** the floor
gate -- it guards a line-buffer capture (`loadp` from `$F01800`,
`storep` to `$001417A8`) that the game legitimately leaves disabled;
nothing ever writes it non-zero and that is correct.

Every commercial GPU-object phrase in the test corpus carries DATA == 0
(Doom `$CD2`, Atari Karts `$2`, Attack of the Mutant Penguins `$2`,
Super Burnout / SlamRacer `$3FFA`, yarc `$2`/`$A`), so no other title
can discriminate the two orderings -- which is why the netlist, not a
corpus sweep, is the deciding evidence.

## BRANCHOBJ (Type 3) -- Branch Object

Single phrase. Conditionally follows an alternate link based on a comparison.

| Bits | Name | Description |
|------|------|-------------|
| 0-2 | TYPE | Object type = 3 |
| 3-13 | YPOS | Y position for comparison |
| 14-16 | CC | Condition code (see below) |
| 17-23 | -- | Unused |
| 24-42 | LINK | Branch target address (taken if condition true) |

| CC | Branch taken if |
|----|-----------------|
| 0 | YPOS == VC, **or** YPOS == $7FF |
| 1 | YPOS > VC |
| 2 | YPOS < VC |
| 3 | Object Processor flag (OBF bit 0) is set |
| 4 | On the second half of the display line (HC10 = 1) |

(An earlier revision of this file swapped CC 0 and 1 and omitted CC 4.
Verbatim from JTRM Rev 8, "Branch Object". Rev 8's own field table
prints CC as bits 14-15, which cannot hold five values; the OP decodes
three bits -- `(p0 >> 14) & 0x07` in `OPProcessList()`, `src/tom/op.c`.)

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
16-bit per pixel: Cyan[15:12] (4 bits), Red[11:8] (4 bits), intensitY[7:0] (8 bits).
The name spells the field order: **C**yan, **R**ed, intensit**Y**.
- The intensity field is the LOW byte (0=black, $FF=full brightness)
- Cyan and Red select a chrominance (hue + saturation) from a 16x16 lookup table
- Verified against the implementation: `src/tom/tom.c` (cyan `>>12`, red `>>8`,
  intensity `& 0x00FF`) and `src/tom/op.c`.  An earlier revision of this file
  had the layout inverted (intensity in the high byte), which is wrong.
- The actual RGB values are computed from CRY via lookup tables in hardware

CRY advantages: smooth Gouraud shading (just interpolate intensity/Y), compact colour space.

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
| $F00010-$F00017 | OB[0-3] | **RO** | Current object phrase, latched by the OP. Four 16-bit registers, **phrase LSW at the lowest address**: OB0 `$F00010` = phrase[15:0] (TYPE/YPOS), OB1 = [31:16], OB2 `$F00014` = [47:32], OB3 = [63:48]. For a GPUOBJ, DATA reads back at `$F00014`. Sourced from `OB.NET:55-67` -- see "Reading DATA back through OB" above. |

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
