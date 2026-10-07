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
- **DSP**: Located in JERRY. 8 KB local RAM ($F1B000-$F1CFFF). Wave table ROM in the 4 KB window $F1D000-$F1DFFF (read-only; see Wave Table ROM below). 64 registers (2 banks of 32). 6 interrupt sources.

Source: JTRM Rev 8 pp.97-98 "Memory Map" / "Wave Table ROM" (DSP RAM range and wave window). Rest of this section:
Derived from: `src/tom/gpu.c`, `src/jerry/dsp.c` -- NOT verified against the JTRM

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

Derived from: `src/tom/gpu.c` (GPU pipeline implementation) -- NOT verified
against the JTRM

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

Source: JTRM Rev 8 p.59 "GPU Flag Register" (INT_ENA0-4 = bits 4-8, INT_CLR0-4 = bits 9-13; source allocation 0 CPU .. 4 Blitter).

### DSP Interrupts (6 sources)

Vectors at offsets in DSP local RAM:

| Int# | Vector Offset | Source              | D_FLAGS enable bit | D_FLAGS clear bit |
|------|---------------|---------------------|--------------------|-------------------|
| 0    | $00           | CPU (external)      | 4                  | 9                 |
| 1    | $10           | I2S (serial xmit)   | 5                  | 10                |
| 2    | $20           | Timer 1 (JPIT1/2)   | 6                  | 11                |
| 3    | $30           | Timer 2 (JPIT3/4)   | 7                  | 12                |
| 4    | $40           | External 0          | 8                  | 13                |
| 5    | $50           | External 1          | **16**             | **17**            |

Source: JTRM Rev 8 p.109 "DSP Flag Register" (INT_ENA0-4 = bits 4-8, INT_CLR0-4 = bits 9-13, INT_ENA5 = bit 16, INT_CLR5 = bit 17; bits 14/15 are REGPAGE/DMAEN, so int 5 cannot sit at 9/15 as an earlier version of this table said) and p.98 (six sources; the manual names ints 2/3 "timer 0/1", the table above uses the JERRY JPIT register naming). Code agrees: `src/jerry/dsp.c` `INT_ENA5 0x10000`, `INT_CLR5 0x20000`.

**IMASK bit** (bit 3 in FLAGS): Global interrupt mask. When set, all interrupts are masked.

When an interrupt fires: PC is saved, REGPAGE may swap (implementation-dependent), execution jumps to the vector address in local RAM. The ISR must clear the interrupt latch via the FLAGS clear bits.

Derived from: `src/tom/gpu.c`, `src/jerry/dsp.c` -- NOT verified against the JTRM

---

## FLAGS Register Layout (G_FLAGS / D_FLAGS)

| Bit                   | Name      | Description                                               |
|-----------------------|-----------|-----------------------------------------------------------|
| 0                     | ZERO      | Zero flag (set by ALU ops)                                |
| 1                     | CARRY     | Carry flag                                                |
| 2                     | NEGA      | Negative flag                                             |
| 3                     | IMASK     | Interrupt mask (1=masked)                                 |
| 4-8 (GPU and DSP)     | INT_ENA0-4 | Interrupt enable bits (overridden by IMASK)              |
| 9-13 (GPU and DSP)    | INT_CLR0-4 | Write 1 to clear interrupt latch (reads 0)               |
| 14                    | REGPAGE   | Register bank select (0=bank0, 1=bank1; IMASK forces bank 0) |
| 15                    | DMAEN     | LOAD/STORE at DMA bus priority. Must NOT be set (see Gotchas 5) |
| 16 (DSP only)         | INT_ENA5  | Enable DSP interrupt 5                                   |
| 17 (DSP only)         | INT_CLR5  | Clear DSP interrupt 5 latch                              |

Source: JTRM Rev 8 p.58 (GPU flag bits 0-2), p.59 (GPU bits 3-15), p.108-109 (DSP bits incl. 16/17).
Flags-write pipelining: v8 p.59/p.109 says leave "at least one other instruction" between a flag-setting STORE
and a flag-dependent instruction; Software Reference v2.4 p.46 (PDF p.48) says two, or four for an indexed STORE;
Hardware Bugs & Warnings (26 Apr 1995) p.3 #11 says two NOPs. Revisions disagree; later documents are stricter.

---

## CTRL Register Layout (G_CTRL / D_CTRL)

