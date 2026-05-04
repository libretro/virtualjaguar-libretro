# Atari Jaguar Register Map -- Distilled Reference

Synthesized from the Jaguar Technical Reference Manual (JTRM) v2.4 and
Software Reference Manual v2.4. Reorganized for emulation-developer and LLM
consumption. This is NOT a verbatim copy of any Atari document; it is a
distilled, corrected summary optimized for quick lookup during emulation work.

Cross-reference source files:
`src/tom/tom.c`, `src/tom/gpu.c`, `src/tom/blitter.c`, `src/tom/op.c`,
`src/jerry/jerry.c`, `src/jerry/dsp.c`, `src/jerry/joystick.c`,
`src/core/vjag_memory.c`

Clock rates (JTRM-authoritative):
- NTSC system clock: 26.590906 MHz
- PAL system clock: 26.593900 MHz
- 68000: system clock / 2 (~13.295 MHz)
- GPU/DSP/Blitter/OP: full system clock rate

---

## Memory Map Overview

| Range | Size | Contents |
|-------|------|----------|
| $000000-$1FFFFF | 2 MB | DRAM (main RAM) |
| $200000-$3FFFFF | 2 MB | DRAM mirror (4 MB systems) |
| $800000-$DFFFFF | 6 MB | Cartridge ROM |
| $E00000-$E1FFFF | 128 KB | Bootstrap ROM (BIOS) |
| $F00000-$F0FFFF | 64 KB | TOM registers + CLUT + Line buffer |
| $F10000-$F1FFFF | 64 KB | JERRY registers + DSP RAM + Wave ROM |
| $F14000-$F14003 | 4 B | Joystick registers |

ROMHI bit (MEMCON1 bit 0) swaps ROM and DRAM positions. Standard 68000
operation uses ROMHI=1 (addresses above are for ROMHI=1).

---

## System Setup Registers (TOM $F00000-$F00058)

Registers in **bold** are boot-time only -- do NOT modify from game code.

| Address | Name | R/W | Width | Description |
|---------|------|-----|-------|-------------|
| **$F00000** | **MEMCON1** | RW | 16 | Memory config 1. Bit 0: ROMHI. Bits 1-2: ROMWIDTH. Bits 3-4: ROMSPEED. Bits 5-6: DRAMSPEED. Bit 7: FASTROM. Bits 11-12: IOSPEED. Bit 12: BIGEND. Bit 13: HILO. Bit 14: CPU32. |
| **$F00002** | **MEMCON2** | RW | 16 | Memory config 2. Bits 0-1: COLS0. Bits 2-3: DWIDTH0. Bits 4-5: COLS1. Bits 6-7: DWIDTH1. Bits 8-11: REFRATE. |
| $F00004 | HC | RW | 16 | Horizontal count. 10-bit counter (bits 0-9), counts up to HP. Bit 10: which half of line (determines which line buffer is active). Written only for ASIC test. |
| $F00006 | VC | RW | 16 | Vertical count. 11-bit counter, counts half-lines. Bit 11: odd/even field. GOTCHA: Write $FFFF as workaround for VDE comparison bug on Jaguar Console. |
| $F00008 | LPH | RO | 16 | Light pen horizontal (11 bits) |
| $F0000A | LPV | RO | 16 | Light pen vertical (11 bits, half-lines) |
| $F00010-16 | OB[0-3] | RO | 64 | Object processor current object data (4 x 16-bit reads). Allows GPU to read OP object parameters. |
| $F00020 | OLP | WO | 32 | Object list pointer. Bottom 3 bits always zero (phrase-aligned). Word-swapped: write high word first, then low word. 68000 should NOT write this directly -- use GPU. |
| $F00026 | OBF | WO | 16 | Object processor flag. Bit 0 tested by OP branch. Any write restarts OP. |
| $F00028 | VMODE | WO | 16 | Video mode. Bit 0: VIDEN. Bits 1-2: MODE (0=CRY16, 1=RGB24, 2=DIRECT16, 3=RGB16). Bit 3: GENLOCK. Bit 4: INCEN. Bit 5: BINC. Bit 6: CSYNC. Bit 7: BGEN. Bit 8: VARMOD. Bits 9-11: PWIDTH1-8 (pixel width in video clocks, actual = value+1). |
| $F0002A | BORD1 | WO | 16 | Border colour (red and green components) |
| $F0002C | BORD2 | WO | 16 | Border colour (blue component) |
| **$F0002E** | **HP** | WO | 16 | Horizontal period (10-bit). Half-line length in video clock ticks. Period = value + 1. |
| **$F00030** | **HBB** | WO | 16 | Horizontal blanking begin (11-bit). MSB usually set (blanking in second half of line). |
| **$F00032** | **HBE** | WO | 16 | Horizontal blanking end (11-bit). MSB usually clear (blanking ends in first half). |
| **$F00034** | **HS** | WO | 16 | Horizontal sync (11-bit). Sync pulse width. Pulses start when HC matches HS. |
| **$F00036** | **HVS** | WO | 16 | Horizontal vertical sync (10-bit). End position of vertical sync pulses. |
| $F00038 | HDB1 | WO | 16 | Horizontal display begin 1 (11-bit). OP starts when HC matches. |
| $F0003A | HDB2 | WO | 16 | Horizontal display begin 2 (11-bit). Alternate OP start (for 2x OP per line). |
| $F0003C | HDE | WO | 16 | Horizontal display end (11-bit). OP stops; border or black after this. |
| **$F0003E** | **VP** | WO | 16 | Vertical period (11-bit). Half-lines per field. If odd, display is interlaced. Actual count = value + 1. |
| **$F00040** | **VBB** | WO | 16 | Vertical blanking begin (11-bit half-line) |
| **$F00042** | **VBE** | WO | 16 | Vertical blanking end (11-bit half-line) |
| **$F00044** | **VS** | WO | 16 | Vertical sync (11-bit half-line). Vertical sync starts here. |
| $F00046 | VDB | WO | 16 | Vertical display begin (11-bit half-line). OP processing starts every line from here to VDE. |
| $F00048 | VDE | WO | 16 | Vertical display end (11-bit half-line). GOTCHA: Due to a hardware bug, set VDE=$FFFF on Jaguar Console so OP processes every line. |
| **$F0004A** | **VEB** | WO | 16 | Vertical equalization begin (11-bit half-line) |
| **$F0004C** | **VEE** | WO | 16 | Vertical equalization end (11-bit half-line) |
| $F0004E | VI | WO | 16 | Vertical interrupt (11-bit half-line). Must be odd for non-interlaced. Fires once per frame (interlaced: once per field). |
| $F00050-52 | PIT[0-1] | WO | 32 | Programmable interrupt timer. PIT0 ($F00050) = prescaler, PIT1 ($F00052) = divider. Freq = sysclk / ((PIT0+1) * (PIT1+1)). Zero in PIT0 disables timer. |
| **$F00054** | **HEQ** | WO | 16 | Horizontal equalization end (10-bit). Short sync pulses near vertical sync. |
| $F00058 | BG | WO | 16 | Background colour (CRY). Line buffer cleared to this in BGEN mode. |

