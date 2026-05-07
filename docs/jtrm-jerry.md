# JTRM JERRY Subsystem Reference

> **Distilled, emulation-developer-focused reference** for the Atari Jaguar JERRY
> subsystem. Synthesized and reorganized from the Jaguar Technical Reference
> Manual (JTRM) for efficient LLM and developer consumption. This is NOT a
> direct copy of the JTRM -- content has been restructured, condensed, and
> annotated with emulation-specific notes and source-file cross-references.
> Always verify against the authoritative JTRM PDFs in `docs/atari-jaguar-1999/`
> for final hardware-accuracy decisions.

---

## Overview

JERRY is the Jaguar's audio and I/O subsystem. Contains: DSP, DAC, two PIT
timers, synchronous serial (I2S for audio), asynchronous serial (UART for
ComLynx/MIDI), joystick interface, EEPROM interface, and clock dividers.

JERRY occupies the address range `$F10000`--`$F1FFFF` in the Jaguar memory map.
DSP local RAM lives at `$F1B000`--`$F1CFFF` (8 KB). Wavetable ROM occupies
`$F1D000`--`$F1DFFF` (read-only, 8 waveforms of 256 x 16-bit samples each).

Source: `src/jerry/jerry.c`

---

## Programmable Interrupt Timers (PIT)

Two identical timers, each with a prescaler and divider register. Both
auto-reload after firing; the interrupt latch must be manually cleared.

### Timer Registers

**Writable (arm the timer):**

| Address   | Name  | R/W | Description                       |
|-----------|-------|-----|-----------------------------------|
| `$F10000` | JPIT1 | WO  | Timer 1 prescaler (write to arm)  |
| `$F10002` | JPIT2 | WO  | Timer 1 divider (write to arm)    |
| `$F10004` | JPIT3 | WO  | Timer 2 prescaler                 |
| `$F10006` | JPIT4 | WO  | Timer 2 divider                   |

NOTE: The register header comment in `jerry.c` lists JPIT4 at `$F10008`, but
the actual write handler (`JERRYWriteWord`, switch on `offset & 0x07`) uses
case 6 (`$F10006`). The implementation is correct; the comment is wrong.

**Read-only aliases (readback only -- do NOT write to these):**

| Address   | Name  | R/W | Description                       |
|-----------|-------|-----|-----------------------------------|
| `$F10036` | JPIT1 | RO  | Timer 1 prescaler readback        |
| `$F10038` | JPIT2 | RO  | Timer 1 divider readback          |
| `$F1003A` | JPIT3 | RO  | Timer 2 prescaler readback        |
| `$F1003C` | JPIT4 | RO  | Timer 2 divider readback          |

### Timer Formula

```
period_us   = (prescaler + 1) * (divider + 1) / system_clock_MHz
frequency   = system_clock / ((prescaler + 1) * (divider + 1))
```

Where system_clock = 26.590906 MHz (NTSC) or 26.593900 MHz (PAL).

Worked example: prescaler = 10, divider = 100

```
period = 11 * 101 / 26.590906 = 41.78 us  -->  ~23,936 Hz
```

