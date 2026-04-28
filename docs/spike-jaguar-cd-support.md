# Jaguar CD Support -- Feasibility Study and Technical Research

**Date:** 2026-04-15
**Issue:** [libretro/virtualjaguar-libretro#41](https://github.com/libretro/virtualjaguar-libretro/issues/41)
**Scope:** Adding Atari Jaguar CD emulation to the Virtual Jaguar libretro core
**Status:** Research / Spike

---

## 1. Jaguar CD Hardware Specifications

### 1.1 Overview

The Atari Jaguar CD is a CD-ROM peripheral released September 21, 1995 for US$149.95. It connects via the cartridge slot atop the console and includes a pass-through cartridge slot. It uses a double-speed (2x) CD-ROM drive mechanism and a proprietary disc format based on audio CD rather than standard CD-ROM data formats. Discs can store up to 790 MB. It also includes Jeff Minter's Virtual Light Machine (VLM), a built-in audio visualizer for CD audio playback.

### 1.2 The Butch ASIC

Butch is a proprietary ASIC that serves as the primary interface between the Jaguar bus and the CD-ROM drive mechanism. It handles:

- CD-ROM command transmission/response via the DSA (Data Strobe Acknowledge) serial bus
- I2S audio data streaming from the CD drive to JERRY's SSI (Synchronous Serial Interface)
- CD subcode processing (track timing, subchannel data)
- Interrupt generation for FIFO half-full, subcode frame, command TX/RX events
- CD-ROM EEPROM access (NM93C14 compatible, 64 words / 128 bytes of NVRAM)
- CIRC error status reporting

### 1.3 Butch Register Map (Base: $DFFF00)

All Butch registers are memory-mapped in the $DFFF00-$DFFF2F range. These are **already mapped in the codebase** (see `src/core/vjag_memory.c`, `src/core/mmu.c`, `src/cd/cdrom.c`).

| Address | Size | Name | R/W | Description |
|---------|------|------|-----|-------------|
| `$DFFF00` | Long | BUTCH | R/W | Interrupt control register |
| `$DFFF04` | Long | DSCNTRL | R/W | DSA control register |
| `$DFFF0A` | Word | DS_DATA | R/W | DSA TX/RX data (command/response) |
| `$DFFF10` | Long | I2CNTRL | R/W | I2S bus control register |
| `$DFFF14` | Long | SBCNTRL | R/W | CD subcode control register |
| `$DFFF18` | Long | SUBDATA | R/W | Subcode data register A |
| `$DFFF1C` | Long | SUBDATB | R/W | Subcode data register B |
| `$DFFF20` | Long | SB_TIME | R/W | Subcode time and compare enable (bit 24) |
| `$DFFF24` | Long | FIFO_DATA | R/W | I2S FIFO data (primary) |
| `$DFFF28` | Long | I2SDAT2 | R/W | I2S FIFO data (secondary/old) |
| `$DFFF2C` | Long | UNKNOWN | R/W | CD EEPROM serial interface |

**BUTCH Interrupt Control Register ($DFFF00) -- Write bits:**

| Bit | Function |
|-----|----------|
| 0 | Global interrupt enable |
| 1 | CD data FIFO half-full interrupt enable |
| 2 | CD subcode frame-time interrupt enable (~7ms at 2x) |
| 3 | Pre-set subcode time-match found interrupt enable |
| 4 | CD command transmit buffer empty interrupt enable |
| 5 | CD command receive buffer full interrupt enable |
| 6 | CIRC failure interrupt enable |
| 7-31 | Reserved (set to 0) |

**BUTCH Interrupt Control Register ($DFFF00) -- Read bits:**

| Bit | Function |
|-----|----------|
| 0-8 | Reserved |
| 9 | CD data FIFO half-full flag pending |
| 10 | Frame pending |
| 11 | Subcode data pending |
| 12 | Command to CD drive pending (TX buffer empty if 1) |
| 13 | Response from CD drive pending (RX buffer full if 1) |
| 14 | CD uncorrectable data error pending |

**I2CNTRL Register ($DFFF10) -- Read bits:**

| Bit | Function |
|-----|----------|
| 0 | I2S data from drive is ON if 1 |
| 1 | I2S path to JERRY is ON if 1 |
| 2 | Reserved |
| 3 | Host bus width is 16 if 1, else 32 |
| 4 | FIFO state: not empty if 1 |

### 1.4 CD Command Protocol (via DS_DATA at $DFFF0A)

Commands are written as 16-bit words where the high byte is the command ID:

| Command | Description |
|---------|-------------|
| `$01nn` | Play/seek to track nn |
| `$0200` | Stop CD |
| `$03nn` | Read session nn TOC (short/overview) |
| `$0400` | Pause CD |
| `$0500` | Unpause CD |
| `$10nn` | Goto (minutes) |
| `$11nn` | Goto (seconds) |
| `$12nn` | Goto (frames) |
| `$14nn` | Read session nn TOC (full) |
| `$15nn` | Set CD mode |
| `$18nn` | Spin up CD to session nn |
| `$5000` | Unknown |
| `$5100` | Mute CD (audio mode only) |
| `$51FF` | Unmute CD (audio mode only) |
| `$5400` | Read number of sessions on CD |
| `$70nn` | Set oversampling mode |

**CD Mode bits (command $15nn):** Bit 0 = single speed, Bit 1 = double speed, Bit 3 set = multisession CD, Bit 3 unset = audio CD.

### 1.5 CD EEPROM Interface ($DFFF2C)

The CD unit has an onboard NM93C14-compatible EEPROM accessed through $DFFF2C with a serial bit-bang protocol. Bits: 0=Chip Select, 1=Clock, 2=Data Out, 3=Busy/Data In. 9-bit serial commands include READ (`%110000000`), EWEN (`%100110000`), ERASE (`%111000000`), WRITE (`%101000000`), ERAL (`%100100000`), WRAL (`%100010000`), EWDS (`%100000000`). The EEPROM provides 64 words (128 bytes) of non-volatile storage.

### 1.6 CD-to-Jaguar Data Path

**CD Audio (I2S path):** CD drive streams I2S data to Butch FIFO (appears to be 32 bytes per FIFO). When I2CNTRL bit 1 is set, Butch streams to JERRY's SSI. JERRY generates DSP IRQs (DSPIRQ_SSI) for each stereo sample. The DSP reads L/R samples from LRXD/RRXD ($F1A148/$F1A14C) and mixes with synthesized audio.

**CD Data (sector reading):** 68K sends seek commands (min/sec/frame) via DS_DATA. Sector data (2352 bytes raw audio format) arrives via FIFO. Data is read from FIFO_DATA ($DFFF24) and I2SDAT2 ($DFFF28) alternately. The CD BIOS uses a GPU ISR to drain the FIFO, reading 8 longwords at a time.

**Interrupt Routing:** Butch interrupts route through JERRY to the GPU via EXT1. The GPU ISR acknowledges via J_INT ($F10020).

### 1.7 Memory Map Summary

The CD unit does NOT add additional general-purpose RAM. The standard Jaguar memory map applies. The "extra RAM" sometimes referenced in community discussions refers to Butch's internal FIFO buffers, not addressable memory. The Memory Track cartridge (sold separately) provides EEPROM save storage.

| Address Range | Size | Description |
|---------------|------|-------------|
| `$000000-$1FFFFF` | 2 MB | Main RAM (unchanged) |
| `$800000-$DFFF00` | ~6 MB | Cart ROM space (CD BIOS occupies part) |
| `$DFFF00-$DFFF2F` | 48 bytes | Butch registers (CD controller I/O) |
| `$E00000-$E1FFFF` | 128 KB | Boot ROM / BIOS ROM |

---

## 2. BIOS Requirements

### 2.1 Required BIOS Files

Two CD-related BIOS ROMs already exist as compiled-in C arrays in the codebase:

| File | Source Array | Size | Description |
|------|-------------|------|-------------|
| `src/bios/jagcdbios.c` | `jaguarCDBootROM[]` | 0x40000 (262,144 bytes = 256 KB) | Retail Jaguar CD BIOS |
| `src/bios/jagdevcdbios.c` | `jaguarDevCDBootROM[]` | 0x40000 (262,144 bytes = 256 KB) | Developer CD BIOS |

### 2.2 BIOS Identification

The file database (`src/core/filedb.c`) identifies two CD BIOS variants by CRC32:

| CRC32 | Name | Flags |
|-------|------|-------|
| `0x687068D5` | [BIOS] Atari Jaguar CD (World) | FF_BIOS |
| `0x55A0669C` | [BIOS] Atari Jaguar Developer CD (World) | FF_BIOS |

### 2.3 BIOS Mapping

The standard Jaguar boot ROM is mapped at `$E00000-$E1FFFF` (128 KB). The CD BIOS is 256 KB (0x40000 bytes) and must replace or extend this mapping. In the standard Jaguar, the boot ROM is loaded into `jagMemSpace + 0xE00000`. The CD BIOS is larger and the mapping at `$E00000-$E3FFFF` would need to accommodate it.

### 2.4 CD BIOS Boot Sequence

1. On power-on, the Jaguar loads the boot ROM into memory (~400ms after power)
2. The CD BIOS initializes Tom, Jerry, GPU, and DSP
3. The BIOS displays the Jaguar CD intro animation (spinning logo)
4. The BIOS reads the disc's TOC to locate sessions
5. Session 2 is spun up; the BIOS reads encrypted boot data from the disc
6. The GPU handles the decryption while the DSP processes audio
7. Decrypted game code is loaded into RAM and executed
8. The BIOS makes hard-coded fix-ups to absolute addresses in decrypted code

**Important:** The CD BIOS leaves the GPU busy on the intro demo and decrypts blocks into the DSP. The DSP must be enabled for CD games to work.

### 2.5 Legal/Distribution Considerations

The CD BIOS ROMs are compiled directly into the binary as C arrays. This is the same approach used for the standard cart boot ROM. This means the BIOS is bundled with the core (no external file needed). This raises copyright concerns since BIOS code is Atari's IP. An alternative approach is to require users to provide a BIOS file externally via `RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY`, which is the safer path used by most libretro CD-based cores. The existing core already ships with embedded cart BIOS data, so the CD BIOS follows the same precedent.

---

## 3. Disc Image Formats

### 3.1 Jaguar CD Disc Structure

Jaguar CD discs use a unique proprietary format:

- **All tracks are audio** -- there are no standard CD-ROM Mode 1/Mode 2 data tracks
- **Multisession layout** -- minimum 2 sessions
  - **Session 1:** Audio tracks (Red Book audio, game soundtrack or silence)
  - **Session 2:** Game data encoded as audio tracks, plus encryption data
- Minimum 2 tracks in session 2; data must exceed ~1 MB
- Data on disc uses raw 2352-byte sectors (audio sector format)
- The "TAIRI" (reversed Atari) signature appears in boot sector data

### 3.2 Disc Image Formats in Use

| Format | Extension(s) | Notes |
|--------|-------------|-------|
| BIN/CUE | `.bin`, `.cue` | Most common for Redump-verified dumps. Best for emulation. |
| CDI | `.cdi` | DiscJuggler format. Very common historically. Used by BigPEmu. |
| CCD/IMG | `.ccd`, `.img`, `.sub` | CloneCD format. Less common but functional. |
| CHD | `.chd` | MAME compressed format. Preferred for distribution in libretro. |

**Community prevalence:** CDI and BIN/CUE are the most common. Redump (28 verified dumps as of March 2026) uses BIN/CUE as the canonical archival format.

### 3.3 libretro-common Disc Handling Facilities

The `libretro-common/` directory provides a VFS CDROM interface (`vfs_implementation_cdrom.h`) with functions for physical drive access, but **NOT** disc image parsing. There is no CUE parser, no BIN reader, no CHD support, and no CDI parser in this vendored copy.

Other libretro cores typically include their own disc image parsing. The recommended approach is to implement a CUE/BIN parser (or port one from another libretro core like beetle-pce-fast or genesis-plus-gx), optionally add CHD support via libchdr, and use the libretro disc control ext interface.

The `retro_disk_control_ext_callback` struct (env 58, `RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE`) provides: `set_eject_state`, `get_eject_state`, `set_image_index`, `get_image_index`, `get_num_images`, `replace_image_index`, `add_image_index`, `set_initial_image`, `get_image_path`, `get_image_label`.

---

## 4. Existing Codebase Analysis

### 4.1 CD Subsystem Already Present

The codebase contains a **substantial but incomplete** CD-ROM emulation framework. All CD files are already compiled into the build via `Makefile.common`:

| File | Purpose | Status |
|------|---------|--------|
| `src/cd/cdrom.c` | Butch register emulation, CD command handling | Partial |
| `src/cd/cdrom.h` | CD-ROM public API declarations | Complete |
| `src/cd/cdintf.c` | OS-agnostic CD interface (disc image abstraction) | **Stubbed** |
| `src/cd/cdintf.h` | CD interface declarations | Complete |
| `src/bios/jagcdbios.c` | Embedded retail CD BIOS ROM data (256 KB) | Present |
| `src/bios/jagdevcdbios.c` | Embedded developer CD BIOS ROM data (256 KB) | Present |

### 4.2 What Already Works

**Memory mapping is complete.** All read/write functions in `src/core/jaguar.c` route `$DFFF00-$DFFFFF` to CDROMReadByte/Word and CDROMWriteByte/Word. `src/core/vjag_memory.c` declares Butch registers as memory-mapped pointers at correct addresses. `src/core/mmu.c` has entries for all Butch registers.

**Butch register handling is partially implemented.** `src/cd/cdrom.c` has a 256-byte `cdRam[]` array for CD register state. Read/write handlers for DS_DATA, BUTCH interrupt register, I2CNTRL, FIFO_DATA exist. The CD command protocol is partially decoded (stop, seek, read TOC, set mode). A serial bus state machine for EEPROM access is implemented. TOC reading protocol for session/track info is present.

**JERRY integration exists.** `src/jerry/jerry.c` checks `ButchIsReadyToSend()` in the I2S callback and calls `SetSSIWordsXmittedFromButch()` to transfer CD audio.

**Init/Reset/Done lifecycle is wired up.** `CDROMInit()` at `JaguarInit()`, `CDROMReset()` at `JaguarReset()`, `CDROMDone()` at `JaguarDone()`.

**CD EEPROM save/load exists.** `src/jerry/eeprom.c` maintains separate `cdromEEPROM[64]` and save/load functions for CD EEPROM data.

### 4.3 What Does NOT Work (Critical Gaps)

**1. CDIntf layer is completely stubbed (`src/cd/cdintf.c`).** All functions return failure/dummy values. `CDIntfInit()` returns `false`. `CDIntfReadBlock()` returns `false`. `CDIntfGetSessionInfo()` returns `0xFF`. This is the **single biggest blocker**. The layer was originally designed for `libcdio` physical drive access (per `docs/INSTALL`), but that dependency was removed for the libretro port. No disc image loading exists.

**2. BUTCHExec() is disabled.** `src/cd/cdrom.c` immediately returns via `#if 1` guard. Interrupt generation logic is present but commented out.

**3. CD BIOS is not loaded.** `libretro.c` only loads the cart BIOS at `$E00000` (line 902-904). No code path loads the CD BIOS. No detection of CD vs cart content.

**4. No disc image file loading.** `retro_load_game()` only handles raw ROM data. No CUE parser, BIN reader, or CHD support. No disc control interface registered.

**5. CD audio playback path has issues.** `GetWordFromButchSSI()` and `SetSSIWordsXmittedFromButch()` call the stubbed `CDIntfReadBlock()`. Byte-swapping and interleaving logic has known issues (comments about MYST CD word offset at line 707). The `cdBuf` handling uses a self-described "crappy kludge" reading two sectors and splicing them.

**6. Settings not wired for CD.** `settings.h` has `CDBootPath[MAX_PATH]` but it is never populated. No core option for CD BIOS type or CD unit emulation.

### 4.4 Upstream Virtual Jaguar Status

The upstream Virtual Jaguar (shamusworld) has a "Use CD Unit" GUI option, but documentation states it "does not work 100% as intended" -- only the CD BIOS patterns appear on screen. Someone at AtariAge did work on CD-ROM code but it was built against outdated code and never fully merged upstream.

---

## 5. Reference Implementations

### 5.1 BigPEmu (by Rich Whitehouse)

Full Jaguar CD support since v1.05 (February 2023). Low-level hardware emulation using the CD BIOS and VLM as guides, without BIOS hooks. Entire retail CD library is compatible (first emulator to achieve this). Sub-channel data fully implemented, CD+G support, VLM fully functional. Supports CDI, BIN/CUE, proprietary .bigpimg. **Closed source** -- cannot reference implementation details. Key insight: the developer noted "appropriate events and delays" were essential.

### 5.2 Project Tempest

Abandoned since 2004. First emulator with any CD support, but only 2 games worked (Primal Rage and Baldies with sound distortion). **Closed source.** Minimal relevance.

### 5.3 MiSTer FPGA Jaguar Core

Active WIP by developer GreyRogue (updates as of July 2025). Improving JagCD support with better loading speed and VLM. **Open source** on GitHub (`MiSTer-devel/Jaguar_MiSTer`). FPGA/Verilog, not directly portable to C, but register-level behavior can serve as reference for Butch emulation accuracy. Potentially the **most useful open-source reference** available.

### 5.4 Jaguar CD Unleashed (AtariAge)

Community project: custom replacement BIOS that loads games from SD card. Documents CD BIOS behavior and boot sequence. Discussion on AtariAge forums provides insights into BIOS internals.

### 5.5 Virtual Jaguar Forks

No known forks have successfully implemented working CD support.

---

## 6. Jaguar CD Game Library

### 6.1 Commercial Releases (13 titles)

| # | Title | Notes |
|---|-------|-------|
| 1 | Baldies | Port of Amiga/PC strategy |
| 2 | Battlemorph | Sequel to Cybermorph |
| 3 | Blue Lightning | Pack-in; Lynx conversion |
| 4 | Brain Dead 13 | FMV/QTE |
| 5 | Dragon's Lair | FMV/QTE classic |
| 6 | Highlander: The Last of the MacLeods | Action game |
| 7 | Hover Strike: Unconquered Lands | CD-enhanced version |
| 8 | Iron Soldier 2 | Also on cartridge |
| 9 | Myst | PC adventure port |
| 10 | Primal Rage | Fighting; CD audio |
| 11 | Space Ace | FMV/QTE classic |
| 12 | Vid Grid | Pack-in; music video puzzle |
| 13 | World Tour Racing | Racing |

Sources vary between 11-13 depending on counting of pack-ins and dual-format releases.

### 6.2 Testing Priority

1. **Blue Lightning** -- Pack-in, widely available, relatively simple
2. **Primal Rage** -- One of 2 games that worked in Project Tempest
3. **Baldies** -- Second game from Project Tempest (with audio issues)
4. **Myst** -- Data-intensive; referenced extensively in existing `cdrom.c` comments
5. **Battlemorph** -- Flagship CD title
6. **Iron Soldier 2** -- Also on cart, useful for A/B comparison

### 6.3 Homebrew

Approximately 20-30% of Jaguar homebrew is CD-based, with releases as recently as 2017.

---

## 7. Implementation Concerns and Blockers

### 7.1 Major Implementation Milestones

**Phase 1 -- Disc Image Loading (Foundation)**
1. Implement CUE/BIN parser to read disc images
2. Implement `CDIntf` functions to read sectors from disc images
3. Parse TOC / session info from CUE sheets
4. Register libretro disc control interface
5. Add content detection for `.cue` / `.cdi` / `.chd` files in `retro_load_game()`

**Phase 2 -- CD BIOS Boot**
6. Load CD BIOS into memory at `$E00000` when CD content detected
7. Ensure 256 KB BIOS mapping is correct (may need `$E00000-$E3FFFF`)
8. Set appropriate boot vectors for CD mode
9. Ensure DSP is enabled (CD games require it)
10. Validate BIOS intro screen appears

**Phase 3 -- Butch Emulation Completion**
11. Enable and fix `BUTCHExec()` interrupt generation
12. Implement proper FIFO emulation with timing
13. Fix CD data sector delivery to GPU via FIFO
14. Implement subcode data delivery
15. Handle DSA command/response protocol correctly for all commands

**Phase 4 -- CD Audio**
16. Fix `GetWordFromButchSSI()` / `SetSSIWordsXmittedFromButch()` to use disc images
17. Implement proper I2S streaming from disc audio tracks
18. Handle mixed data+audio playback
19. Address byte-swapping / word-offset issues noted in existing code

**Phase 5 -- Polish and Compatibility**
20. CD EEPROM save/load properly
21. CDI format support (or rely on conversion tools)
22. CHD format support via libchdr
23. Game-specific fixes
24. Core options for CD-related settings

### 7.2 Technical Challenges

**Timing and Synchronization:** CD access timing is critical. BigPEmu's developer specifically noted "appropriate events and delays" were essential. The existing `BUTCHExec()` was disabled because "the whole IRQ system needs an overhaul" for cycle accuracy. The current event system runs on scanline boundaries, which may not be granular enough for CD FIFO interrupts.

**FIFO Emulation:** Each FIFO appears to be 32 bytes. Data is read from FIFO_DATA and I2SDAT2 alternately, 8 reads at a time. Timing of FIFO half-full interrupts relative to GPU/DSP execution is critical.

**Disc Format Complexity:** The all-audio-tracks format means raw 2352-byte sector reads are needed (not standard CD-ROM mode reads). Multisession handling must correctly identify session boundaries. Encryption is handled by the BIOS, not the emulator, but encrypted data must be delivered correctly.

**Interrupt Routing:** Butch interrupts route through JERRY to the GPU. The existing comment notes "any interrupt that BUTCH generates would be routed to the GPU." Current interrupt system may not support this correctly.

### 7.3 Potential Blockers

| Blocker | Severity | Mitigation |
|---------|----------|------------|
| CDIntf completely stubbed | **Critical** | Must implement disc image reading; primary work item |
| BUTCHExec disabled | **High** | Enable and fix interrupt generation; may need event system work |
| BIOS not loaded for CD | **High** | Straightforward to add CD-mode detection and BIOS loading |
| Interrupt timing granularity | **Medium** | May need sub-scanline event scheduling for FIFO interrupts |
| CDI format parsing | **Medium** | Can defer to BIN/CUE initially |
| Byte-swapping in audio path | **Medium** | Known issue; comments document the problem |
| Undocumented Butch behavior | **Medium** | MiSTer FPGA core as reference; BigPEmu proves full emulation achievable |

### 7.4 What Can Be Reused

All memory mapping code, all Butch register definitions, CD command protocol parsing, EEPROM handling, JERRY SSI integration, embedded BIOS ROMs, and the serial bus state machine.

### 7.5 What Must Be Written From Scratch

Disc image parser (CUE/BIN and optionally CDI, CHD), CDIntf implementation, CD content detection, BIOS loading path, libretro disc control callbacks, FIFO timing, and audio track extraction.

### 7.6 Performance Concerns

Performance impact should be minimal. Disc image reading is fast file I/O. Butch emulation is lightweight register I/O. FIFO/interrupt handling adds event callbacks at low frequency. CD audio mixing reuses the existing DAC/SSI path.

---

## 8. Testing Strategy

### 8.1 BIOS Boot Validation

Load CD BIOS at `$E00000` with no disc. Verify intro animation appears. Verify BIOS reaches "Insert CD" state gracefully. Monitor Butch register accesses.

### 8.2 Disc Loading Validation

Load BIN/CUE of Blue Lightning. Verify TOC parsing. Monitor DS_DATA commands.

### 8.3 Game Boot Validation

Verify BIOS reads session 2 boot data. Verify decryption succeeds. Verify game reaches title screen.

### 8.4 CD Audio Validation

Test with Primal Rage (Red Book audio). Verify I2S path delivers audio. Test mixed audio.

### 8.5 Regression Testing

All existing cartridge games must continue to work. The CD subsystem is already initialized for all games. Extend `test/regression_test.sh` to cover CD BIOS boot.

### 8.6 Test Data

BIN/CUE from Redump set (28 verified dumps). Embedded CD BIOS for independent testing. MYST sector dump in `src/cd/cdrom.c` comments as known-good reference data.

---

## 9. Implementation Roadmap

### Estimated Effort

| Phase | Description | Effort | Dependencies |
|-------|-------------|--------|--------------|
| 1 | Disc image loading (CUE/BIN) | 2-3 weeks | None |
| 2 | CD BIOS boot | 1 week | Phase 1 |
| 3 | Butch emulation completion | 2-3 weeks | Phase 2 |
| 4 | CD audio | 1-2 weeks | Phase 3 |
| 5 | Polish and compatibility | 2-4 weeks | Phase 4 |
| **Total** | | **8-13 weeks** | |

### Quick Win Path

Fastest path to a visible milestone: implement CUE/BIN parser (borrow from beetle-pce or genesis-plus-gx), wire up CDIntf, load embedded CD BIOS when .cue is opened, enable BUTCHExec (rough timing), target Blue Lightning as first bootable title.

### Recommended First PR

Phase 1 only: disc image loading and CDIntf implementation, with no behavioral changes to existing emulation. Testable by verifying CDIntfInit returns true, CDIntfReadBlock returns sector data, and TOC functions return correct info -- without booting a CD game.

---

## Summary of Key Findings

1. **The codebase is much further along than expected.** Roughly 60-70% of the CD subsystem infrastructure exists -- memory mapping, Butch registers, command protocol, JERRY integration, embedded BIOS ROMs, EEPROM handling. The core gap is the disc image reading layer.

2. **The critical blocker is `src/cd/cdintf.c`** -- every function is stubbed to return failure. Implementing real disc image reading here unlocks everything else.

3. **BigPEmu proves full compatibility is achievable.** The entire 13-title commercial library works in BigPEmu with a low-level approach. The closed-source MiSTer FPGA core is the best available open reference.

4. **The disc format is unusual** -- all tracks are audio, multisession, raw 2352-byte sectors. This requires a parser that handles audio CD images, not standard CD-ROM data images.

5. **Estimated total effort is 8-13 weeks** for a developer familiar with the codebase, with disc image loading as the foundational first step.

### Key Codebase Files for Implementation

- `src/cd/cdintf.c` -- Primary implementation target (currently stubbed)
- `src/cd/cdrom.c` -- Butch emulation (needs BUTCHExec enabled, FIFO timing)
- `libretro.c` -- Content detection, BIOS loading, disc control interface
- `src/core/jaguar.c` -- BIOS loading path in JaguarReset()
- `src/core/settings.h` -- CD-related settings