---

## TOM Interrupt Registers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F000E0 | INT1 | RW | CPU interrupt control/status |
| $F000E2 | INT2 | WO | CPU interrupt resume (restores bus priorities after ISR) |

### INT1 bit layout

Reading INT1 returns enable + pending status in the same bit positions.

| Bit | Name (enable) | Name (clear) | Interrupt source |
|-----|---------------|--------------|------------------|
| 0 | C_VIDENA | -- | Video (vertical interrupt at VI half-line) |
| 1 | C_GPUENA | -- | GPU (GPU writes to internal register) |
| 2 | C_OPENA | -- | Object Processor (stop object) |
| 3 | C_PITENA | -- | Timer (TOM PIT) |
| 4 | C_JERENA | -- | Jerry (active-high edge-triggered input from Jerry) |
| 8 | -- | C_VIDCLR | Clear pending video interrupt |
| 9 | -- | C_GPUCLR | Clear pending GPU interrupt |
| 10 | -- | C_OPCLR | Clear pending OP interrupt |
| 11 | -- | C_PITCLR | Clear pending timer interrupt |
| 12 | -- | C_JERCLR | Clear pending Jerry interrupt |

**Usage pattern:** Write enable bits to low byte. To clear pending, write 1 to
the corresponding bit in the high byte. INT2 must be written at the end of
every CPU ISR to restore bus priorities.

---

## Colour Look-Up Table and Line Buffer

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F00400-$F007FE | CLUT | RW | 256-entry x 16-bit colour look-up table. Two tables: A ($F00400-$F005FE) and B ($F00600-$F007FE). Writing to either range writes both. |
| $F00800-$F00D9E | LBUF A | RW | Line buffer A (360 x 32-bit words). Add $8000 for 32-bit write access. |
| $F01000-$F0159E | LBUF B | RW | Line buffer B. |
| $F01800-$F01D9E | LBUF (test) | RW | Line buffer test/GPU assist range. |

---

## GPU Registers ($F02100-$F0211F)

All GPU registers are 32 bits wide.

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F02100 | G_FLAGS | RW | GPU flags register (see bit layout below) |
| $F02104 | G_MTXC | WO | Matrix control. Bits 0-3: matrix width (3-15). Bit 4: MATCOL (column vs row access). |
| $F02108 | G_MTXA | WO | Matrix address. Bits 2-11: local RAM address of matrix data. |
| $F0210C | G_END | WO | Data organisation. Bit 0: BIG_IO (big-endian CPU I/O). Bit 1: BIG_PIX (big-endian pixel org). Bit 2: BIG_INST (big-endian instruction fetch). |
| $F02110 | G_PC | RW | GPU program counter. Write before setting GPUGO. Read gives current instruction address. |
| $F02114 | G_CTRL | RW | GPU control/status (see bit layout below) |
| $F02118 | G_HIDATA | RW | High 16 bits for 48-bit GPU phrase reads/writes |
| $F0211C | G_REMAIN | RO | Division remainder (read) |
| $F0211C | G_DIVCTRL | WO | Division control (write). Bit 0: DIV_OFFSET (1 = 16.16 fixed-point division). |

