# JTRM GPU & DSP RISC Reference

> **Distilled, emulation-developer-focused reference** for the Atari Jaguar GPU
> and DSP RISC processors. Synthesized and reorganized from the Jaguar Technical
> Reference Manual (JTRM) for efficient LLM and developer consumption. This is
> NOT a direct copy of the JTRM -- content has been restructured, condensed, and
> annotated with emulation-specific notes and source-file cross-references.
> Always verify against the authoritative JTRM PDFs in `docs/atari-jaguar-1999/`
> for final hardware-accuracy decisions.

---

## Architecture Overview

GPU and DSP share the same RISC ISA (with DSP having a few extra instructions). Both are 32-bit RISC processors running at the full system clock (26.590906 MHz NTSC / 26.593900 MHz PAL).

- **GPU**: Located in TOM. 4 KB local RAM ($F03000-$F03FFF, 1024 x 32-bit). 64 registers (2 banks of 32, selected by REGPAGE bit in G_FLAGS). 5 interrupt sources.
- **DSP**: Located in JERRY. 8 KB local RAM ($F1B000-$F1BFFF). 8 KB wave table ROM ($F1D000-$F1DFFF, read-only). 64 registers (2 banks of 32). 6 interrupt sources.

Source: `src/tom/gpu.c`, `src/jerry/dsp.c`

---

## Instruction Encoding

All instructions are 16 bits: `opcode[15:10]`, `reg1[9:5]`, `reg2[4:0]`.

`reg1` is typically the source, `reg2` is typically the destination/result.

For immediate instructions (MOVEI, MOVEQ, etc.), the register fields encode immediate values or register numbers differently per instruction.

MOVEI is the only 32-bit instruction -- the 16-bit instruction word is followed by a 32-bit immediate (**low word first, then high word -- swapped order!**).

---

## Pipeline

4-stage pipeline: Decode -> Read Operands -> Compute -> Write-back.

**Score-boarding**: The processor tracks register dependencies. If an instruction reads a register that a previous instruction hasn't written back yet, the pipeline stalls (inserts wait states).

**IMPORTANT for emulation**: Jump/branch instructions interact with the pipeline. After a JUMP or JR, the instruction in the delay slot (the next instruction after the jump) ALWAYS executes. This is a single-instruction delay slot, not optional.

The `MOVEI` instruction occupies 3 pipeline slots (instruction word + 2 data words).

Source: `src/tom/gpu.c` (GPU pipeline implementation)

---

## Register File

64 registers total, in 2 banks of 32 (bank 0: r0-r31, bank 1: r32-r63). REGPAGE bit in FLAGS register selects active bank. Both banks are always accessible from the host CPU.

Register conventions (not enforced by hardware):
- r0-r1: Often used as scratch/accumulators
- r14-r15: Commonly used as stack pointer / link register by convention
- r30-r31: Often used for return addresses in interrupt handlers

---

## Interrupts

### GPU Interrupts (5 sources)

Vectors at offsets in GPU local RAM (vector = interrupt_number * 16 bytes):

| Int# | Vector Offset | Source           | G_FLAGS enable bit | G_FLAGS clear bit |
|------|---------------|------------------|--------------------|-------------------|
| 0    | $00           | CPU (external)   | 4                  | 9                 |
| 1    | $10           | DSP/JERRY        | 5                  | 10                |
| 2    | $20           | Timer (PIT)      | 6                  | 11                |
| 3    | $30           | Object Processor | 7                  | 12                |
| 4    | $40           | Blitter          | 8                  | 13                |

### DSP Interrupts (6 sources)

Vectors at offsets in DSP local RAM:

| Int# | Vector Offset | Source              | D_FLAGS enable bit | D_FLAGS clear bit |
|------|---------------|---------------------|--------------------|-------------------|
| 0    | $00           | CPU (external)      | 4                  | 10                |
| 1    | $10           | I2S (serial xmit)   | 5                  | 11                |
| 2    | $20           | Timer 1             | 6                  | 12                |
| 3    | $30           | Timer 2             | 7                  | 13                |
| 4    | $40           | External 0          | 8                  | 14                |
| 5    | $50           | External 1          | 9                  | 15                |

