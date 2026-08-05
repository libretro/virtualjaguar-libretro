# Jaguar GameDrive (JagGD) interface notes — research for #312

Status: phases 1+2 of §9 are implemented (`src/core/jaggd.c`, core option
`virtualjaguar_jgd`, savestate v8, `test/test_jgd.c`); the GD menu, SD/FAT
filesystem, GPU async reads, encrypted `.jgd` and `.MRQ` remain out of scope.
This document is the hardware/protocol spec of record for that
implementation, at the quality bar of `docs/memory-track.md`.

**Primary ground truth: RetroHQ's own published homebrew API**
(<https://github.com/RetroHQ/JagGD>, by SainT, the JagGD author). Unlike the
Memory Track, we did NOT have to reconstruct this interface from third-party
behavior — the cart-side 68K source (`gdbios_bindings.s`) is public and contains
the SPI register addresses, bit definitions, the full handshake protocol, and
the bank-switching semantics in SainT's own comments. Local copies of
everything cited live in the research scratchpad
(`.../scratchpad/jaggd-api/`, `.../scratchpad/jagstudio/`,
`.../scratchpad/bigpemu_disasm.txt`).

Confidence labels used throughout:
- **established** — stated in RetroHQ source (`gdbios_bindings.s` /
  `gdbios.h` / `README.md`) or directly observable in shipped JagStudio code
  (`RAPTORGD.O`, disassembled and verified identical to the bindings).
- **established-behavioral** — read out of the BigPEmu 1.2x arm64 binary
  (`/Applications/BigPEmu.app/Wrapper/BigPEmu.app/BigPEmu`, disassembled) or
  its changelog. Tells us what the ecosystem's reference emulator does, not
  necessarily what silicon does.
- **inferred** — follows from the above but not directly stated.
- **guessed** — plausible, must be verified before relying on it.

---

## 1. Hardware model

The JagGD is a flash cart with (established, from the API source + product
materials):

- **16 MB of onboard SDRAM** presented to the Jaguar as cartridge ROM.
- An **ASIC/FPGA** that decodes the cart bus, maps SDRAM into the 6 MB cart
  window, and exposes a small SPI-style mailbox register set.
- A **microcontroller** ("the GD", "the micro" in SainT's comments) behind
  that SPI link. It owns the SD card, the FAT filesystem, serial numbers, the
  menu, firmware, and it *serves the GDBIOS blob on request*.

Communication model: the 68K talks to the micro through three registers in
JERRY's **GPIO2** decode range. Cart EEPROM already works exactly this way
through GPIO0/GPIO1 ($F14800/$F15000, see `src/jerry/eeprom.c`), so the GPIO
chip selects demonstrably route to the cartridge connector; GPIO2 is the next
one up and is marked "reserved" in our own jerry.c address map (established by
analogy + the fact that the GD API uses these addresses from cart code).

## 2. Register map

All from `gdbios_bindings.s` (established) unless noted.

| Address | Name (RetroHQ) | Access | Meaning |
|---|---|---|---|
| `$F16002` | `ASIC_SPI_STATUS` | R/W 16-bit | Status + control, bits below |
| `$F16004` | `ASIC_SPI_DATA` | R/W 16-bit | Write = trigger one SPI byte exchange; word read = drain rx latch |
| `$F16005` | `ASIC_SPI_DATA_BYTE` | R 8-bit | Received byte (low byte of `$F16004`) |

`ASIC_SPI_STATUS` bits (established):

| Bit | Mask | Name | Direction | Meaning |
|---|---|---|---|---|
| 0 | `$0001` | `SLAVE_SELECT` | W | Assert slave select (ack packet start) |
| 3 | `$0008` | `HAVE_DATA` | R | Handshake bit; see GDWaitData below. Idle/ready state is **clear**; the slave raises it to ack/announce data |
| 4 | `$0010` | `PACKET_START` | W | Request packet start / block boundary ("process it, GD!") |
| 5 | `$0020` | `LATCH_FULL` | R | A received byte is sitting in the rx latch (must be drained before starting a transaction) |
| 15 | `$8000` | (busy) | R | Exchange in progress. All waits are `tst.w $F16002` / `bmi` loops, i.e. spin while bit 15 set |

Write of `$0000` to `$F16002` = end of packet / deselect (established — every
transaction ends with `clr.w ASIC_SPI_STATUS`).

**Data register semantics** (established from code shape; exact tx byte order
inferred): writing **any word** to `$F16004` clocks **one byte exchange**. The
transmitted byte is the **low byte of the written word** (inferred — see
`GDExchangeWord` trace below; the bulk-read loop writes its loop counter as a
don't-care dummy, so the written value is irrelevant when only receiving).
After bit 15 clears, the received byte is read at `$F16005`.

`GDExchangeWord` (send a 16-bit word, receive a 16-bit word — established):

```
move.w  d0,$F16004        ; exchange byte #1
(wait not busy)
move.b  $F16005,d0        ; rx1 -> low byte
ror.w   #8,d0             ; rx1 -> high byte, old high byte -> low
move.w  d0,$F16004        ; exchange byte #2 (tx = old high byte)
(wait not busy)
move.b  $F16005,d0        ; result = rx1<<8 | rx2
```

`GDWaitData` (the packet handshake — established):

```
1. spin until HAVE_DATA (bit 3) == 0
2. write PACKET_START|SLAVE_SELECT ($0011) to $F16002
3. spin until HAVE_DATA == 1        ; slave ack
```

**Consequence for a machine with no GD attached:** step 3 never completes.
There is no timeout anywhere in the RetroHQ or JagStudio code. See §6.

## 3. Micro packet protocol

Transaction shape (established, from `_GD_HWVersion` and `_GD_Install`):

```
write $0010 to STATUS            ; request packet start
GDWaitData                       ; handshake
GDExchangeWord(command)          ; 16-bit command word
GDExchangeWord(param_size)       ; 16-bit param byte count (0 if none)
[param bytes...]
write $0010 to STATUS            ; "we're done, process it GD!"
--- response ---
GDWaitData
GDExchangeWord / byte loop       ; response payload, format per command
write $0010 to STATUS            ; after each ≤512-byte block
...
write $0000 to STATUS            ; end of packet
```

Known command words (established):

| Cmd | Function | Response |
|---|---|---|
| `12` (`$0C`) | HW version | u32: high word = **firmware version, BCD** (e.g. `$0111` = v1.11), low word = ASIC version, BCD. Read as two `GDExchangeWord`s, high word first. After reading, the caller busy-waits ~500 dbra iterations "to let the micro finish up" |
| `$80` | Fetch GDBIOS | u16 blob size, then the blob streamed in ≤512-byte chunks, one `PACKET_START` write between chunks; each byte clocked out by a dummy word write to `$F16004` |

Before `$80`, the installer drains the rx latch: while `LATCH_FULL` set, read
`$F16004` (word) and re-check ("in case of FIFO DMA termination",
established).

All other micro commands (file I/O, serials, LED, reset, page set, …) are
issued **by the GDBIOS blob**, not by game code, and their SPI encodings are
**unknown** — and irrelevant for us, because the blob itself comes from the
emulated micro, i.e. *we get to supply our own blob* (see §7). BigPEmu
necessarily does the same thing (it cannot ship RetroHQ's copyrighted blob).

## 4. GDBIOS blob ABI (what games actually link against)

Games do not poke registers for anything except the two commands above. They
call `GD_Install(buffer)` (a 4 KB, long-aligned RAM buffer), which:

1. Calls HW version (cmd 12); **fails if firmware < `$111` (v1.11)** —
   established, both in RetroHQ bindings (`MINVERSION`... actually the FW
   gate) and JagStudio's `RAPTOR_GD_Init` (identical code, verified by
   disassembling `RAPTORGD.O`).
2. Streams the blob (cmd `$80`) into the buffer, stores the pointer
   (`GDB_Base`).
3. Checks blob version word ≥ `$100`, then calls blob function 1 (`GD_Init`).

Blob memory layout (established):

| Offset | Size | Meaning |
|---|---|---|
| `+0` | u16 | GDBIOS version, BCD, min `$100` |
| `+2` | u16 | Highest function number implemented |
| `+N*4` | 4 bytes | Entry point of function N (a 4-byte slot — enough for one `bra.w`/`jmp` stub) |

Call convention (established): `a6` = blob base; caller does
`cmp.w #N,2(a6)` / `blt fail` then `jsr N*4(a6)`. `d0-d1/a0-a1` scratch,
everything else callee-saved. Function numbers and register ABI:

| N | Name | Args in | Returns (d0) |
|---|---|---|---|
| 1 | `GD_Init` | — | — |
| 2 | `GD_InitGPURead` | a0=GPU RAM buffer (224 bytes), d0=flags (0 preserve / 1 fast) | — |
| 3 | `GD_BIOSVersion` | — | u16 BCD |
| 4 | `GD_ROMWriteEnable` | d0=1 enable / 0 disable | — |
| 5 | `GD_ROMSetPage` | d0 = page<<16 \| bank | — |
| 6 | `GD_ROMSetPages` | d0 = u32, one bank nibble per page, LSN = page 0 | — |
| 7 | `GD_GetCartSerial` | a0=16-byte buffer | 0 ok / !0 fail |
| 8 | `GD_GetCardSerial` | a0=16-byte buffer | 0 ok / !0 fail |
| 9 | `GD_CardIn` | — | 0 no / 1 yes |
| 10 | `GD_FileOpen` | a0=path, d0=mode | handle ≥0 / <0 fail |
| 11 | `GD_FileClose` | d0=handle | 0 / <0 |
| 12 | `GD_FileSeek` | d0 = flags<<16 \| handle, d1=offset | 0 / <0 |
| 13 | `GD_FileRead` | d0 = flags<<16 \| handle, a0=buffer, d1=size | 0 / <0 |
| 14 | `GD_FileWrite` | d0=handle, a0=buffer, d1=size | 0 / <0 |
| 15 | `GD_FileTell` | d0=handle | offset / $FFFFFFFF |
| 16 | `GD_FileSize` | d0=handle | size / $FFFFFFFF |
| 17 | `GD_FileAsyncPos` | — | pos |
| 18 | `GD_FileAsyncWait` | — | — |
| 19 | `GD_FileAsyncActive` | — | 0/1 |
| 20 | `GD_FileInfo` | a0=path, a1=buffer, d0=flags (bit0 = long name) | ≥0 / <0 |
| 21 | `GD_DirOpen` | a0=path | handle / <0 |
| 22 | `GD_DirRead` | d0 = flags<<16 \| handle, a0=buffer | ≥0 / <0 |
| 23 | `GD_DirClose` | d0=handle | 0 / <0 |
| 24 | `GD_Reset` | d0 = 0 normal / 1 to GD menu / 2 debug | — |
| 25 | `GD_SetLED` | d0=flags | — |
| 26 | `GD_DebugString` | a0=C string (→ virtual USB COM port) | — |

`GD_HWVersion` is **not** a blob function — it is raw SPI in the binding and
works pre-install.

## 5. Bank switching semantics

Verbatim from SainT's comments in `gdbios_bindings.s` (established):

```
ROMSetPage(u16 page, u16 bank)
  Page 0 : $8xxxxx      1 : $9xxxxx      2 : $axxxxx
       3 : $bxxxxx      4 : $cxxxxx      5 : $dxxxxx
  Bank 0-15 is 1MB pages of the onboard 16MB SDRAM

ROMSetPages(u32 banks)
  Set all SDRAM banks at once, one per nibble.
  Lowest significant nibble is page 0, upto nibble 5. Data above is ignored.
```

So:

- **Granularity:** 1 MB. The 6 MB cart window ($800000–$DFFFFF) is six
  independent 1 MB pages, each mappable to any of sixteen 1 MB banks of the
  16 MB SDRAM. (established)
- **Window:** whole cart space. Note page 5 covers $Dxxxxx including the
  $DFFF00 BUTCH-overlay tail — for emulation, keep our existing
  $DFFF00–$DFFFFF handling above the banked read, exactly as today's dispatch
  already orders it. (inferred, trivially safe)
- **Latch behavior:** immediate on the next bus access; a page register is
  plain combinational address-translation state in the ASIC. No evidence of
  any delay or double-buffering. (inferred)
- **Initial mapping:** identity (page N → bank N) so that a flat ≤6 MB image
  looks like a normal cart, and a >6 MB image exposes its first 6 MB.
  (inferred — required for anything to boot at all; BigPEmu boots >6 MB
  images this way per its changelog)
- **Writes:** `GD_ROMWriteEnable(1)` makes the SDRAM-backed "ROM" writable
  (the GD menu uses this to load; homebrew can use cart space as extra RAM).
  Granularity global vs per-page unknown; assume global. (established that it
  exists; granularity guessed)
- **How the real blob performs the switch is unknown** (SPI command to the
  micro vs. a direct ASIC register). Deliberately irrelevant to us: games can
  only reach banking through the blob ABI, and the emulator supplies the
  blob. (established consequence)

## 6. GD detection — what a GD-locked title actually does

From JagStudio v1.11 (`RAPTORGD.O` disassembled; `buildfiles/template/rapapp.s`;
`projects/basic/jaguargd/`) — JagStudio is what real GD homebrew (e.g. SKYLAR)
is built with (established):

1. Projects opt in with `useGD=1`; startup then calls `RAPTOR_GD_Init`
   **unconditionally, before DSP setup** ("MUST be before DSP setup").
2. `RAPTOR_GD_Init` = RetroHQ's `GD_Install` with a detect flag:
   `raptor_GD_detect = 1`, run HW-version (cmd 12); if FW < `$111` →
   `GDB_Base = 0`, `detect = -1`, return. Otherwise install blob + `GD_Init`.
3. Game code branches on `raptor_GD_detect == 1` ("GD Cart Detected") — and
   every wrapped call re-checks `GDB_Base != 0` and the function-count word,
   returning -1 gracefully when absent.

**The failure mode without a GD is a hang, not a refusal**: `GDWaitData`
step 3 spins forever waiting for `HAVE_DATA` to go high. On our core today,
`$F16002` reads fall into the `F14000–F1A0FF` catch-all in
`src/jerry/jerry.c` → `EepromReadWord` → returns `$0000` for non-EEPROM
offsets — bit 3 never rises, the 68K wedges in `.waitAck`, and `crash_detect`
will report `video_stall`. BigPEmu with JGD off behaves the same way (its GD
register special-case is gated on a JGD-enabled flag bit; disabled, the read
falls through to a flat memory array) — which is exactly why Batocera tells
users GD-locked games "won't boot" until they turn on Force JGD.

So the **detection contract we must implement** is precisely:
status handshake (bit 3 sequencing + bit 15 busy + bit 5 drain), command 12
returning FW ≥ `$111` (recommend `$0300`/`$0102`-style current-ish BCD values;
anything ≥ `$0111` works), and command `$80` serving a valid blob.

What we do *not* have to implement for detection: serial numbers can return
zeros-with-success (titles hard-locked to one physical cart serial are
distributed as encrypted `.jgd` files we can't load anyway — out of scope,
same as BigPEmu).

## 7. What BigPEmu does (established-behavioral)

From its changelog (mirrored at emunations.com) and the arm64 binary:

- v1.01 "Added JaguarGD bank switching support." v1.02 "Added an option to
  force Jaguar GD emulation, which enables bank switching even in ROM images
  smaller than 6MB" (config key `ForceJGD`; sits alongside `AttachButch`,
  `AttachMT`, `ShareMT`, `CDSeekSpeed` in `BigPEmuConfig.System`). v1.19
  "Added more Jaguar GD functionality. Filesystem functions are stubbed out…"
- Its JERRY byte/word read+write handlers special-case addresses ≥ `$F16002`
  behind a JGD-enable flag. The status read is synthesized: `LATCH_FULL` is
  set from a response-FIFO head≠tail comparison, and the read has
  side effects on the stored status bits (read-sensitive handshake
  emulation). `PACKET_START` writes raise a "process packet" flag consumed by
  a command engine that fills the response FIFO. I.e. **BigPEmu emulates the
  GD at the SPI-register level for detection/install**, matching §2–§3.
- GD *function* services are HLE'd host-side: its file-open paths read the
  filename out of emulated RAM and call host filesystem/`wcslen` code — i.e.
  the blob it serves is a thin trampoline into native handlers. (This is the
  v1.19 "filesystem stubs" layer.)
- Auto-enable: banking turns on automatically for images ≥ 6 MB; `ForceJGD`
  covers smaller GD-locked images. Our proposed core option
  `virtualjaguar_jgd = disabled|auto|enabled` maps 1:1.
- `.MRQ` (GD box-art/metadata sidecar) support is pure UI, ignore.

## 8. Remaining unknowns, and the cheapest experiment for each

| Unknown | Impact | Cheapest resolution |
|---|---|---|
| Exact tx byte order of `$F16004` word writes (low-byte-out inferred) | None if we implement to satisfy the binding code shape (we are the only slave); matters only for exotic homebrew doing raw SPI | Write a 20-line probe ROM from the bindings, run under BigPEmu with its scripting/debugger tracing `$F16004`; or ask SainT on AtariAge |
| Full micro SPI command set beyond 12/`$80` | None for scope of #312 (blob is ours) | Dump the real blob: run `GD_Install` on real hardware and save the 4 KB buffer to SD with `GD_FileWrite` — a ~30-line JagStudio program; also answers the next row |
| What the real GDBIOS `ROMSetPage` pokes | None (same reason); would be nice for a hardware-faithful mode | Same blob dump, disassemble |
| Initial page map on boot (identity assumed) | Boot of >6 MB images | Run SKYLAR 1.0 in BigPEmu, break before first `ROMSetPage`, checksum the six pages against file offsets |
| `ROMWriteEnable` granularity (global assumed) | Writable-cart feature only | Blob dump / SainT |
| Stock-console (no GD) read value at `$F16002` | Only affects how faithfully "GD absent" hangs; our current `$0000` reproduces the observed hang class | Real-hardware probe; not needed for #312 |
| Whether any homebrew calls raw SPI beyond `GD_HWVersion` | Compatibility ceiling | Corpus scan for `$F160` word constants in ROM images once we have GD titles locally |

None of these block implementation.

## 9. Implementation sketch (mapped onto our code)

New `src/core/jaggd.c` + `jaggd.h` (mirror `memtrack.c` structure):

- **State:** `jgdEnabled` (from core option `virtualjaguar_jgd =
  disabled|auto|enabled`; `auto` = on when image > `0x5FFF00` bytes),
  `uint8_t jgdPage[6]` (init 0..5), full-image buffer `jgdROM` (≤16 MB;
  `jaguarMainROM`/`jagMemSpace` cart region stays as-is for the non-GD path),
  SPI engine: `status`, small state machine (IDLE → HANDSHAKE → CMD → LEN →
  PARAMS → RESPONSE), response byte-FIFO, `rxLatch`.
- **Register hook:** in `src/jerry/jerry.c`, intercept `$F16000–$F16007`
  in `JERRYReadByte/Word` and `JERRYWriteByte/Word` **before** the
  `F14000–F1A0FF` EEPROM catch-all (which currently swallows these
  addresses), delegating to `JGDControlRead/Write` when `jgdEnabled`.
  Behavior per §2–§3: implement exactly the sequence the bindings perform;
  respond to cmd 12 with FW `$0300` / ASIC `$0102` (any BCD ≥ `$0111` FW),
  cmd `$80` with our blob; unknown commands → zero-filled responses.
- **The served blob:** ~200 bytes of hand-written 68K, embedded via bin2c
  like `src/bios/*` (source checked in under `src/bios/` with its build
  script, same pattern as the CD boot stub). Header `$0100`, func count 26.
  Implemented: 1 (rts), 3 (version), 4 (write enable), 5/6 (page set),
  7/8 (zero serial, return 0), 9 (return 0), 24 (write to reset backdoor —
  or just rts initially), 25/26 (rts); file/dir/async functions return -1
  (`moveq #-1,d0; rts` — BigPEmu also only "stubs" these; genuinely
  implementing them against a host directory is possible later via the same
  backdoor). Banking mechanism for the blob: functions 5/6 write the packed
  bank value to a **backdoor register we define at `$F16006`** (word write,
  inside the GPIO2 range we already own; nothing else decodes it). This
  keeps the blob 68K-only and trivially small, and keeps all interpretation
  in `jaggd.c`. `retro_deinit` must reset all of this (iOS static-state
  rule).
- **Cart read path:** in `src/core/jaguar.c` `JaguarReadByte/Word/Long`
  cart branches (and the DMA/`JaguarRead*`-alike helpers at lines ~659-690),
  when `jgdBankingActive`:
  `off = address - 0x800000; return jgdROM[((uint32_t)jgdPage[off >> 20] << 20) | (off & 0xFFFFF)]`
  — one table lookup + shift, same cost class as the existing MEMTRACK
  gate. Word/long reads that could straddle a 1 MB boundary: only possible
  for the odd-address/boundary cases; handle by composing bytes (straddling
  reads are already rare/degenerate on real carts). `GD_ROMWriteEnable`
  makes the same translation apply in `JaguarWriteByte/Word` cart branches
  (write into `jgdROM`).
- **Loader:** `src/core/file.c` `JaguarLoadROM`/callers currently cap at
  the cart window; accept up to `0x1000000` (16 MB), copy first 6 MB into
  `jaguarMainROM` for the flat path, keep full image in `jgdROM`, set
  auto-enable. (Alignment/padding to 1 MB multiple.)
- **Savestate:** serialize `jgdEnabled(active)`, `jgdPage[6]`, SPI engine
  state + FIFO; single version bump per release policy
  (`docs/savestate-compat.md`). The 16 MB image itself is NOT serialized
  (same as cart ROM today) — but pages written via `ROMWriteEnable` are
  dirty state; simplest correct v1: serialize a dirty flag and, if any
  write occurred, the full written-page set. (Defer: most titles won't
  write.)
- **Memory map / RA:** `test/tools/test_memory_map.c` expects the current
  descriptor layout; cart descriptor stays the 6 MB window (it describes the
  68K address space, which is unchanged).
- **Out of scope** (match BigPEmu): GD menu, SD/FAT filesystem semantics,
  `GD_InitGPURead` GPU async reads (function 2 = rts; `GD_FREAD_GPU` modes
  are documented to not function without it, so returning -1 from FileRead
  keeps the contract honest), `.jgd` encrypted images, `.MRQ` sidecars.

Suggested implementation order: (1) register hook + detection handshake +
blob install — gets "GD Cart Detected" on the JagStudio example; (2) banking
+ >6 MB loader — gets SKYLAR; (3) savestate + core option plumbing + tests.

## 10. Test cases

| Title | What it exercises | Local availability |
|---|---|---|
| JagStudio `jaguargd` example | `GD_Install`, detect flag, serials, LED, file I/O — prints "GD Cart Detected"/"NOT Detected" on screen | **Yes** — prebuilt `build/jaguargd.bin` (BJL/RAM-load format) in the scratchpad JagStudio tree; rebuild to `.rom` with JagStudio (Windows/Wine or the shipped `jaggd-x64` tooling) for a cart image |
| SKYLAR 1.0 preview (JagFest 2024) | The real thing: 16 MB image, live `ROMSetPage` banking + GD file access | No — download from the AtariAge thread ("SKYLAR 1.0 - 16MB BankSwitching GameDrive ROM", topic 371072; needs the SKYLAR directory on "SD card" — file calls will return -1 under our stub, so expect partial function: boot+banking yes, streamed assets no |
| Own probe ROM (recommended) | Deterministic harness ROM built with rmac + the RetroHQ bindings: install, `ROMSetPages`, checksum each page window, report via screen color / RAM flag readable by `harness_dlsym` | Build ourselves — best regression asset, no distribution problem, drop into `test/roms/` |
| GD-locked commercial homebrew | The ForceJGD path for <6 MB images | None in the local corpus (scanned `test/roms/private` — no GD titles, nothing >4 MB unpacked). Names of specific GD-locked retail homebrew were not pinned down by public sources during this research; Batocera/BigPEmu docs confirm the class exists without naming titles. Acquire via AtariAge when needed; the probe ROM covers the mechanism in the meantime |

The local corpus check: `test/roms/private/ROMS` is a classic-era set (139
retail entries + PD folder); no GD-aware or >6 MB images. The two >4 MB files
are 7z archives (Ultra Vortek, PD collection).

## 11. Source index (provenance)

- `github.com/RetroHQ/JagGD` — `gdbios.h`, `gdbios_bindings.s`, `README.md`
  (SainT/RetroHQ, official). Registers, bits, protocol, blob ABI, page/bank
  semantics, GPU-read notes. Cloned to scratchpad `jaggd-api/`.
- JagStudio v1.11 (`reboot-games.com/jagstudio/releases/jagstudio-v1.11.zip`)
  — `buildfiles/raptor/RAPTORGD.O` (disassembled with capstone; byte-for-byte
  the same logic as the bindings + `raptor_GD_detect`), `RAPTORGD.INC`,
  `template/rapapp.s` (`useGD=1` init ordering), `projects/basic/jaguargd/`
  example. Scratchpad `jagstudio/`.
- BigPEmu macOS arm64 binary — JERRY handler special-cases at
  `0x1001d0a0c`/`0x1001d1f88`/`0x1001d22f8` (GD SPI status/data emulation,
  gated on a mode flag), GD filesystem HLE cluster near `0x10014b150`
  (host-side file ops on emulated-RAM filenames), config key block
  (`ForceJGD` next to `AttachButch`/`AttachMT`). Disasm at scratchpad
  `bigpemu_disasm.txt` (2M lines).
- BigPEmu changelog v1.01/v1.02/v1.19 via emunations.com mirror; BigPEmu user
  manual (richwhitehouse.com) for MRQ note.
- AtariAge topic 371072 (SKYLAR 1.0) — existence/shape of a real 16 MB
  banking title; JagStudio credit "Rik, CJ and SainT for implementing …
  Bankswitching / GameDrive files … in JagStudio".
- Batocera wiki `systems:jaguar` — user-facing confirmation that GD-locked
  games need Force JGD Emulation.
- This repo: `src/jerry/jerry.c` GPIO map comment (F16000-F16FFF = GPIO2
  reserved; F14000-F1A0FF catch-all currently swallowing GD addresses via
  `EepromReadByte` → `$0000`), `src/core/jaguar.c` cart dispatch + MEMTRACK
  overlay pattern, `src/jerry/eeprom.c` GPIO0/1 precedent that GPIO decodes
  reach the cart connector.