### GPU local RAM: $F03000-$F03FFF (4 KB)

Also available at $F0B000-$F0BFFF as 32-bit write-only (GPU/Blitter fast path).

Interrupt vector offsets within local RAM:
- $F03000: Int 0 -- CPU (external)
- $F03010: Int 1 -- DSP/Jerry
- $F03020: Int 2 -- PIT (timer)
- $F03030: Int 3 -- Object Processor
- $F03040: Int 4 -- Blitter

### G_FLAGS bit layout

| Bit(s) | Name | Description |
|--------|------|-------------|
| 0 | ZERO_FLAG | ALU zero flag |
| 1 | CARRY_FLAG | ALU carry flag |
| 2 | NEGA_FLAG | ALU negative flag |
| 3 | IMASK | Interrupt mask (set on ISR entry, cleared by writing 0) |
| 4 | G_CPUENA | Enable Int 0 (CPU) |
| 5 | G_JERENA | Enable Int 1 (Jerry/DSP) |
| 6 | G_PITENA | Enable Int 2 (PIT/Timer) |
| 7 | G_OPENA | Enable Int 3 (Object Processor) |
| 8 | G_BLITENA | Enable Int 4 (Blitter) |
| 9 | G_CPUCLR | Clear Int 0 latch |
| 10 | G_JERCLR | Clear Int 1 latch |
| 11 | G_PITCLR | Clear Int 2 latch |
| 12 | G_OPCLR | Clear Int 3 latch |
| 13 | G_BLITCLR | Clear Int 4 latch |
| 14 | REGPAGE | Select register bank 1 (overridden by IMASK) |
| 15 | DMAEN | DMA priority for LOAD/STORE. HARDWARE BUG: must not be set on Jaguar Console. |

**WARNING:** Writing to flag bits and using them in the following instruction
will not work due to pipe-lining. Insert at least 2 instructions (or 4 for
indexed STORE) between a flag-setting STORE and a flag-dependent instruction.

### G_CTRL bit layout

| Bit(s) | Name | Description |
|--------|------|-------------|
| 0 | GPUGO | Start/stop GPU. Write G_PC first. |
| 1 | CPUINT | Write 1 to interrupt 68000 from GPU. Always reads 0. |
| 2 | FORCEINT0 | Force GPU interrupt type 0. Always reads 0. |
| 3 | SINGLE_STEP | Enable single-step mode |
| 4 | SINGLE_GO | Advance one instruction in single-step mode |
| 5 | -- | Unused, write zero |
| 6-10 | G_CPULAT..G_BLITLAT | Interrupt latch status (read-only). 0=CPU, 1=Jerry, 2=PIT, 3=OP, 4=Blitter |
| 11 | BUS_HOG | GPU holds bus between program fetches (do not set on Console) |
| 12-15 | VERSION | GPU version code (read-only). 1=pre-production, 2=production. |

---

## Blitter Registers ($F02200-$F02298)

Data registers are 64-bit (two 32-bit writes, low address first). Address
registers are 32-bit. All registers may only be written while the Blitter is
idle. B_CMD write initiates the Blitter operation.

### Address Registers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F02200 | A1_BASE | WO | A1 base address (phrase-aligned) |
| $F02204 | A1_FLAGS | WO | A1 control flags (see detailed layout below) |
| $F02208 | A1_CLIP | WO | A1 clipping window. Low word: width (15-bit unsigned). High word: height (15-bit unsigned). Top bit of each word ignored. |
| $F0220C | A1_PIXEL | RW | A1 pixel pointer. Low word: X (16-bit signed). High word: Y (16-bit signed). Readable after blit (RO*). |
| $F02210 | A1_STEP | WO | A1 step. Low word: X step (16-bit signed). High word: Y step (16-bit signed). Applied between outer loop passes. |
| $F02214 | A1_FSTEP | WO | A1 fractional step. Same layout as A1_STEP but fractional parts. |
| $F02218 | A1_FPIXEL | RW | A1 fractional pixel pointer (for DDA line-drawing). Low word: X frac. High word: Y frac. |
| $F0221C | A1_INC | WO | A1 increment (used in add-increment mode). Low word: X inc (16-bit signed). High word: Y inc (16-bit signed). |
| $F02220 | A1_FINC | WO | A1 fractional increment |
| $F02224 | A2_BASE | WO | A2 base address (phrase-aligned) |
| $F02228 | A2_FLAGS | WO | A2 control flags (simpler than A1 -- no fractional, no clipping) |
| $F0222C | A2_MASK | WO | A2 window mask. ANDed with pointer when Mask bit set in A2_FLAGS. For repeating textures/fill patterns. |
| $F02230 | A2_PIXEL | RW | A2 pixel pointer. Same format as A1_PIXEL. Readable after blit (RO*). |
| $F02234 | A2_STEP | WO | A2 step values |