| Bit                       | Name        | Description                                               |
|---------------------------|-------------|-----------------------------------------------------------|
| 0                         | GPUGO/DSPGO | Start processor (1=running)                              |
| 1                         | CPUINT      | Cause CPU interrupt (write 1)                            |
| 2                         | FORCEINT0   | Force interrupt 0                                        |
| 3                         | SINGLE_STEP | Enable single-step mode                                  |
| 4                         | SINGLE_GO   | Execute one instruction (in single-step)                 |
| 6-10 (GPU and DSP)        | INT_LAT0-4  | Interrupt latch status (read-only)                       |
| 11                        | BUS_HOG     | Keep bus between accesses (reduces latency, starves others). Must NOT be set (see Gotchas 4) |
| 12-15                     | VERSION     | Hardware version (read-only)                             |
| 16 (DSP only)             | INT_LAT5    | DSP interrupt 5 latch (read-only)                        |

Source: JTRM Rev 8 pp.60-61 (G_CTRL), pp.110-111 (D_CTRL; INT_LAT5 = bit 16, bit 11 is BUS_HOG on both).
Code agrees: `src/jerry/dsp.c` `INT_LAT5 0x10000`.

---

## Instruction Set

All instructions execute in 1 cycle unless noted.

**Opcode numbers** in every table below: Source: JTRM Rev 8 pp.44-58 "Instruction Set" (GPU) and pp.100-108
(DSP); they match the dispatch tables in `src/tom/gpu.c` and `src/jerry/dsp.c`. An earlier version of this
section had 27 numbers wrong (e.g. it put CMP at 15, MOVEQ at 41, JR at 52) and omitted 14 instructions. The Cycles/Notes columns are
still `Derived from: src/tom/gpu.c`, `src/jerry/dsp.c` -- NOT verified against the JTRM, except where a
`p.N` is given.

### Data Movement

| Opcode | Mnemonic | Operation                     | Cycles    | Notes                                                          |
|--------|----------|-------------------------------|-----------|----------------------------------------------------------------|
| 38     | MOVEI    | Rn <- 32-bit immediate        | 3         | Only 32-bit instruction (low word, high word after opcode; p.51) |
| 37     | MOVEFA   | Rn <- alternate bank Rm       | 1         | Move from alternate register bank                              |
| 36     | MOVETA   | alt Rn <- Rm                  | 1         | Move to alternate register bank                                |
| 34     | MOVE     | Rn <- Rm                      | 1         | Register to register                                           |
| 35     | MOVEQ    | Rn <- quick (0-31)            | 1         | 5-bit immediate                                                |
| 51     | MOVE PC  | Rn <- PC                      | 1         | Address of the current instruction (p.50)                      |
| 41     | LOAD     | Rn <- (Rm)                    | **varies** | External memory = many cycles; local RAM = fast               |
| 40     | LOADW    | Rn <- word (Rm)               | varies    | 16-bit load. Internal RAM does a 32-bit read instead (p.49)    |
| 39     | LOADB    | Rn <- byte (Rm)               | varies    | 8-bit load. Internal RAM does a 32-bit read instead (p.49)     |
| 42     | LOADP    | Rn <- low long (Rm); G_HIDATA <- high long | varies | 64-bit load, external memory only; high long lands in the 32-bit high-data register $F02118 (pp.50, 61) |
| 43/44  | LOAD (R14+n) / (R15+n) | Rn <- (R14/R15 + 4n)  | varies | Indexed, n in 1-32 long words (p.49)            |
| 58/59  | LOAD (R14+Rm) / (R15+Rm) | Rn <- (R14/R15 + Rm) | varies | Register offset (p.49)                          |
| 47     | STORE    | (Rn) <- Rm                    | varies    | 32-bit store                                                   |
| 46     | STOREW   | (Rn) <- Rm (word)             | varies    | 16-bit store (external memory only; internal = 32-bit)         |
| 45     | STOREB   | (Rn) <- Rm (byte)             | varies    | 8-bit store (external memory only; internal = 32-bit)          |
| 48     | STOREP   | (Rn) <- G_HIDATA:Rm           | varies    | 64-bit store, high long from G_HIDATA (p.56)                   |
| 49/50  | STORE (R14+n) / (R15+n) | (R14/R15 + 4n) <- Rm | varies | Indexed store (p.56)                              |
| 60/61  | STORE (R14+Rn) / (R15+Rn) | (R14/R15 + Rn) <- Rm | varies | Register offset store (p.56)                    |

Source: JTRM Rev 8 pp.48-51, 55-56.

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
| 22     | ABS      | Rn <- abs(Rn)                 | ZNC   | Absolute value                     |
| 30     | CMP      | Rn - Rm (flags only)          | ZNC   | Compare, no writeback              |
| 31     | CMPQ     | Rn - quick (flags only)       | ZNC   | Quick compare (signed -16..+15, p.47) |

Source: JTRM Rev 8 pp.44-47, 52, 57 (opcode numbers).

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

Source: JTRM Rev 8 pp.45-46, 52-53 (opcode numbers). (Earlier versions of this table also listed CMP at 15,
colliding with BCLR.)

### Shift