**IMASK bit** (bit 3 in FLAGS): Global interrupt mask. When set, all interrupts are masked.

When an interrupt fires: PC is saved, REGPAGE may swap (implementation-dependent), execution jumps to the vector address in local RAM. The ISR must clear the interrupt latch via the FLAGS clear bits.

Source: `src/tom/gpu.c`, `src/jerry/dsp.c`

---

## FLAGS Register Layout (G_FLAGS / D_FLAGS)

| Bit                   | Name      | Description                                               |
|-----------------------|-----------|-----------------------------------------------------------|
| 0                     | ZERO      | Zero flag (set by ALU ops)                                |
| 1                     | CARRY     | Carry flag                                                |
| 2                     | NEGA      | Negative flag                                             |
| 3                     | IMASK     | Interrupt mask (1=masked)                                 |
| 4-8(GPU) / 4-9(DSP)  | INT_ENAx  | Interrupt enable bits                                     |
| 9-13(GPU) / 10-15(DSP) | INT_CLRx | Write 1 to clear interrupt latch                         |
| 14                    | REGPAGE   | Register bank select (0=bank0, 1=bank1)                  |
| 15                    | DMAEN     | DMA enable -- GOTCHA: has hardware bug on some revisions  |

---

## CTRL Register Layout (G_CTRL / D_CTRL)

| Bit                       | Name        | Description                                               |
|---------------------------|-------------|-----------------------------------------------------------|
| 0                         | GPUGO/DSPGO | Start processor (1=running)                              |
| 1                         | CPUINT      | Cause CPU interrupt (write 1)                            |
| 2                         | FORCEINT0   | Force interrupt 0                                        |
| 3                         | SINGLE_STEP | Enable single-step mode                                  |
| 4                         | SINGLE_GO   | Execute one instruction (in single-step)                 |
| 6-10(GPU) / 6-11(DSP)    | INT_LATx    | Interrupt latch status (read-only)                       |
| 11                        | BUS_HOG     | Keep bus between accesses (reduces latency, starves others) |
| 12-15                     | VERSION     | Hardware version (read-only)                             |

---

## Instruction Set

All instructions execute in 1 cycle unless noted.

### Data Movement

| Opcode | Mnemonic | Operation                     | Cycles    | Notes                                                          |
|--------|----------|-------------------------------|-----------|----------------------------------------------------------------|
| 38     | MOVEI    | Rn <- 32-bit immediate        | 3         | Only 32-bit instruction (low word, high word after opcode)     |
| 35     | MOVEFA   | Rn <- alternate bank Rm       | 1         | Move from alternate register bank                              |
| 36     | MOVETA   | alt Rn <- Rm                  | 1         | Move to alternate register bank                                |
| 34     | MOVE     | Rn <- Rm                      | 1         | Register to register                                           |
| 41     | MOVEQ    | Rn <- quick (0-31)            | 1         | 5-bit immediate                                                |
| 51     | MOVEPC   | Rn <- PC                      | 1         | Get current PC                                                 |
| 37     | LOAD     | Rn <- (Rm)                    | **varies** | External memory = many cycles; local RAM = fast               |
| 43     | LOADW    | Rn <- word (Rm)               | varies    | 16-bit load                                                    |
| 44     | LOADB    | Rn <- byte (Rm)               | varies    | 8-bit load                                                     |
| 47     | LOADP    | Rn:Rn+1 <- phrase (Rm)        | varies    | 64-bit load (uses HIDATA for upper 16 bits)                    |
| 42     | STORE    | (Rn) <- Rm                    | varies    | 32-bit store                                                   |
| 45     | STOREW   | (Rn) <- Rm (word)             | varies    | 16-bit store                                                   |
| 46     | STOREB   | (Rn) <- Rm (byte)             | varies    | 8-bit store                                                    |
| 48     | STOREP   | (Rn) <- Rm:Rm+1 phrase        | varies    | 64-bit store                                                   |

