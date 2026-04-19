# Jaguar CD Data Flow

How CD data gets from disc to main RAM. Derived from MiSTer FPGA core,
MAME, and BIOS disassembly.

## Interrupt Path

```
BUTCH eint  -->  Jerry external interrupt 0  -->  68K IRQ2 / GPU IRQ0 / DSP EXT0
```

Jerry routes `eint` to both the 68K interrupt controller (via J_INTCTRL
$F10020) and the DSP external interrupt inputs (via D_FLAGS $F1A100 EXT0ENA).

The BIOS typically configures a **GPU ISR** to handle CD data transfers. The
68K sets G_DSPENA in G_FLAGS so the GPU receives the interrupt from Jerry.

## I2S Data Path: Disc -> FIFO -> RAM

1. **CD mechanism** sends audio/data frames to BUTCH over a serial bus
2. **BUTCH transport** buffers 8-byte chunks in a 4-deep 64-bit FIFO,
   deserializes at 44.1kHz into 16-bit samples via the I2S serializer
3. If I2CNTRL bit 2 is set, each sample pair is written into the
   **16-deep 32-bit software FIFO** (`i2s_fifo[0:15]`)
4. When FIFO fill >= 8, bit 9 (FIFO_HALF) asserts in BUTCH status
5. If bits 0+1 (master + FIFO IRQ enable) are set, `eint` asserts
6. **Jerry external interrupt 0** fires -> **GPU ISR** activates
7. GPU ISR reads 8 longwords alternating $DFFF28/$DFFF24 -> stores to RAM
8. Each read pops one 32-bit entry; FIFO drops below half -> `eint` deasserts
9. BUTCH continues filling; when half-full again, cycle repeats

## CD_read BIOS Function Sequence

### Phase 1: Setup (68K)
1. Write I2CNTRL ($DFFF10) = $07 (I2S drive + Jerry path + FIFO enable)
2. Write BUTCH ($DFFF00) = $03 (master IRQ + FIFO half-full IRQ enable)
3. Configure Jerry I2S as slave via SMODE ($F1A154)
4. Load GPU ISR into GPU RAM for FIFO drain
5. Enable GPU with DSP interrupt input (G_DSPENA in G_FLAGS)

### Phase 2: Seek (68K -> DSA)
6. Write DS_DATA: $10mm (goto minutes), $11ss (goto seconds), $12ff (goto frames)
7. $12ff triggers the actual seek; BUTCH queues $0100 response when complete
8. Optional: $15nn to set CD-ROM mode (bit 3)

### Phase 3: Playback (BUTCH internal)
9. When I2CNTRL bit 2 transitions 0->1 in CD-ROM mode, BUTCH starts `splay`:
   pre-fills internal FIFO, enables I2S serializer, transport begins

### Phase 4: Data Transfer (continuous loop)
10. BUTCH fills 16-deep FIFO at I2S rate (~22us per entry)
11. FIFO fill >= 8 -> bit 9 set -> `eint` asserts
12. GPU ISR fires, reads 8 longwords from $DFFF28/$DFFF24
13. Stores to target RAM buffer, advances CD_ptr
14. Repeats until requested byte count reached

### Phase 5: Completion
15. 68K monitors CD_ptr to know when read is complete
16. Game sends $0200 (STOP) through DS_DATA

## BIOS RAM Code Map

| ROM Range | RAM Range | Size | Purpose |
|-----------|-----------|------|---------|
| $802000-$8042A6 | $050000+ | 9KB | BIOS RAM-resident code |
| $8084A6-$808E90 | $003000+ | 2.5KB | BIOS jump table |
| $808E90-$81421C | $080000+ | 23KB | CD Player UI fallback |
| $81421C-$82F1C8 | $192000+ | 110KB | BIOS service routines |

Entry: Cart populator at $802000 copies all of the above, then JMPs to $0500D6.
BIOS runs auth, then `JSR $00080000` at PC=$050176 (boot stub or CD Player).

## BIOS Jump Table ($003000)

6-byte entries: BRA.W + NOP. Key entries:
- Entry 13 ($304E -> $3610): CD_read -- the function games call to read CD data

## Boot Stub Layout (Session 2 Track, sector 0, after word-swap)

```
+0x000-0x041: Sync preamble (0xD7 0x72 "ATRI"... repeated)
+0x042-0x061: "ATARI APPROVED DATA HEADER ATRI " (32-byte magic)
+0x062-0x065: Load address (big-endian, typically $00080000)
+0x066-0x069: Length (big-endian)
+0x06A onward: M68K boot loader code
```

## References

- [MiSTer Jaguar CD_latest](https://github.com/MiSTer-devel/Jaguar_MiSTer/tree/CD_latest) - butch.v, butch_i2s.v
- [MAME jaguar.cpp](https://github.com/mamedev/mame/blob/master/src/mame/atari/jaguar.cpp)
- [Jaguar Technical Reference Manual](https://www.hillsoftware.com/files/atari/jaguar/jag_v8.pdf)
- [AtariAge CD BIOS threads](https://forums.atariage.com/topic/254145-cd-bios-questions/)