| Opcode | Mnemonic | Operation                 | Flags | Notes                                   |
|--------|----------|---------------------------|-------|-----------------------------------------|
| 23     | SH       | Rn shift by Rm            | ZNC   | **Positive = right**, negative = left; magnitude >= 32 gives 0 |
| 26     | SHA      | Rn arith shift by Rm      | ZNC   | As SH but right shift is arithmetic (sign shifted in) |
| 27     | SHARQ    | Rn >>a quick              | ZNC   | Arithmetic right shift by immediate     |
| 24     | SHLQ     | Rn << quick(1-32)         | ZNC   | Left shift by immediate                 |
| 25     | SHRQ     | Rn >> quick(1-32)         | ZNC   | Logical right shift by immediate        |
| 28     | ROR      | Rn rotate right by Rm     | ZNC   | Rotate right (low 5 bits of Rm)         |
| 29     | RORQ     | Rn rotate right by quick  | ZNC   | Rotate right by immediate               |

Source: JTRM Rev 8 pp.53-55 (SH: "A positive value causes a shift to the right", p.54; `gpu_opcode_sh` agrees).
An earlier version of this table swapped 24-27 and said positive = left.

### Multiply / MAC

| Opcode | Mnemonic | Operation                     | Notes                                      |
|--------|----------|-------------------------------|--------------------------------------------|
| 16     | MULT     | Rn <- Rn * Rm                 | 16x16->32 **unsigned** multiply (p.51)     |
| 17     | IMULT    | Rn <- Rn * Rm                 | 16x16->32 **signed** multiply (p.47) -- a different instruction from MULT |
| 18     | IMULTN   | Rn * Rm -> ACC (no writeback) | Signed multiply, result to accumulator (p.48) |
| 19     | RESMAC   | Rn <- ACC                     | Read MAC accumulator result (p.53)         |
| 20     | IMACN    | ACC += Rn * Rm                | Signed MAC accumulate, no writeback (p.47) |

The MAC unit enables systolic matrix operations. IMULTN starts a chain, IMACN accumulates, RESMAC reads the result.

Source: JTRM Rev 8 pp.47-48, 51, 53 (opcode numbers and signedness). An earlier version said "IMULT same as MULT
on Jaguar" and was one number off for the whole group (DIV is 21, not 57).

**For DSP**: The MAC accumulator is 40 bits (D_MACHI at $F1A120 holds bits 32-39). This prevents overflow during audio DSP chains.

Derived from: `src/jerry/dsp_acc40.h`, `test/test_dsp_mac40.c` -- NOT verified
against the JTRM

### Branch / Jump

| Opcode | Mnemonic       | Operation               | Notes                                              |
|--------|----------------|-------------------------|----------------------------------------------------|
| 53     | JR             | PC <- PC + offset       | Relative jump, signed offset in words (+15/-16). ALWAYS has 1 delay slot. |
| 52     | JUMP           | PC <- (Rn)              | Absolute jump. ALWAYS has 1 delay slot.            |
| --     | JR cc, label   | Conditional relative    | cc = condition code                                |
| --     | JUMP cc, (Rn)  | Conditional absolute    |                                                    |

Source: JTRM Rev 8 p.48 (JR 53, JUMP 52; the condition field is five bits ANDed together: bit 0 Z clear,
bit 1 Z set, bit 2 selected flag clear, bit 3 selected flag set, bit 4 selects N (1) or C (0)). The assembler
mnemonics below are a convention, not from the manual:
Condition codes: `T` (always), `NE` (Z=0), `EQ` (Z=1), `CC`/`HS` (C=0), `CS`/`LO` (C=1), `PL` (N=0), `MI` (N=1)

### DSP-Only Instructions

| Opcode | Mnemonic | Operation                          | Notes                              |
|--------|----------|------------------------------------|------------------------------------|
| 63     | ADDQMOD  | Rn <- (Rn + quick) MOD D_MOD      | Circular buffer increment          |
| 32     | SUBQMOD  | Rn <- (Rn - quick) MOD D_MOD      | Circular buffer decrement          |
| 33     | SAT16S   | Rn <- clamp(Rn, -32768, 32767)    | Signed 16-bit saturation           |
| 42     | SAT32S   | Rn <- clamp(40-bit, -2^31, 2^31-1) | Signed saturation of the 40-bit MAC result; apply only straight after a multiply/accumulate (pp.98-99, 106) |
| 48     | MIRROR   | Rn <- bit-reverse(Rn)              | Bit 0 <-> bit 31 etc.; FFT address generation (p.104) |

ADDQMOD/SUBQMOD use D_MOD ($F1A118) as the modulo value. Essential for circular audio buffers.

