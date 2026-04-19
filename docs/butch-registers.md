# BUTCH Register Map ($DFFF00 - $DFFF2F)

Reference for the Jaguar CD BUTCH chip registers. Derived from MiSTer FPGA
(`butch.v`, `butch_i2s.v`), MAME (`jaguar.cpp`), ChillyWilly JaguarLibs, and
Atari Jaguar Technical Reference Manual.

## $DFFF00 - BUTCH (Interrupt Control Register, R/W)

### Write bits (longword)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | MASTER_EN | Master IRQ enable (must be 1 for any BUTCH interrupt) |
| 1 | FIFO_EN | CD data FIFO half-full interrupt enable |
| 2 | SUBFRAME_EN | CD subcode frame-time interrupt enable (~7ms at 2x) |
| 3 | SUBMATCH_EN | Pre-set subcode time-match found interrupt enable |
| 4 | TX_EN | CD module command TX buffer empty interrupt enable |
| 5 | RX_EN | CD module command RX buffer full interrupt enable |
| 6 | CIRC_EN | CIRC failure interrupt enable |
| 17 | CD_RESET | CD reset |
| 18 | BIOS_OVRD | CD BIOS override (BUTCH handles cart-space addresses) |
| 19 | LID_RESET | CD open-lid reset |
| 20 | CART_RESET | CD cartridge-pull reset |

### Read bits (longword)
| Bit | Name | Description |
|-----|------|-------------|
| 9 | FIFO_HALF | CD data FIFO half-full (>= 8 entries) |
| 10 | SUBCODE_PEND | Subcode frame pending |
| 11 | FRAME_PEND | Frame pending (set if cdPlaying) |
| 12 | TX_EMPTY | Command to CD drive pending (TX buffer empty if 1) |
| 13 | RX_FULL | Response from CD drive pending (RX buffer full if 1) |
| 14 | CD_ERROR | CD uncorrectable data error pending |

### Interrupt generation (from MiSTer butch.v)
```
eint = bit0 && (fifo_int || frame_int || sub_int || tbuf_int || rbuf_int)

fifo_int  = bit9  && bit1   // FIFO half-full status AND enable
frame_int = bit10 && bit2   // Frame status AND enable
sub_int   = bit11 && bit3   // Subcode status AND enable
tbuf_int  = bit12 && bit4   // TX empty status AND enable
rbuf_int  = bit13 && bit5   // RX full status AND enable
```

## $DFFF04 - DSCNTRL (DSA Control Register, R/W)
- Bit 16: Enable DSA bus
- Reading clears bit 12 (TX buffer empty) in BUTCH status register

## $DFFF0A - DS_DATA (DSA TX/RX Data, R/W, 16-bit)

### DSA Commands (write)
| Cmd | Description | Parameter |
|-----|-------------|-----------|
| $01nn | Play title | Track number (hex) |
| $0200 | Stop | - |
| $03nn | Read TOC | Session number |
| $0400 | Pause | - |
| $0500 | Pause release | - |
| $10nn | Goto time (min) | Minutes (hex) |
| $11nn | Goto time (sec) | Seconds (hex) |
| $12nn | Goto time + start | Frames (hex, triggers seek) |
| $14nn | Read long TOC | Session number |
| $15nn | Set mode | Mode bits (bit 3 = CD-ROM mode) |
| $18nn | Spin up | Session number |
| $5000 | Get disc status | - |
| $51nn | Set volume | Volume level |
| $5400 | Get max session | - (returns session count) |
| $70nn | Set DAC mode | Oversampling mode |

### DSA Responses (read)
| Response | Description |
|----------|-------------|
| $0100 | Found (seek complete) |
| $0200 | Stopped |
| $03nn | Disc status |
| $04nn | Error code |
| $10nn | Current title (track number) |
| $20nn-$24nn | TOC values: min track, max track, leadout M/S/F |

## $DFFF10 - I2CNTRL (I2S Bus Control Register, R/W)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | I2S_DRIVE | I2S drive enable (I2S output from BUTCH active) |
| 1 | I2S_JERRY | I2S path to Jerry enabled |
| 2 | FIFO_EN | FIFO enabled (gates samples into software-readable FIFO) |
| 3 | MODE_16 | 16-bit mode (vs 32-bit I2S word format) |
| 4 | FIFO_NE | FIFO not empty (read-only, `wptr != rptr`) |

Writing bit 2 high in CD-ROM mode triggers `splay` (playback start).

## $DFFF14 - SBCNTRL (Subcode Control, R/W)
Reading clears pending subcode and frame interrupts.

## $DFFF18 - SUBDATA (Subcode Data A, R)
## $DFFF1C - SUBDATB (Subcode Data B, R)
Sub-Q channel data.

## $DFFF20 - SB_TIME (Subcode Time + Compare Enable, R/W)

## $DFFF24 - FIFO_DATA / I2SDAT1 (I2S FIFO Data, R)
## $DFFF28 - I2SDAT2 (I2S FIFO Data, R)

Both addresses read from the **same 16-deep circular FIFO**. Each entry is a
32-bit word (left+right 16-bit samples). The BIOS reads by alternating between
$DFFF24 and $DFFF28 -- each read pops one 32-bit entry.

The BIOS reads 8 longwords per interrupt (16 word-reads = 32 bytes of data).

## $DFFF2C - EEPROM (NM93C14 EEPROM Interface, R/W)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | CS | Chip Select |
| 1 | SK | Clock |
| 2 | DO | Data Out (to EEPROM) |
| 3 | DI | Data In / Busy (from EEPROM, read-only) |
