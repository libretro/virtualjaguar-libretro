# Memory Track (Jaguar CD save cartridge)

The Memory Track is a 128 KB flash cartridge, sold separately in 1995, that
plugs into the Jaguar's cartridge slot **while the CD unit sits on top** — the
CD drive has its own pass-through slot. CD games use it to save settings,
progress and high scores.

Neither the Jaguar Technical Reference Manual nor the official SDK documents
it. Everything below was reconstructed from two projects, with thanks:

- **[cubanismo/skunk_mtrk](https://github.com/cubanismo/skunk_mtrk)** —
  preserves the **original Atari NVM BIOS v1.01 68K source** (`asmnvm.s`) and
  its February 1995 specification, *"Non Volatile Memory - Bios calls"*.
  Released by James Jones under CC0-1.0.
- **[MiSTer-devel/Jaguar_MiSTer](https://github.com/MiSTer-devel/Jaguar_MiSTer)** —
  `Jaguar.sv` carries the flash detection protocol and address map in
  comments. The only known-working open-source hardware implementation.

BizHawk's Jaguar core is a Virtual Jaguar port and shares our original gaps.

## Two layers

Games do **not** talk to the flash chip. They call a RAM-resident BIOS module,
which talks to the flash. Both layers have to work.

### Layer 1 — the flash device

| what | where |
|---|---|
| NVRAM data | **`$900000`–`$91FFFF`** (128 KB, plain 16-bit window) |
| Unlock addr 1 | `$815554` (`$800000 + 4*$5555`) |
| Unlock addr 2 | `$80AAA8` (`$800000 + 4*$2AAA`) |
| Manufacturer ID | `$800000` → `$1F` (ATMEL) or `$01` (AMD) |
| Device ID | `$800004` → `$D5` (AT29C010) or `$20` (AM29F010) |

Unlock sequence: `$AA` → `$815554`, `$55` → `$80AAA8`, then a command to
`$815554` — `$90` enters ID mode ("override memtrack flash in place of cart
rom"), `$F0` undoes it, `$A0` enables writes. After the unlock write,
`$80AAA8` reads back `$0055` (the "ROMULATOR" probe).

Two things that are easy to get wrong, and that we had wrong:

- **The save data is not in the `$8xxxxx` ROM window.** It is at `$900000`.
  Mapping it over `$8xxxxx` collides with the CD BIOS.
- **There is no MEMCON1/ROMWIDTH gating.** A plain cartridge sits at ROMWIDTH 2
  as its *normal* width, and CD content runs at ROMWIDTH 0 — so a ROMWIDTH test
  makes the device unreachable for discs. MiSTer gates purely on presence. The
  device must instead claim only its own addresses so everything else still
  reads the cartridge ROM or CD BIOS (`MTClaimsRead`/`MTClaimsWrite` in
  `src/core/memtrack.c`).

### Layer 2 — the NVM BIOS module

The CD BIOS boot copies a 2 KB module from the cartridge into RAM:

- **`$2400`** — magic cookie `'_NVM'` (`$5F4E564D`). This is how games detect
  the cartridge. Vid Grid's check is at `$012B72`: `cmpi.l #'_NVM',$2400.w`.
- **`$2404`** — dispatcher. `JSR` with an opcode and args on the stack.

Opcodes: 0 Initialize, 1 Create, 2 Open, 3 Close, 4 Delete, 5 Read, 6 Write,
7 SearchFirst, 8 SearchNext, 9 Seek, 10 Inquire. All return a 32-bit value in
`D0`; negative means error (`ENOINIT` −1, `ENOSPC` −2, `EFILNF` −3, `EINVFN`
−4, `ERANGE` −5, `ENFILES` −6, `EIHNDL` −7).

`Initialize` must be called first and takes the application name plus a 16 KB
scratch buffer the module may trash.

#### On-cart filesystem

- 512-byte blocks, 256 total. Blocks 0–7 (first 4 KB) are system.
- Block 0 is the FAT — one byte per block: `0` free, `1` end-of-chain, `2`
  empty file, otherwise the next block.
- Blocks 1–7 are the directory: 199 entries × 18 bytes —
  `startblock`(1) `numblocks`(1) `appname`(10) `filename`(6).
- Names pack 3 characters per 16-bit word from a 40-character set
  (`A-Z`, `0-9`, `:`, `'`, `.`, space); app names ≤ 15 chars, file names ≤ 9.
  Anything outside the set packs as space.
- Byte 0–1 of block 0 is a 16-bit checksum of bytes 2..511; byte 3 is the
  used-block count.
- A file is identified by **app name + file name**, so two games can use the
  same file name without colliding.

#### Quirks worth preserving

Games were written against the shipped module, so `src/core/nvmbios.c` keeps
its behaviour rather than an idealised version:

- **A bad checksum silently reformats** the FAT and directory and reports
  success — which is how a blank cartridge self-formats on first use.
- **Delete always reports success.** The module recalculates the checksum
  afterwards, clobbering `D0`, so even a missing file returns non-negative.
- **Close does no handle validation.**
- Only **3 file handles** exist; a 4th `Open` returns `ENFILES`.
- `file_offset` reports `ERANGE` for offset 0 on a zero-length file (an
  acknowledged bug in the original).

## In this core

| piece | file |
|---|---|
| Flash device + address claims | `src/core/memtrack.c` |
| NVM BIOS module (HLE) | `src/core/nvmbios.c` |
| Presence flag, dispatcher hook | `src/core/jaguar.c` |
| Option, install, save wiring | `libretro.c` |

Presence is CD content (or an explicitly loaded MT cart dump). The core option
`virtualjaguar_memory_track` (default enabled) lets you emulate a console
without the cartridge — games then warn that information cannot be saved,
which is correct hardware behaviour.

The dispatcher is hooked in the same pre-instruction callback the CD HLE jump
table uses, and runs in **both** boot modes: the module is RAM-resident on
hardware regardless of which CD BIOS booted the disc.

Save data lives in the CD save buffer alongside the cart and CD EEPROM banks,
so it round-trips through the frontend's `.srm`.

## Tests

- `test/test_memtrack.c` — flash device: address claims (including that the
  CD BIOS at `$800000` is *not* swallowed), ID reporting, write-enable gating,
  full 128 KB addressability without aliasing.
- `test/test_nvmbios.c` — filesystem: round trips, app-scoped naming, handle
  exhaustion, search, inquire accounting, every error code, and each quirk
  above.

Both run in `make test` and need no ROMs.

## Status

Vid Grid (USA) (Rev 1) is the first title confirmed to use it — it boots into
its level-select with a working Save option in both boot modes. Other CD
titles have not been individually surveyed; a title that never calls the
module simply never sees a cartridge, which is also what happens on hardware.