Source: JTRM Rev 8 pp.100-108 (DSP instruction list; p.100: LOADP, SAT8, SAT16, SAT24, STOREP, PACK and UNPACK
are absent on the DSP; SAT16S, SAT32S, ADDQMOD, SUBQMOD and MIRROR are added). Opcodes 32, 33, 42, 48, 62 and
63 mean different things on GPU vs DSP. Code: `dsp_opcode[]` in `src/jerry/dsp.c` (62 = illegal).

### Miscellaneous

| Opcode | Mnemonic | Operation                                                        |
|--------|----------|------------------------------------------------------------------|
| 54     | MMULT    | Matrix multiply (uses MTXC, MTXA)                                |
| 21     | DIV      | Rn <- Rn / Rm, unsigned (uses DIVCTRL; remainder in REMAIN)      |
| 63     | PACK     | Pack unpacked pixel -> 16-bit CRY; PACK and UNPACK share opcode 63, reg1 = 0 selects PACK |
| 63     | UNPACK   | Reverse of PACK; reg1 = 1 selects UNPACK                         |
| 55     | MTOI     | Mantissa of IEEE float in Rm -> signed integer in Rn             |
| 56     | NORMI    | Normalisation integer of unsigned Rm -> Rn                       |
| 62     | SAT24    | Saturate to 24-bit unsigned (GPU only)                           |
| 32     | SAT8     | Saturate to 8-bit unsigned (GPU only)                            |
| 33     | SAT16    | Saturate to 16-bit unsigned (GPU only)                           |
| 57     | NOP      | No operation                                                     |

Source: JTRM Rev 8 pp.47, 50-54, 58 (opcode numbers; DIV writes its result at cycle 18, p.47; the divide unit's
operation takes sixteen ticks, p.42). An earlier version put MMULT at 39, DIV at 57, NOP at 54, PACK/UNPACK at
61/62 and omitted MTOI, NORMI, SAT8/16/24.

---

## Wave Table ROM ($F1D000-$F1DFFF)

Eight 128-entry tables of signed 16-bit samples, each sign-extended to 32 bits on read (so the ROM "appears
to occupy 1K 32-bit locations"; only the low 16 bits are significant). Tables are 0x200 bytes apart:

| Offset | Waveform                 |
|--------|--------------------------|
| $000   | Triangle (TRI)           |
| $200   | Sine (SINE)              |
| $400   | Amplitude-modulated sine (AMSINE) |
| $600   | Sine + second harmonic (SINE12W) |
| $800   | Chirp (CHIRP16)          |
| $A00   | Triangle + noise (NTRI)  |
| $C00   | Delta / spike (DELTA)    |
| $E00   | White noise (NOISE)      |

Source: JTRM Rev 8 p.98 "Wave Table ROM" (eight 128-entry tables, 16-bit values sign-extended to 32 bits;
addresses F1D000/200/400/.../DE00). An earlier version of this section said 8 x 256 x 32-bit samples at 0x400
spacing and a ROM size of 8 KB; none of that is in the manual. Software Reference v2.4 (PDF p.80) agrees with
128 entries. Implementation: `src/jerry/wavetable.c`.

---

## Known Emulation Gotchas

1. **Delay slots**: JR/JUMP always execute the next instruction before branching. Missing this breaks virtually all GPU/DSP code.
2. **MOVEI word order**: The 32-bit immediate after MOVEI is stored LOW word first, HIGH word second. Getting this wrong corrupts all address loads.
3. **Score-boarding stalls**: The pipeline stalls on RAW hazards. Not modeling this makes code run too fast and breaks timing-dependent programs.
4. **BUS_HOG (do not set)**: When set in CTRL, the processor doesn't release the bus between program fetches. This starves lower-priority masters. The manual's register chapter describes it as a normal feature (v8 p.61, p.111), but Software Reference v2.4 marks it "must not be set" on the console (GPU p.47 (PDF p.49), DSP PDF p.85). Not modeled by the emulator. See `docs/jtrm-errata.md` (entry A3).
5. **DMAEN (do not set)**: G_FLAGS/D_FLAGS bit 15 moves LOAD/STORE to DMA bus priority. v8 documents it as a feature (p.59, p.109) but v8's own bug list (TOM #24, p.138) says no master may outrank the Object Processor (stripes on screen), Software Reference v2.4 says it must not be set (GPU p.45 (PDF p.47), DSP p.81 (PDF p.83)), and Hardware Bugs & Warnings (26 Apr 1995) p.2 #4 says that on the DSP it makes an external LOAD/STORE hang the DSP. Not modeled. See `docs/jtrm-errata.md` (entry A3).
6. **40-bit MAC (DSP only)**: The DSP accumulator is 40 bits. D_MACHI holds overflow bits. Truncating to 32 bits causes audio distortion in games that use long MAC chains.
7. **External memory access timing**: LOAD/STORE to external memory takes many more cycles than local RAM. The exact timing depends on bus contention.
