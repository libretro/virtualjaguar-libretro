# Jaguar Clock Hierarchy, Video Timing & PIT Reference

> **Distilled from the Jaguar Technical Reference Manual (JTRM).**
> This is NOT a verbatim copy of the JTRM. It is a reorganized,
> emulation-developer-focused reference optimized for efficient LLM and
> developer consumption. Authoritative PDFs live in
> `docs/atari-jaguar-1999/` (gitignored, copyrighted).

---

## Clock Hierarchy

The Jaguar derives every internal clock from a single crystal oscillator.
The frequency differs between NTSC and PAL systems.

| Clock domain | NTSC (MHz) | PAL (MHz) | Notes |
|---|---|---|---|
| System / processor clock | 26.590906 | 26.593900 | Master oscillator |
| 68000 CPU | 13.295453 | 13.296950 | system_clock / 2 |
| GPU (RISC) | 26.590906 | 26.593900 | Full system clock |
| DSP (RISC) | 26.590906 | 26.593900 | Full system clock |
| PIT timers | 26.590906 | 26.593900 | Full system clock -- see **Gotcha** below |

RISC cycle time (NTSC): 1 / 26.590906 MHz = **37.607 ns**

### Programmable Clock Dividers (JERRY)

JERRY exposes three general-purpose clock dividers used to generate I2S
SCLK, DAC sample rates, and other derived clocks.

| Register | Address | Formula |
|---|---|---|
| CLK1 (processor divider) | `$F10010` | system_clock / (2 * (N + 1)) |
| CLK2 (video divider) | `$F10012` | system_clock / (2 * (N + 1)) |
| CLK3 (chroma divider) | `$F10014` | system_clock / (2 * (N + 1)) |

Source: `src/jerry/jerry.c`

---

## Video Timing

### NTSC vs PAL Parameters

| Parameter | NTSC | PAL |
|---|---|---|
| Master clock | 26.590906 MHz | 26.593900 MHz |
| Horizontal period | 63.5555 us | 64.0 us |
| Horizontal sync width | 4.8390 us | 4.7 us |
| Back porch (colour burst to active) | 5.7 us | 5.6 us |
| Active display | 52.0 us | 52.0 us |
| Front porch | 1.0 us | 1.7 us |
| Total vertical lines | 524 (262.5 per field) | 625 (312.5 per field) |
| Vertical sync lines | 6 | 5 |
| Pre-equalizing pulses | 6 | 5 |
| Post-equalizing pulses | 6 | 5 |
| Vertical blanking lines | ~20 | ~24 |
| Active display lines | ~240 | ~256 |

Source: `src/tom/tom.c` (scanline timing, VMODE), `src/tom/op.c` (active display)

### Pixel Clock Divisor (VMODE Register)

The VMODE register selects a pixel divisor that determines how many
system clocks per pixel.

| Divisor | Overscanned pixels | Non-overscanned pixels | Typical use |
|---|---|---|---|
| 1 | 1330 | ~1040 | |
| 2 | 665 | ~520 | 640-wide modes |
| 3 | 443 | ~347 | |
| 4 | 332 | ~260 | **320-wide (square pixels)** |
| 5 | 266 | ~208 | |
| 6 | 221 | ~173 | |
| 7 | 190 | ~149 | |
| 8 | 166 | ~130 | |

Divisor 4 gives square pixels. Most games use divisor 4 (320-wide) or
divisor 2 (640-wide).

---

## PIT Timer Formulas

JERRY contains two identical Programmable Interrupt Timers.

### Registers

**Writable (arm the timer):**

| Register | Address | Description |
|---|---|---|
| JPIT1 | `$F10000` | Timer 1 prescaler (W) |
| JPIT2 | `$F10002` | Timer 1 divider (W) |
| JPIT3 | `$F10004` | Timer 2 prescaler (W) |
| JPIT4 | `$F10006` | Timer 2 divider (W) |

**Read-only aliases (readback only -- do NOT write to these):**

| Address | Description |
|---|---|
| `$F10036` | Timer 1 prescaler readback |
| `$F10038` | Timer 1 divider readback |

### Formula

```
period_us   = (prescaler + 1) * (divider + 1) / system_clock_MHz
frequency   = system_clock / ((prescaler + 1) * (divider + 1))
```

NTSC example:

```
period_us = (prescaler + 1) * (divider + 1) / 26.590906
```

Worked example: prescaler = 10, divider = 100

```
period = 11 * 101 / 26.590906 = 41.78 us  -->  ~23,936 Hz
```

### CRITICAL GOTCHA: PIT Clock Rate

> **The PIT clock is the FULL system clock (26.59 MHz), NOT half.**
>
> Source code comments historically stated "half system clock" which is
> **wrong**. This was incorrectly implemented in PRs #134 and #141 and
> broke Doom and Rayman music timing. The JTRM is unambiguous: PIT
> counters decrement at the full processor clock rate.
>
> If you see `system_clock / 2` in PIT calculations, it is a bug.

Source: `src/jerry/jerry.c`

---

## Memory Map Overview

| Address Range | Size | Contents |
|---|---|---|
| `$000000`--`$1FFFFF` | 2 MB | Main DRAM |
| `$200000`--`$3FFFFF` | 2 MB | DRAM mirror (optional 4 MB config) |
| `$800000`--`$DFFFFF` | 6 MB | Cartridge ROM |
| `$E00000`--`$E1FFFF` | 128 KB | Bootstrap ROM (optional) |
| `$F00000`--`$F0FFFF` | 64 KB | TOM registers |
| `$F10000`--`$F1FFFF` | 64 KB | JERRY registers |
| `$F14000`--`$F14003` | 4 B | Joystick interface |

### Key Memory Config Registers

| Register | Address | Purpose |
|---|---|---|
| MEMCON1 | `$F00000` | DRAM width (16/32/64-bit), speed, row size, BIGEND |
| MEMCON2 | `$F00002` | ROMHI (remap ROM high), additional timing |

Source: `src/core/vjag_memory.c` (header comment has full map),
`src/core/jaguar.c` (dispatch logic)

---

## Bus Priority

From highest to lowest priority:

| Priority | Bus master |
|---|---|
| 1 | Higher daisy-chain position |
| 2 | Memory refresh |
| 3 | DSP DMA |
| 4 | GPU DMA |
| 5 | Blitter (high priority -- `BUSHI` bit in `B_CMD`) |
| 6 | Object Processor |
| 7 | DSP (normal) |
| 8 | CPU interrupt acknowledge |
| 9 | GPU (normal) |
| 10 | Blitter (normal) |
| 11 | CPU (68000) |

The GPU `BUS_HOG` bit (`G_CTRL` bit 11) keeps the bus between accesses.
The DSP has an equivalent mechanism.

---

## Source File Cross-References

| Domain | Files |
|---|---|
| Clock definitions | `src/jerry/jerry.c` (PIT, CLK dividers), `src/core/jaguar.c` (system clock constant) |
| Video timing | `src/tom/tom.c` (scanline timing, VMODE), `src/tom/op.c` (active display) |
| Memory map | `src/core/vjag_memory.c`, `src/core/jaguar.c` |
| Byte-swap macros | `GET16` / `GET32` / `SET16` / `SET32` in memory headers (handle LE host byte-swap) |