### Control Registers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F02238 | B_CMD | WO | Blitter command -- writing initiates operation (see B_CMD layout below) |
| $F02238 | B_CMD (status) | RO | Blitter status. Bit 0: IDLE. Bit 1: STOPPED (collision). Bits 2-15: diagnostic state machine bits. Bits 16-31: inner count. |
| $F0223C | B_COUNT | WO | Loop counts. Low word: inner count (1-65536, 0=65536). High word: outer count (1-65536, 0=65536). |

### Data Registers (all 64-bit, written as two 32-bit words)

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F02240 | B_SRCD | WO** | Source data. Also holds intensity fractional parts for Gouraud. |
| $F02248 | B_DSTD | WO** | Destination data |
| $F02250 | B_DSTZ | WO** | Destination Z |
| $F02258 | B_SRCZ1 | WO** | Source Z register 1 (integer parts of computed Z) |
| $F02260 | B_SRCZ2 | WO** | Source Z register 2 (fractional parts of computed Z) |
| $F02268 | B_PATD | WO** | Pattern data. Also holds computed intensity integer parts and colours. |
| $F02270 | B_IINC | WO | Intensity increment (32-bit: 8.16 int+frac, top 8 bits = colour, leave zero) |
| $F02274 | B_ZINC | WO | Z increment (32-bit: 16.16 int+frac) |
| $F02278 | B_STOP | WO | Collision stop control. Bit 0: RESUME. Bit 1: ABORT. Bit 2: STOPEN (enable collision stops). |
| $F0227C | B_I3 | WO | Intensity 3 (24-bit: 8.16 number -- modifies pattern data and source data intensity fields) |
| $F02280 | B_I2 | WO | Intensity 2 |
| $F02284 | B_I1 | WO | Intensity 1 |
| $F02288 | B_I0 | WO | Intensity 0 |
| $F0228C | B_Z3 | WO | Z 3 (32-bit: 16.16 -- modifies source Z1 and Z2 fields) |
| $F02290 | B_Z2 | WO | Z 2 |
| $F02294 | B_Z1 | WO | Z 1 |
| $F02298 | B_Z0 | WO | Z 0 |

\* Must be refreshed after a blit.
\*\* Must be refreshed if used to store dynamic data (inner loop read with GOURD or GOURZ set).

Note: JTRM v2.2 and earlier reversed the descriptions of B_SRCZ1 and B_SRCZ2.
The equates were not changed, so source code is unaffected.

### A1_FLAGS / A2_FLAGS bit layout

| Bit(s) | Name | Description |
|--------|------|-------------|
| 0-1 | PITCH | Phrase gap: 2^PITCH phrases between successive pixel data phrases. 0=contiguous, 1=1 gap, 2=3 gaps, 3=7 gaps. Useful for Z-buffer interleaving. |
| 2 | -- | Unused |
| 3-5 | PIXEL | Pixel size: 0=1bpp, 1=2bpp, 2=4bpp, 3=8bpp, 4=16bpp, 5=32bpp |
| 6-8 | ZOFFS | Z data offset in phrases from pixel data. Values 0 and 7 not used. |
| 9-14 | WIDTH | Window width (6-bit floating point -- see encoding below) |
| 15 | -- | Unused (A1) / MASK enable (A2) |
| 16-17 | XADDCTL | X pointer update per inner loop pass: 00=XADDPHR (phrase boundary), 01=XADDPIX (+pixel size), 10=XADD0 (no change), 11=XADDINC (add A1_INC) |
| 18 | YADDCTL | Y pointer update within inner loop: 0=YADD0 (no change), 1=YADD1 (+1) |
| 19 | XSIGNSUB | X add becomes subtract (use with XADDPIX for right-to-left) |
| 20 | YSIGNSUB | Y add becomes subtract |

**WIDTH encoding (6-bit floating point):**
- Bits 9-12 (4 bits): unsigned exponent
- Bits 12-14 (3 bits): mantissa (with implicit leading 1)
- Value = (0b1_mmm) << exponent, where mmm = mantissa bits

Valid widths: 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64,
80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896,
1024, 1280, 1536, 1792, 2048, 2560, 3072, 3584.

Example: width=640. 640 = 1010_000000 binary = 0b1_010 << 7.
Mantissa = 010, exponent = 0111. WIDTH field = 0b0111_010 = bits [14:9].