### Arithmetic

| Opcode | Mnemonic | Operation                     | Flags | Notes                              |
|--------|----------|-------------------------------|-------|------------------------------------|
| 0      | ADD      | Rn <- Rn + Rm                 | ZNC   |                                    |
| 1      | ADDC     | Rn <- Rn + Rm + C             | ZNC   | Add with carry                     |
| 2      | ADDQ     | Rn <- Rn + quick(1-32)        | ZNC   | 5-bit immediate (0 encodes 32)     |
| 3      | ADDQT    | Rn <- Rn + quick              | --    | Add quick, no flags                |
| 4      | SUB      | Rn <- Rn - Rm                 | ZNC   |                                    |
| 5      | SUBC     | Rn <- Rn - Rm - C             | ZNC   | Subtract with carry                |
| 6      | SUBQ     | Rn <- Rn - quick(1-32)        | ZNC   |                                    |
| 7      | SUBQT    | Rn <- Rn - quick              | --    | Subtract quick, no flags           |
| 8      | NEG      | Rn <- -Rn                     | ZNC   | Two's complement negate            |
| 22     | ABS      | Rn <- |Rn|                    | ZNC   | Absolute value                     |
| 15     | CMP      | Rn - Rm (flags only)          | ZNC   | Compare, no writeback              |
| 16     | CMPQ     | Rn - quick (flags only)       | ZNC   | Quick compare (signed -16..+15)    |

### Logic

| Opcode | Mnemonic | Operation            | Flags |
|--------|----------|----------------------|-------|
| 9      | AND      | Rn <- Rn & Rm        | ZN    |
| 10     | OR       | Rn <- Rn \| Rm       | ZN    |
| 11     | XOR      | Rn <- Rn ^ Rm        | ZN    |
| 12     | NOT      | Rn <- ~Rn            | ZN    |
| 13     | BTST     | Test bit Rm of Rn    | Z     |
| 14     | BSET     | Set bit Rm of Rn     | ZN    |
| 15     | BCLR     | Clear bit Rm of Rn   | ZN    |

### Shift

| Opcode | Mnemonic | Operation                 | Flags | Notes                                   |
|--------|----------|---------------------------|-------|-----------------------------------------|
| 23     | SH       | Rn shift by Rm            | ZNC   | Positive=left, negative=right           |
| 24     | SHA      | Rn arith shift by Rm      | ZNC   | Arithmetic (sign-extending) shift       |
| 25     | SHARQ    | Rn >>a quick              | ZNC   | Arithmetic right shift by immediate     |
| 26     | SHLQ     | Rn << quick(1-32)         | ZNC   | Left shift by immediate                 |
| 27     | SHRQ     | Rn >> quick(1-32)         | ZNC   | Logical right shift by immediate        |
| 28     | ROR      | Rn rotate right by Rm     | ZNC   | Rotate right                            |
| 29     | RORQ     | Rn rotate right by quick  | ZNC   | Rotate right by immediate               |

### Multiply / MAC

| Opcode | Mnemonic | Operation                     | Notes                                      |
|--------|----------|-------------------------------|--------------------------------------------|
| 17     | MULT     | Rn <- Rn * Rm                 | 16x16->32 signed multiply                  |
| 18     | IMULT    | Rn <- Rn * Rm                 | 16x16->32 signed (same as MULT on Jaguar)  |
| 19     | IMULTN   | Rn * Rm -> ACC (no writeback) | Multiply, result to accumulator             |
| 20     | RESMAC   | Rn <- ACC                     | Read MAC accumulator result                 |
| 21     | IMACN    | ACC += Rn * Rm                | MAC accumulate, no writeback                |

The MAC unit enables systolic matrix operations. IMULTN starts a chain, IMACN accumulates, RESMAC reads the result.