**CRITICAL: PIT runs at the FULL system clock (26.59 MHz), NOT half.** This
was historically wrong in the source code and broke Doom/Rayman music
(PRs #134/#141). The JTRM is unambiguous: PIT counters decrement at the full
processor clock rate. If you see `system_clock / 2` in PIT calculations, it
is a bug.

**GOTCHA:** Writing to `$F10036`--`$F1003C` does NOT arm the timer -- those
are read-only readback aliases. You must write to `$F10000`--`$F10006` to
actually configure the timers.

### Timer Interrupts

- Timer 1 fires JERRY interrupt source `IRQ2_TIMER1` (bit 2 in JINTCTRL enable mask).
- Timer 2 fires JERRY interrupt source `IRQ2_TIMER2` (bit 3 in JINTCTRL enable mask).
- Timer 1 also fires DSP interrupt `DSPIRQ_TIMER0` (DSP int #2, vector `$20`).
- Timer 2 also fires DSP interrupt `DSPIRQ_TIMER1` (DSP int #3, vector `$30`).
- JERRY interrupts route to the 68K via TOM INT1 as the DSP/JERRY source (bit 4, `IRQ_DSP`, mask `$10`).

Source: `src/jerry/jerry.c` (`JERRYPIT1Callback`, `JERRYPIT2Callback`)

---

## Interrupt Control (JINTCTRL `$F10020`)

Split read/write register. Low byte = enable mask (RW). High byte = clear
pending (WO, write 1 to clear).

### Enable Bits (low byte, RW)

| Bit | Name         | Enum           | Description                      |
|-----|--------------|----------------|----------------------------------|
| 0   | J_EXTENA     | `IRQ2_EXTERNAL` (0x01) | External interrupt         |
| 1   | J_TIM1ENA    | `IRQ2_TIMER1`   (0x04) | Timer 1 (sample)           |
| 2   | J_TIM2ENA    | `IRQ2_TIMER2`   (0x08) | Timer 2 (tempo)            |
| 3   | J_ASYNENA    | `IRQ2_ASI`      (0x10) | Async serial (UART)        |
| 4   | J_SYNENA     | `IRQ2_SSI`      (0x20) | Sync serial (I2S)          |

NOTE: The register comment in `jerry.c` maps 5 bits (0--4) for enable.
The `IRQ2_` enum in `jerry.h` defines 6 values including `IRQ2_DSP` (0x02),
but the register comment does not show a corresponding enable bit for it.
The actual write handler takes `data & 0xFF` as the mask, so all 8 low bits
are stored.

### Clear Bits (high byte, WO)

| Bit | Name         | Description                          |
|-----|--------------|--------------------------------------|
| 8   | J_EXTCLR     | Clear external interrupt pending     |
| 9   | J_TIM1CLR    | Clear timer 1 pending                |
| 10  | J_TIM2CLR    | Clear timer 2 pending                |
| 11  | J_ASYNCLR    | Clear UART pending                   |
| 12  | J_SYNCLR     | Clear I2S pending                    |

Write pattern: set enable bits in low byte to arm interrupt sources; write 1
to high-byte bits to acknowledge/clear pending interrupts.

### JERRY-to-TOM Interrupt Routing

All JERRY interrupts merge into a single "DSP interrupt" line that connects
to TOM INT1 bit 4 (`IRQ_DSP`, mask `$0010`). The 68K sees JERRY interrupts
as IPL 2 via TOM. Clearing the interrupt requires clearing BOTH the JERRY
JINTCTRL pending bit AND acknowledging TOM's INT1 DSP pending bit.

Source: `src/jerry/jerry.c`, `src/tom/tom.h` (`IRQ_DSP` enum)

---

## Clock Dividers

| Address   | Name | Width     | Description                                      |
|-----------|------|-----------|--------------------------------------------------|
| `$F10010` | CLK1 | 10 bits   | Processor clock divider                          |
| `$F10012` | CLK2 | 10 bits   | Video clock divider                              |
| `$F10014` | CLK3 | 6 bits    | Chroma clock divider                             |

Formula for all three:

```
output_frequency = system_clock / (2 * (N + 1))
```

CLK1 is commonly used to generate the I2S bit clock (SCLK). CLK2/CLK3 are
used for video timing adjustments.

Source: `src/jerry/jerry.c`

---

## Synchronous Serial (I2S) -- Audio DAC Interface

The primary audio output path. The DSP writes samples to LTXD/RTXD; the I2S
interface serializes them to the external DAC.

### Registers

| Address   | Name  | R/W | Description                                  |
|-----------|-------|-----|----------------------------------------------|
| `$F1A148` | LTXD  | WO  | Left channel transmit data (16-bit)          |
| `$F1A148` | LRXD  | RO  | Left channel receive data (same address)     |
| `$F1A14C` | RTXD  | WO  | Right channel transmit data (16-bit)         |
| `$F1A14C` | RRXD  | RO  | Right channel receive data (same address)    |
| `$F1A150` | SCLK  | WO  | Serial clock divider (8 bits)                |
| `$F1A150` | SSTAT | RO  | Serial status (same address)                 |
| `$F1A154` | SMODE | WO  | Serial mode configuration                    |

NOTE: The register header comment in `jerry.c` labels `$F1A148` as "R_DAC"
and `$F1A14C` as "L_DAC" -- this is swapped. The `#define` values in `dac.c`
(`LTXD=0xF1A148`, `RTXD=0xF1A14C`) are authoritative.

### SSTAT Bits (read at `$F1A150`)

| Bit | Name | Description                              |
|-----|------|------------------------------------------|
| 0   | WS   | Word strobe status                       |
| 1   | left | (undocumented)                           |

### SMODE Bits (write at `$F1A154`)

| Bit | Name       | Define           | Description                          |
|-----|------------|------------------|--------------------------------------|
| 0   | INTERNAL   | `SMODE_INTERNAL` | Enable JERRY internal serial clock   |
| 1   | MODE       | `SMODE_MODE`     | Mode select                          |
| 2   | WSEN       | `SMODE_WSEN`     | Enable word strobes                  |
| 3   | RISING     | `SMODE_RISING`   | Interrupt on rising WS edge          |
| 4   | FALLING    | `SMODE_FALLING`  | Interrupt on falling WS edge         |
| 5   | EVERYWORD  | `SMODE_EVERYWORD`| Interrupt on MSB of every word       |

### Serial Clock Formula

```
SCLK_frequency = system_clock / (2 * (SCLK_value + 1))
I2S_sample_rate = SCLK_frequency / 32
```

The I2S frame is 32 bit-clocks per stereo sample (16 bits left + 16 bits
right). So:

```
sample_rate = system_clock / (2 * (SCLK_value + 1) * 32)
            = system_clock / (64 * (SCLK_value + 1))
```

Default SCLK value in the emulator: 19 (~20.8 kHz). BIOS typically sets
SCLK = 8 for ~46.2 kHz (close to CD-quality 44.1 kHz).

### Audio Pipeline

The typical audio path in Jaguar games:

1. DSP runs audio mixing code in its local RAM (`$F1B000`--`$F1CFFF`).
2. I2S interrupt (`DSPIRQ_SSI`, DSP int #1, vector `$10`) fires when the
   DAC needs a new sample.
3. DSP ISR writes left/right samples to LTXD (`$F1A148`) / RTXD (`$F1A14C`).
4. I2S hardware serializes and clocks data out to the external DAC.

In the emulator, the I2S callback (`JERRYI2SCallback`) fires at the I2S
sample rate. When `SMODE_INTERNAL` is set, the interval is calculated as:

```c
jerryI2SCycles = 32 * (2 * (*sclk + 1));
usecs = jerryI2SCycles * RISC_CYCLE_IN_USEC;
```

When JERRY is slave to an external word clock (CD-ROM mode), the callback
fires at a fixed ~44.1 kHz (22.675737 us period).

Source: `src/jerry/jerry.c` (`JERRYI2SCallback`), `src/jerry/dac.c`,
`src/jerry/dac.h`, `libretro.c` (audio callback)

---

## Asynchronous Serial (UART)

Used for ComLynx networking and MIDI. Few games use this; low priority for
emulation accuracy.

### Registers

| Address   | Name    | R/W | Description                                 |
|-----------|---------|-----|---------------------------------------------|
| `$F10030` | ASIDATA | RW  | Serial data (8 bits)                        |
| `$F10032` | ASICTRL | WO  | Serial control                              |
| `$F10032` | ASISTAT | RO  | Serial status (same address)                |
| `$F10034` | ASICLK  | RW  | Baud rate clock divider                     |

### ASICTRL Bits (write)

| Bit | Name   | Description                              |
|-----|--------|------------------------------------------|
| 0   | ODD    | Odd parity select                        |
| 1   | PAREN  | Parity enable                            |
| 2   | TXOPOL | Transmitter output polarity              |
| 3   | RXIPOL | Receiver input polarity                  |
| 4   | TINTEN | Enable transmitter interrupts            |
| 5   | RINTEN | Enable receiver interrupts               |
| 6   | CLRERR | Clear error flags                        |
| 14  | TXBRK  | Transmit break                           |

### ASISTAT Bits (read)

| Bit | Name   | Description                              |
|-----|--------|------------------------------------------|
| 0   | ODD    | Odd parity (readback)                    |
| 1   | PAREN  | Parity enable (readback)                 |
| 2   | TXOPOL | TX output polarity (readback)            |
| 3   | RXIPOL | RX input polarity (readback)             |
| 4   | TINTEN | TX interrupt enable (readback)           |
| 7   | RBF    | Receive buffer full                      |
| 8   | TBE    | Transmit buffer empty                    |
| 9   | PE     | Parity error                             |
| 10  | FE     | Framing error                            |
| 11  | OE     | Overrun error                            |
| 13  | SERIN  | Serial input level                       |
| 14  | TXBRK  | Transmit break (readback)                |
| 15  | ERROR  | OR of PE, FE, OE                         |

### Baud Rate Formula

```
baud_rate = system_clock / (16 * (N + 1))
```

Source: `src/jerry/jerry.c`

---

## Joystick Interface

### Registers

| Address   | Name     | R/W | Description                               |
|-----------|----------|-----|-------------------------------------------|
| `$F14000` | JOYSTICK | RW  | Joystick/keypad data (active-low, muxed)  |
| `$F14002` | JOYBUTS  | RO  | Fire buttons A, B, C, Option, Pause       |

The joystick register uses a multiplexer: write output bits to select which
row of the keypad/direction matrix to read, then read back the result. Bit 15
of the write enables joystick outputs; bits 7--0 are the output data that
selects the matrix row.

Button and direction signals are active-low. The emulator handles the
mux logic in the input polling layer.

Source: `src/jerry/joystick.c`

---

## EEPROM Interface

128 bytes of serial EEPROM for game saves. Directly memory-mapped via the
joystick port GPIO bits at `$F14000`--`$F14003`. The protocol is bit-banged
serial (I2C or Microwire, depending on the EEPROM chip).

The EEPROM shares the joystick port address range -- reads/writes to
`$F14000`--`$F14003` are dispatched to both the joystick handler and the
EEPROM handler simultaneously in the emulator.

Source: `src/jerry/eeprom.c`

---

## Wavetable ROM

JERRY contains 8 read-only waveform tables in ROM, each 256 x 16-bit
samples (512 bytes). Total: 4 KB at `$F1D000`--`$F1DFFF`.

| Address   | Name         | Description                              |
|-----------|--------------|------------------------------------------|
| `$F1D000` | ROM_TRI      | Triangle wave                            |
| `$F1D200` | ROM_SINE     | Full sine wave                           |
| `$F1D400` | ROM_AMSINE   | Amplitude modulated sine wave            |
| `$F1D600` | ROM_12W      | Sine wave + second order harmonic        |
| `$F1D800` | ROM_CHIRP16  | Chirp                                    |
| `$F1DA00` | ROM_NTRI     | Triangle wave with noise                 |
| `$F1DC00` | ROM_DELTA    | Spike (delta function)                   |
| `$F1DE00` | ROM_NOISE    | White noise                              |

These are loaded into `jerry_ram_8` at offset `$D000` during init. Writes to
this range are silently ignored (ROM protection).

Source: `src/jerry/jerry.c` (`JERRYInit`), `src/jerry/wavetable.c`

---

## DSP (Quick Reference)

The DSP is documented in detail in `docs/jtrm-gpu-dsp.md`. Key JERRY-specific
facts:

- DSP local RAM: `$F1B000`--`$F1CFFF` (8 KB).
- DSP control registers: `$F1A100`--`$F1A123`.
- 6 interrupt sources (CPU, I2S, Timer1, Timer2, Ext0, Ext1).
- Shares the RISC ISA with GPU (plus a few DSP-only instructions).
- Runs at full system clock (26.59 MHz).

### DSP Control Registers (Summary)

| Address   | Name      | Description                                  |
|-----------|-----------|----------------------------------------------|
| `$F1A100` | D_FLAGS   | DSP flags (ALU flags, interrupt enable/clear) |
| `$F1A104` | D_MTXC    | Matrix control                               |
| `$F1A108` | D_MTXA    | Matrix address                               |
| `$F1A10C` | D_END     | Data organization (endianness)               |
| `$F1A110` | D_PC      | DSP program counter                          |
| `$F1A114` | D_CTRL    | DSP control/status (go, single-step, latches)|
| `$F1A118` | D_MOD     | Modulo instruction mask                      |
| `$F1A11C` | D_REMAIN  | Divide unit remainder (RO) / D_DIVCTRL (WO) |
| `$F1A120` | D_MACHI   | Multiply-accumulate high bits (RO)           |

Source: `src/jerry/dsp.c`, `src/jerry/dsp.h`, `docs/jtrm-gpu-dsp.md`

---

## Known Emulation Gotchas

1. **PIT clock rate**: Full system clock (26.59 MHz), NOT half. Getting this
   wrong desynchronizes music timing in many games (Doom, Rayman, etc.).

2. **JPIT readback aliases**: `$F10036`--`$F1003C` are read-only. Writing to
   them is a no-op. Only `$F10000`--`$F10006` arm the timers.

3. **JPIT4 address comment bug**: The register comment in `jerry.c` says
   JPIT4 (Timer 2 divider) is at `$F10008`, but the write handler uses
   `offset & 0x07 == 6`, i.e. `$F10006`. The implementation is correct.

4. **JERRY-to-TOM interrupt routing**: JERRY doesn't directly interrupt the
   68K. It goes through TOM's INT1 DSP bit (`IRQ_DSP`, bit 4). Clearing the
   interrupt requires clearing both JERRY's JINTCTRL pending bit AND TOM's
   INT1 DSP pending bit.

5. **L/R DAC register comment swap**: The register header comment in `jerry.c`
   labels `$F1A148` as "R_DAC" (right) and `$F1A14C` as "L_DAC" (left).
   The `#define` values in `dac.c` have `LTXD=0xF1A148` (left) and
   `RTXD=0xF1A14C` (right). The `dac.c` definitions are authoritative.

6. **I2S word swap**: There may be byte-order issues when the DSP writes
   16-bit audio samples to LTXD/RTXD on a little-endian host. The emulator
   must handle the endian swap correctly via `GET16`/`SET16` macros.

7. **Timer re-arming**: After a timer interrupt fires, the prescaler and
   divider auto-reload (the timer keeps running). But the interrupt latch
   must be manually cleared via the JINTCTRL high-byte clear bits. The
   emulator re-arms via `JERRYResetPIT1()`/`JERRYResetPIT2()` at the end of
   each callback.

8. **I2S slave mode (CD-ROM)**: When `SMODE_INTERNAL` is not set, JERRY is
   slave to an external word clock. The emulator falls back to a fixed
   ~44.1 kHz interrupt rate (22.675737 us) and checks `ButchIsReadyToSend()`
   for CD audio data.

9. **EEPROM timing**: The bit-banged EEPROM protocol is timing-sensitive.
   Some games have tight polling loops that expect specific response timing.

10. **Wavetable ROM protection**: Writes to `$F1D000`--`$F1DFFF` must be
    silently dropped. The ROM data is copied into `jerry_ram_8` at init, so
    the write guard in `JERRYWriteByte`/`JERRYWriteWord` must come before the
    fallthrough `jerry_ram_8[offset] = data` line.

---

## Source File Cross-References

| Domain              | Files                                              |
|---------------------|----------------------------------------------------|
| JERRY core          | `src/jerry/jerry.c`, `src/jerry/jerry.h`           |
| DSP                 | `src/jerry/dsp.c`, `src/jerry/dsp.h`               |
| DAC / I2S           | `src/jerry/dac.c`, `src/jerry/dac.h`               |
| EEPROM              | `src/jerry/eeprom.c`, `src/jerry/eeprom.h`         |
| Joystick            | `src/jerry/joystick.c`, `src/jerry/joystick.h`     |
| Wavetable ROM       | `src/jerry/wavetable.c`, `src/jerry/wavetable.h`   |
| Clock constants     | `src/core/jaguar.h` (`RISC_CLOCK_RATE_NTSC/PAL`)   |
| TOM interrupt defs  | `src/tom/tom.h` (`IRQ_DSP` enum)                   |
| Libretro audio      | `libretro.c` (audio callback)                      |
| GPU/DSP RISC ref    | `docs/jtrm-gpu-dsp.md`                             |
| Clock/timing ref    | `docs/jtrm-clocks-timing.md`                       |