**A2_FLAGS** is the same layout but:
- Bit 15 = MASK (enable Boolean AND masking with A2_MASK register)
- No fractional support, no clipping support

### B_CMD bit layout

| Bit | Name | Description |
|-----|------|-------------|
| 0 | SRCEN | Source data read enable |
| 1 | SRCENZ | Source Z read enable (ignored unless SRCEN set) |
| 2 | SRCENX | Source extra read (re-alignment / bit-to-pixel expansion) |
| 3 | DSTEN | Destination data read enable |
| 4 | DSTENZ | Destination Z read enable |
| 5 | DSTWRZ | Destination Z write enable |
| 6 | CLIP_A1 | Enable A1 clipping (inhibit writes outside A1_CLIP window) |
| 7 | -- | Diagnostic only, set to zero |
| 8 | UPDA1F | Update A1 fractional pointer between outer loop passes |
| 9 | UPDA1 | Update A1 step pointer between outer loop passes |
| 10 | UPDA2 | Update A2 step pointer between outer loop passes |
| 11 | DSTA2 | Destination is A2 (default: A1 is destination, A2 is source) |
| 12 | GOURD | Gouraud shading enable |
| 13 | ZBUFF | Z-buffer computation enable |
| 14 | TOPBEN | Top byte enable (carry into top byte of intensity in Gouraud, leave clear for CRY) |
| 15 | TOPNEN | Top nibble enable (carry into top nibble, leave clear for CRY) |
| 16 | PATDSEL | Pattern data select (use B_PATD as write data) |
| 17 | ADDDSEL | Add source + destination as write data. Source data is signed offset. Set TOPBEN+TOPNEN for 16-bit saturation; leave clear for CRY intensity-only adjustment. |
| 18-20 | ZMODE | Z comparator mode (only in 16bpp). 0=source < dest, 1=source = dest, 2=source > dest. Bits OR together: e.g. 3 = less-or-equal. All zero disables Z compare. |
| 21-24 | LFU | Logic Function Unit minterm (4-bit). See truth table below. |
| 25 | CMPDST | Compare destination data (else source) with pattern data for DCOMPEN |
| 26 | BCOMPEN | Bit comparator enable (bit-to-pixel expansion, 8bpp phrases only) |
| 27 | DCOMPEN | Data comparator enable (transparency: inhibit write on match) |
| 28 | BKGWREN | Background write enable (when comparator inhibits, write dest data back) |
| 29 | BUSHI | Bus priority high. HARDWARE BUG: should not be set on Jaguar Console. |
| 30 | SRCSHADE | Source shading (use IINC to modify intensity of data read from source address) |

### LFU truth table (bits 21-24)

The LFU implements a 4-bit minterm on Source (S) and Destination (D):
- Bit 21: output when NOT source AND NOT dest
- Bit 22: output when source AND NOT dest
- Bit 23: output when NOT source AND dest
- Bit 24: output when source AND dest

| LFU Value | Equate | Operation |
|-----------|--------|-----------|
| $000000 | LFU_CLEAR | All zeros |
| $100000 | LFU_NSAND | ~S & ~D |
| $200000 | LFU_SAND | S & ~D |
| $400000 | LFU_NOTS | ~S (complement source) |
| $600000 | LFU_REPLACE / LFU_S | S (copy source -- most common) |
| $800000 | LFU_NSAD | ~S & D |
| $A00000 | LFU_D | D (destination unchanged) |
| $600000 | LFU_NOTD | ~D |
| $900000 | LFU_N_SXORD | ~(S ^ D) (XNOR) |
| $C00000 | LFU_SAD | S & D |
| $E00000 | LFU_SORD | S | D |
| $F00000 | LFU_ONE | All ones |

---

## Object Processor

The OP is not register-programmed in the traditional sense -- it reads an
object list from DRAM starting at the address in OLP. Five object types exist:

| Type (bits 0-2) | Name | Phrases | Description |
|-----------------|------|---------|-------------|
| 0 | BITOBJ | 2 | Unscaled bitmap object |
| 1 | SCBITOBJ | 3 | Scaled bitmap object (third phrase: HSCALE/VSCALE) |
| 2 | GPUOBJ | 1 | GPU interrupt object (fires GPU int 3) |
| 3 | BRANCH | 1 | Conditional branch (test OP flag, VC compare, etc.) |
| 4 | STOP | 1 | Stop processing (fires 68K OP interrupt if enabled) |

### Bitmap Object -- First Phrase (64 bits)

| Bits | Field | Description |
|------|-------|-------------|
| 0-2 | TYPE | 0 = unscaled bitmap |
| 3-13 | YPOS | Vertical position (half-line number for first line) |
| 14-23 | HEIGHT | Number of data lines. Decremented each frame (written back). For scaled: HEIGHT = bitmap height - 1. |
| 24-42 | LINK | Address of next object (bits 3-21 of 24-bit address, phrase-aligned) |
| 43-63 | DATA | Address of pixel data (bits 3-23, phrase-aligned) |

