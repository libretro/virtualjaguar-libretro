# Jaguar CD BIOS Jump Table Calling Convention

Reverse-engineered from the retail CD BIOS ROM (`[BIOS] Atari Jaguar CD (World).j64`),
cross-referenced against the developer BIOS, CDBYPASS ROMs, and MiSTer FPGA implementation.

## Jump Table Layout

The BIOS copies an 18-entry branch table from ROM `$8084A6` to RAM `$3000`.
Each entry is 6 bytes: `BRA.W <offset>` + `NOP`.

| Entry | Address | Name               | Parameters                         |
|-------|---------|--------------------|------------------------------------|
| 0     | $3000   | CD_setup_audio_isr | A0 = GPU RAM base. Copies $E0 bytes of ISR. Sets [$3072]=0. |
| 1     | $3006   | CD_wait_response   | Polls BUTCH bit 13, reads DS_DATA response into D1. |
| 2     | $300C   | CD_wait_response2  | Same as entry 1. |
| 3     | $3012   | CD_i2s_enable      | D0: 0=disable, 1=enable Jerry+FIFO. |
| 4     | $3018   | CD_spin_up         | D1=session, D0=wait flag. DSA cmd $18nn. |
| 5     | $301E   | CD_stop_drive      | D0=wait flag. DSA cmd $0200. |
| 6     | $3024   | CD_set_volume_mute | D0=wait. DSA cmd $5100. |
| 7     | $302A   | CD_set_volume_max  | D0=wait. DSA cmd $51FF. |
| 8     | $3030   | CD_pause           | D0=wait. DSA cmd $0400. |
| 9     | $3036   | CD_unpause         | D0=wait. DSA cmd $0500. |
| 10    | $303C   | **CD_read**        | See below. |
| 11    | $3042   | CD_fifo_disable    | Clear bit 2 of I2CNTRL. |
| 12    | $3048   | CD_hw_reset        | DSA cmd $7001. Reset BUTCH/DSCNTRL/I2CNTRL. |
| 13    | $304E   | **CD_poll**        | See below. |
| 14    | $3054   | CD_set_dac_mode    | D0=0-2. DSA cmd $70nn. |
| 15    | $305A   | CD_read_toc        | A0 = buffer ($384 bytes). DSA cmds $03nn/$14nn. |
| 16    | $3060   | CD_setup_cdrom_isr | A0 = GPU RAM base. Copies $150 bytes. Sets [$3072]=$FF. |
| 17    | $3066   | CD_setup_data_isr  | A0 = GPU RAM base. Copies $D4 bytes. Sets [$3072]=1. |

## CD_read ($303C) — Full Specification

### Inputs

| Register | Meaning |
|----------|---------|
| D0.L     | Packed MSF seek position: `(min << 16) \| (sec << 8) \| frm`. Values are binary (NOT BCD). **Bit 31**: if set, skip hardware init, just re-seek (GPU data area already configured by prior call). |
| D1.L     | Sync sentinel for CD-ROM mode ISR. Stored to GPU data area [+16]. In audio mode ISR, ignored. CDBYPASS passes `$41545249` ("ATRI") for boot stub reads. Games may pass DDL markers. |
| D2.L     | Speed/mode parameter. Only used in CD-ROM mode ([$3072] bit 7 set). |
| A0       | Destination buffer in Jaguar RAM. Internally decremented by 4 (GPU ISR pre-increments before store). |
| A1       | End address (destination + byte count). Stored to GPU data area [+4]. |

### Behavior (bit 31 clear — full init, used by games)

1. Disable BUTCH IRQs, disable FIFO
2. Store `A0 → GPU_DATA[+0]` (dest), `A1 → GPU_DATA[+4]` (end), `0 → GPU_DATA[+8]` (progress)
3. Drain FIFO, re-enable BUTCH with IRQ
4. Extract MSF from D0: min = `(D0 >> 16) & 0xFF`, sec = `(D0 >> 8) & 0xFF`, frm = `D0 & 0xFF`
5. Send DSA seek commands: `$10MM`, `$11SS`, `$12FF` to DS_DATA ($DFFF0A)

### Behavior (bit 31 set — re-seek, used by BIOS internally)

Skip steps 1-3, only send DSA seek commands (step 4-5).

### Transfer Mechanism

Data arrives asynchronously via the GPU ISR:
- Audio mode ISR ($3000 setup): transfers all incoming I2S data directly to RAM
- **CD-ROM mode ISR ($3060 setup): scans incoming I2S data for the D1 sync sentinel, then starts transferring from that point**
- Data ISR ($3066 setup): variant of audio mode

The ISR writes data to the destination buffer and advances the write pointer.
Transfer completes when the write pointer reaches the end address (A1).

### Sync Sentinel Scanning (CD-ROM mode)

In CD-ROM mode, the GPU ISR does NOT transfer data immediately.
It scans each incoming I2S word for the 32-bit pattern stored in D1.
When the pattern is found, the ISR begins the actual data transfer.

This is how the BIOS locates specific data on disc:
- The boot stub seeks to near the session-2 start
- The I2S stream contains all sectors from that point forward (crossing track boundaries)
- The ISR scans through potentially hundreds of sectors of boot stub / padding data
- When it finds the DDL marker matching D1, it starts copying game data to RAM

### BIOS Internal Completion

The BIOS does NOT use CD_poll. It polls DSP RAM flag at `[$F1B4C8]` — the GPU ISR
writes `$FFFFFFFF` there when the transfer completes, and the BIOS loops until negative.

## CD_poll ($304E)

### Inputs
None.

### Outputs

| Register | Meaning |
|----------|---------|
| A0       | Current RAM write position (GPU ISR advances this as data arrives) |
| A1       | Error status: 0 = OK (transfer in progress or complete), non-zero = error |

Boot stubs save the end address in A6 before calling CD_read, then poll:
```
.poll:  JSR ($304E).w        ; CD_poll
        CMPA.L #0, A1        ; error?
        BNE .error            ; A1 != 0 → error/retry
        CMPA.L A6, A0         ; current position >= end?
        BLT .poll             ; not done yet
        ; success — transfer complete
```

**Note:** A1 is NOT "bytes transferred" — it is an error flag. The Primal Rage boot stub
at $0803A4 explicitly checks `CMPA.L #0,A1; BNE .error`. This was confirmed by
disassembly of the retail boot stub and verified against BIOS behavior.

## CDBYPASS Boot Sequence (Reference)

1. `JSR $3048` — hardware reset
2. `JSR $3006` — CD_init (D0=2, audio mode)
3. `JSR $305A` — read TOC to $2C00
4. TOC scan: find session marker (byte[4]==1), take next entry's MSF
5. MSF adjustment: subtract 6 frames (pregap offset)
6. `JSR $3060` — set up **CD-ROM mode ISR** (A0=$F030A4)
7. First `JSR $303C` — CD_read 4096 bytes, D1="ATRI", to $6010
8. Scan loaded data for "TRI " header, extract load address + length
9. Second `JSR $303C` — CD_read (length) bytes of game data
10. Copy to final load address, `JMP` to it

## Sector Data Format

Jaguar CD data tracks are typed as AUDIO in CUE sheets. Each sector is 2352 bytes
of raw data (no Mode 1 sync/header/ECC structure). The I2S bus transfers all 2352 bytes.

The I2S transfer swaps bytes within 16-bit words. The GPU ISR un-swaps before writing
to RAM. Data on disc (in BIN files) is stored pre-swapped.

MiSTer FPGA note: the ring buffer stores 2048 bytes per sector, but this is an
implementation detail of the HPS interface, not a reflection of the Jaguar hardware.