**For DSP**: The MAC accumulator is 40 bits (D_MACHI at $F1A120 holds bits 32-39). This prevents overflow during audio DSP chains.

Source: `src/jerry/dsp_acc40.h`, `test/test_dsp_mac40.c`

### Branch / Jump

| Opcode | Mnemonic       | Operation               | Notes                                              |
|--------|----------------|-------------------------|----------------------------------------------------|
| 52     | JR             | PC <- PC + offset       | Relative jump (signed offset). ALWAYS has 1 delay slot. |
| 53     | JUMP           | PC <- (Rn)              | Absolute jump. ALWAYS has 1 delay slot.            |
| --     | JR cc, label   | Conditional relative    | cc = condition code                                |
| --     | JUMP cc, (Rn)  | Conditional absolute    |                                                    |

Condition codes: `T` (always), `NE` (Z=0), `EQ` (Z=1), `CC`/`HS` (C=0), `CS`/`LO` (C=1), `PL` (N=0), `MI` (N=1)

### DSP-Only Instructions

| Opcode | Mnemonic | Operation                          | Notes                              |
|--------|----------|------------------------------------|------------------------------------|
| 63     | ADDQMOD  | Rn <- (Rn + quick) MOD D_MOD      | Circular buffer increment          |
| 32     | SUBQMOD  | Rn <- (Rn - quick) MOD D_MOD      | Circular buffer decrement          |
| 33     | SAT16S   | Rn <- clamp(Rn, -32768, 32767)    | Signed 16-bit saturation           |
| --     | SAT32S   | Rn <- clamp(Rn, -2^31, 2^31-1)    | Signed 32-bit saturation           |

ADDQMOD/SUBQMOD use D_MOD ($F1A118) as the modulo value. Essential for circular audio buffers.

### Miscellaneous

| Opcode | Mnemonic | Operation                                                        |
|--------|----------|------------------------------------------------------------------|
| 39     | MMULT    | Matrix multiply (uses MTXC, MTXA)                                |
| 57     | DIV      | Rn <- Rn / Rm (uses DIVCTRL, result in REMAIN for remainder)    |
| 61     | PACK     | Pack 2x16->CRY or unpack CRY->2x16                              |
| 62     | UNPACK   | Reverse of PACK                                                  |
| 54     | NOP      | No operation                                                     |

---

## Wave Table ROM ($F1D000-$F1DFFF)

8 waveforms, each 256 x 32-bit samples:

| Offset | Waveform                 |
|--------|--------------------------|
| $000   | Triangle                 |
| $400   | Sine                     |
| $800   | Amplitude-modulated sine |
| $C00   | 12-waveform composite    |
| $1000  | Chirp (frequency sweep)  |
| $1400  | Noisy triangle           |
| $1800  | Delta (impulse)          |
| $1C00  | White noise              |

Source: `src/jerry/wavetable.c`

---

## Known Emulation Gotchas

1. **Delay slots**: JR/JUMP always execute the next instruction before branching. Missing this breaks virtually all GPU/DSP code.
2. **MOVEI word order**: The 32-bit immediate after MOVEI is stored LOW word first, HIGH word second. Getting this wrong corrupts all address loads.
3. **Score-boarding stalls**: The pipeline stalls on RAW hazards. Not modeling this makes code run too fast and breaks timing-dependent programs.
4. **BUS_HOG**: When set in CTRL, the processor doesn't release the bus between accesses. This starves the 68K and other bus masters. Games use this deliberately for performance-critical GPU code.
5. **DMAEN bug**: Setting DMAEN in G_FLAGS can cause improper reads on some hardware revisions.
6. **40-bit MAC (DSP only)**: The DSP accumulator is 40 bits. D_MACHI holds overflow bits. Truncating to 32 bits causes audio distortion in games that use long MAC chains.
7. **External memory access timing**: LOAD/STORE to external memory takes many more cycles than local RAM. The exact timing depends on bus contention.