### Bitmap Object -- Second Phrase

| Bits | Field | Description |
|------|-------|-------------|
| 0-11 | XPOS | X position (12-bit signed, -2048 to +2047). 0 = left edge of line buffer. |
| 12-14 | DEPTH | Bits per pixel: 0=1, 1=2, 2=4, 3=8, 4=16, 5=32 |
| 15-17 | PITCH | Data phrase gap (8 * PITCH added to address between phrases) |
| 18-27 | DWIDTH | Data width in phrases (address stride for next line) |
| 28-37 | IWIDTH | Image width in phrases (for clipping; must be non-zero) |
| 38-44 | INDEX | Palette index offset (for 1-8 bpp: top 7-4 bits of palette address) |
| 45 | REFLECT | Draw right-to-left |
| 46 | RMW | Read-modify-write (additive blend with line buffer) |
| 47 | TRANS | Colour 0 is transparent |
| 48 | RELEASE | Release bus between data fetches (set for low-res objects) |
| 49-54 | FIRSTPIX | First pixel to display (sub-phrase clipping) |
| 55-63 | -- | Unused, write zeros |

### Scaled Bitmap -- Third Phrase

| Bits | Field | Description |
|------|-------|-------------|
| 0-7 | HSCALE | Horizontal scale (3.5 fixed-point). 1.0 = $20. |
| 8-15 | VSCALE | Vertical scale (3.5 fixed-point). 1.0 = $20. |
| 16-63 | REMAINDER | Internal use (written back by OP) |

---

## JERRY Registers ($F10000-$F1FFFF)

### Clock Dividers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| **$F10010** | **CLK1** | WO | Processor clock divider (10-bit). PLL ratio: freq = (N+1) * CHRDIV. Leave as reset value. |
| **$F10012** | **CLK2** | WO | Video clock divider (10-bit). PLL ratio: freq = (N+1) * CHRDIV. Leave as reset value (zero). |
| **$F10014** | **CLK3** | WO | Chroma clock divider (6-bit + enable). Divides chroma oscillator by N+1. Bit 15: enable VCLK output. Leave as reset value. |

### Programmable Timers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F10000 | JPIT1 | WO | Timer 1 prescaler. Divides processor clock by N+1. Writing loads counter. |
| $F10002 | JPIT2 | WO | Timer 1 divider. Divides prescaler output by N+1. Writing loads counter. |
| $F10004 | JPIT3 | WO | Timer 2 prescaler |
| $F10006 | JPIT4 | WO | Timer 2 divider |

Timer frequency = ProcessorClock / ((JPIT1+1) * (JPIT2+1))

Both stages are down-counters loaded on write and when they reach zero. When
the divider reaches zero, it can interrupt DSP or 68000 (independently maskable).

GOTCHA: There are readable aliases at different addresses:

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F10036 | JPIT1_RB | RO | Timer 1 prescaler readback. DO NOT WRITE. |
| $F10038 | JPIT2_RB | RO | Timer 1 divider readback. DO NOT WRITE. |

Writing to $F10036/$F10038 does NOT arm the timer. Use $F10000/$F10002 to arm.

### JERRY Interrupt Control

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F10020 | JINTCTRL | RW | Jerry interrupt control register |

#### JINTCTRL bit layout

| Bit | Name (enable) | Name (clear) | Source |
|-----|---------------|--------------|--------|
| 0 | J_EXTENA | -- | External interrupt (EINT[0] input) |
| 1 | J_DSPENA | -- | DSP interrupt |
| 2 | J_TIM1ENA | -- | Timer 1 (sample rate) |
| 3 | J_TIM2ENA | -- | Timer 2 (tempo) |
| 4 | J_ASYNENA | -- | Asynchronous serial (UART) |
| 5 | J_SYNENA | -- | Synchronous serial (I2S) |
| 8 | -- | J_EXTCLR | Clear pending external |
| 9 | -- | J_DSPCLR | Clear pending DSP |
| 10 | -- | J_TIM1CLR | Clear pending Timer 1 |
| 11 | -- | J_TIM2CLR | Clear pending Timer 2 |
| 12 | -- | J_ASYNCLR | Clear pending async serial |
| 13 | -- | J_SYNCLR | Clear pending sync serial |

Reading bits 0-5 returns pending status.

### Asynchronous Serial (UART / ComLynx / MIDI)

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F10030 | ASIDATA | RW | Serial data. Read: received byte (bits 0-7, clears RBF). Write: transmit byte (bits 0-7). |
| $F10032 | ASICTRL | WO | Serial control. Bit 0: ODD parity. Bit 1: PAREN. Bit 2: TXOPOL. Bit 3: RXIPOL. Bit 4: TINTEN (TX int enable). Bit 5: RINTEN (RX int enable). Bit 6: CLRERR. Bit 14: TXBRK. |
| $F10032 | ASISTAT | RO | Serial status (same address, read). Bit 7: RBF. Bit 8: TBE. Bit 9: PE. Bit 10: FE. Bit 11: OE. Bit 13: SERIN. Bit 14: TXBRK. Bit 15: ERROR. |
| $F10034 | ASICLK | RW | Serial clock. Baud = SystemClock / (N+1) / 16. |

---

## DSP Registers ($F1A100-$F1A120)

All DSP registers are 32 bits wide. The DSP is structurally similar to the GPU
but resides inside Jerry, has 8 KB of local RAM (vs GPU's 4 KB), wave table
ROM, circular buffer support (ADDQMOD/SUBQMOD), and extended 40-bit
multiply/accumulate.

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F1A100 | D_FLAGS | RW | DSP flags (same structure as G_FLAGS but with 5 interrupt sources) |
| $F1A104 | D_MTXC | WO | Matrix control |
| $F1A108 | D_MTXA | WO | Matrix address |
| $F1A10C | D_END | WO | Data organisation |
| $F1A110 | D_PC | RW | DSP program counter |
| $F1A114 | D_CTRL | RW | DSP control/status (same layout as G_CTRL) |
| $F1A118 | D_MOD | RW | Modulo instruction mask (for ADDQMOD/SUBQMOD circular buffer addressing). Set high bits to 1, low bits to 0 for 2^n buffer size. |
| $F1A11C | D_REMAIN | RO | Division remainder |
| $F1A11C | D_DIVCTRL | WO | Division control. Bit 0: DIV_OFFSET. |
| $F1A120 | D_MACHI | RO | MAC accumulator high byte (bits 32-39 of 40-bit result, read after RESMAC) |

### DSP Memory

| Range | Size | Contents |
|-------|------|----------|
| $F1A000-$F1A1FF | 512 B | DSP control registers |
| $F1B000-$F1CFFF | 8 KB | DSP local RAM |
| $F1D000-$F1DFFF | 8 KB | Wave table ROM (8 x 128-entry x 16-bit, sign-extended to 32-bit on read) |

### Wave Table ROM contents

| Address | Name | Waveform |
|---------|------|----------|
| $F1D000 | ROM_TRI | Triangle wave |
| $F1D200 | ROM_SINE | Sine wave |
| $F1D400 | ROM_AMSINE | Amplitude-modulated sine |
| $F1D600 | ROM_12W | Sine + second harmonic |
| $F1D800 | ROM_CHIRP16 | Chirp (increasing frequency sine) |
| $F1DA00 | ROM_NTRI | Triangle + noise |
| $F1DC00 | ROM_DELTA | Spike (delta) |
| $F1DE00 | ROM_NOISE | White noise |

### D_FLAGS bit layout

| Bit(s) | Name | Description |
|--------|------|-------------|
| 0 | ZERO_FLAG | ALU zero |
| 1 | CARRY_FLAG | ALU carry |
| 2 | NEGA_FLAG | ALU negative |
| 3 | IMASK | Interrupt mask |
| 4 | D_CPUENA | Enable Int 0 (CPU) |
| 5 | D_I2SENA | Enable Int 1 (I2S / sync serial) |
| 6 | D_TIM1ENA | Enable Int 2 (Timer 1) |
| 7 | D_TIM2ENA | Enable Int 3 (Timer 2) |
| 8 | D_EXT0ENA | Enable Int 4 (External 0 / EINT[0]) |
| 9 | D_CPUCLR | Clear Int 0 latch |
| 10 | D_I2SCLR | Clear Int 1 latch |
| 11 | D_TIM1CLR | Clear Int 2 latch |
| 12 | D_TIM2CLR | Clear Int 3 latch |
| 13 | D_EXT0CLR | Clear Int 4 latch |
| 14 | REGPAGE | Select register bank 1 |

DSP interrupt vectors (offsets within local RAM at $F1B000):
- $F1B000: Int 0 -- CPU
- $F1B010: Int 1 -- I2S (sync serial transmit)
- $F1B020: Int 2 -- Timer 1
- $F1B030: Int 3 -- Timer 2
- $F1B040: Int 4 -- External 0

Note: JTRM page 79 also lists Int 5 (External 1) but D_FLAGS only has 5
enable/clear bit pairs (bits 4-8 / 9-13). The 6th interrupt source exists in
hardware but is not controllable via the flags register in standard revisions.

---

## Synchronous Serial Interface (I2S Audio)

These registers are in DSP local address space and can be accessed by the DSP
without external bus overhead.

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F1A148 | L_I2S / LTXD | WO | Left transmit data (16-bit, to DAC). Also aliases to L_DAC ($F1A148). |
| $F1A148 | LRXD | RO | Left receive data (16-bit, from I2S input) |
| $F1A14C | R_I2S / RTXD | WO | Right transmit data (16-bit, to DAC). Also aliases to R_DAC ($F1A14C). |
| $F1A14C | RRXD | RO | Right receive data |
| $F1A150 | SCLK | WO | Serial clock divider (8-bit). Freq = SystemClock / (2 * (N+1)). |
| $F1A150 | SSTAT | RO | Serial status. Bit 0: WS (word strobe state). Bit 1: Left (channel). |
| $F1A154 | SMODE | WO | Serial mode. Bit 0: INTERNAL (enable clock/WS output). Bit 2: WSEN (word strobe enable). Bit 3: RISING (interrupt on WS rising). Bit 4: FALLING (interrupt on WS falling). Bit 5: EVERYWORD (interrupt on every word MSB). |

GOTCHA: The R_DAC/L_DAC aliases and LTXD/RTXD names refer to the same
physical registers. The "right and left are swapped" note in the JTRM means
the DAC aliases use opposite naming from the I2S aliases.

Standard audio setup: SCLK configured for ~1.4 MHz bit clock (CD-quality
44.1 kHz x 16-bit x 2 channels). DSP I2S interrupt fires on word strobe,
DSP writes LTXD/RTXD with next sample pair.

---

## Joystick Registers

| Address | Name | R/W | Description |
|---------|------|-----|-------------|
| $F14000 | JOYSTICK | RW | Joystick register. **Read:** 16 joystick inputs (active low) when input buffers enabled. JOYLO asserted during read. **Write:** Low 8 bits latched to output. JOYL2 asserted during write. Bit 8: audio unmute (set to enable audio after reset). Bit 15: enable joystick outputs. JOYL3 = inverse of bit 15. |
| $F14002 | JOYBUTS | RW | Button register. **Read:** 4 fire button inputs (active low). JOYL1 asserted during read. |

Bit 0 of JOYSTICK is used by EEPROM cartridges. Do not rely on its readable
status -- it is random.

The joystick uses a 4-to-16 multiplexer scheme: write a column select value to
the low byte, then read back to get the row data for that column.

---

## General Purpose IO Decodes

| Range | Name | Description |
|-------|------|-------------|
| $F14800-$F14FFF | GPIO0 | Reserved |
| $F15000-$F15FFF | GPIO1 | Reserved |
| $F16000-$F16FFF | GPIO2 | Reserved |
| $F17000-$F177FF | GPIO3 | Reserved |
| $F17800-$F17BFF | GPIO4 | Reserved |
| $F17C00-$F17FFF | GPIO5 | Reserved |

These active-low chip selects are active in the corresponding address ranges.
Used by Jaguar CD (BUTCH controller is on GPIO0, address $F14800+).

---

## Bus Priority (highest to lowest)

1. Higher-priority daisy-chained bus master
2. Refresh
3. DSP at DMA priority
4. GPU at DMA priority
5. Blitter at high priority (BUSHI)
6. Object Processor
7. DSP at normal priority
8. CPU under interrupt
9. GPU at normal priority
10. Blitter at normal priority
11. CPU

---

## Video Timing Reference

| Parameter | PAL | NTSC |
|-----------|-----|------|
| System clock | 26.593900 MHz | 26.590906 MHz |
| Horizontal period | 64.0 us | 63.5555 us |
| H sync width | 4.7 us | 4.76 us |
| V sync width | 27.3 us | 29.26 us |
| Total lines (non-interlaced) | 624 | 524 |
| V sync pulses | 5 | 6 |
| V front porch | 12 lines | 12 lines |
| V back porch | 17 lines | 12 lines |

Standard game setup uses pixel divisor 4 (square pixels) giving ~332
overscanned pixels per line. Recommended visible area: 320 x 240 (NTSC) or
320 x 256 (PAL), using the standardized Jaguar Startup Code for register
initialization.

---

## Known Hardware Bugs (from JTRM "Hardware Bugs & Warnings")

1. **VDE comparison bug:** On Jaguar Console, VDE must be set to $FFFF and VC
   written with $FFFF before VDE comparison works reliably. Standard practice
   is to set VDE=$FFFF so the OP runs every line.

2. **DMAEN bug (G_FLAGS bit 15):** Setting DMAEN on Jaguar Console causes
   improper memory reads. Do not set this bit.

3. **BUSHI bug (B_CMD bit 29):** Setting BUSHI on Jaguar Console can cause
   system instability. Do not set this bit.

4. **BUS_HOG bug (G_CTRL bit 11):** Setting BUS_HOG on Jaguar Console starves
   other bus masters. Do not set this bit.

5. **GPU STORE + flags pipe-lining:** Writing to flag bits via STORE and testing
   them in the immediately following instruction does not work. Need 2+
   instructions gap (4+ for indexed STORE).

6. **Divide unit timing:** GPU/DSP divide completes in 16 ticks. Reading the
   quotient or starting another divide before completion inserts wait states.
