# Hardware model, libretro layer & JTRM reference

Detail for [`CLAUDE.md`](../../CLAUDE.md). **Verify clocks/register behavior against the JTRM,
never against source comments** — comments have historically been wrong (e.g. PIT clock was
incorrectly halved).

## Hardware model

Four processors, unified memory map, big-endian. `GET16/GET32/SET16/SET32` byte-swap on LE
hosts. Address-range map: header comment in `src/core/vjag_memory.c`; dispatch in
`src/core/jaguar.c`. RAM `0x000000` (2 MB), cart `0x800000`, TOM regs `0xF00000`, JERRY regs
`0xF10000`.

System clock: **26.590906 MHz NTSC / 26.593900 MHz PAL**. 68K = half (~13.3 MHz). GPU/DSP/PIT
run at full system clock.

- **68000** (13.3 MHz, `src/m68000/`) — main CPU, UAE-derived. `cpuemu.c` is machine-generated
  ~1.8 MB — never read whole; grep first, then `Read` with offset/limit on matched ranges.
- **GPU** (26.6 MHz RISC, `src/tom/gpu.c`) — graphics coprocessor.
- **DSP** (`src/jerry/dsp.c`) — same ISA as GPU; audio.
- **Object Processor** (`src/tom/op.c`) — sprite/bitmap rendering.
- **TOM** (`src/tom/tom.c`) — video, GPU, OP, Blitter (`src/tom/blitter.c`).
- **JERRY** (`src/jerry/jerry.c`) — audio DAC, DSP, timers, EEPROM.

Frame loop is event-driven (not cycle-accurate): `JaguarExecuteNew()` in `src/core/jaguar.c`
runs 68K to next event, then GPU, then fires callbacks (half-line render, timers).

## Libretro layer

`libretro.c` (top-level) implements the API. Video XRGB8888 dynamic res (320×240 NTSC /
320×256 PAL). Audio 48 kHz 16-bit stereo. Core options in `libretro_core_options.h` (blitter
mode, BIOS, NTSC/PAL, DSP, input).

## Source layout

- `src/core/` — orchestration, memory map, events, settings, files, cheats
- `src/tom/` — video, GPU, OP, blitter (+ SIMD)
- `src/jerry/` — audio, DSP, DAC, EEPROM, input, wavetable, UART/netlink (`uart.c` + `jlink.c`
  + `jlink_discover.c`; see `docs/netlink-design.md`, `docs/netlink-ux-design.md`,
  `docs/netlink-user-guide.md`)
- `src/cd/` — Jaguar CD: BUTCH/FIFO/DSA/Q-subcode in `cdrom.c`; image loading (CUE/BIN, CDI,
  CHD) in `cdintf.c`; BIOS auth bypass + boot stub in `src/core/jaguar.c`. CHD requires `CHSE`
  session tags from a post-2026-08 chdman — old internet CHDs refused; see `docs/jagcd-chd.md`,
  issue #322.
- `src/core/jaggd.c` — Jaguar GameDrive: SPI mailbox at `$F16000`, embedded GDBIOS blob,
  6×1MB page → 16-bank switching for images up to 16 MB (spec: `docs/jgd-interface-notes.md`)
- `src/core/titledb.c` — per-title enhancement defaults (#368), applied at option-read time in
  `libretro.c`, user values always win. `negative[]` (#464): known-bad `{key,value}` that refuse
  an unsafe per-title *default* (with warning) but never override explicit user choice. Ships
  zero negative rows. Contract in `docs/enhancement-hooks.md`.
- `src/core/titlehook.c` — per-title enhancement **hooks** (#370): verified byte patches into
  cartridge ROM at load, gated by `virtualjaguar_enhancement_hooks` (default **disabled**).
  Ships zero rows. Authoring rules + three fences (GameDrive banked image, cart entry vector
  `$400..$407`, `TitleHook*` needs own export-list entry) in `docs/enhancement-hooks.md`. Not a
  scripting surface: `{key,value}` fits → it's a `pairs[]` entry; a timing bug is never a hook.
- `src/bios/` — embedded BIOS / boot stubs
- `src/m68000/` — UAE 68K (machine-generated; treat as opaque)
- `libretro-common/` — shared utility lib
- `test/tools/` — test harnesses; `test/roms/private/` — commercial ROMs/BIOSes (gitignored)

## Distilled JTRM reference (`docs/jtrm-*.md`)

LLM-optimized reference for the Jaguar hardware. **Read before any hardware-accuracy decision;
always supersedes source comments.** But these files themselves are a **mix of manual-derived and
source-derived material, not a uniformly JTRM-verified set** (issue #522 audit, 2026-08-20): most
sections were originally distilled from `src/` rather than the manual, then cited back as if
manual-authoritative. Every section now carries a per-line tag saying which it is:

- `Source: <manual name> p.<N> "<section>" (...)` — read against the PDF and safe to cite as
  JTRM-authoritative.
- `Source: the original Flare/Atari TOM design netlists` — a legitimate primary source outside the
  manual; safe to cite.
- `Derived from: <src file> -- NOT verified against the JTRM` — distilled from the emulator's own
  source code. Treat it the same as an inline source comment: plausible, but **not** grounds for a
  hardware-accuracy decision on its own. If the decision matters, open the cited PDF page yourself.

Two verified disagreements between a `Derived from:` claim and the manual are recorded inline
where found (`jtrm-clocks-timing.md` CLK1/2/3 divider formula; `jtrm-jerry.md` wavetable entry
count) — read those notes before touching either area. Full TRM PDFs in `docs/atari-jaguar-1999/`
(gitignored — copyrighted).

- `jtrm-clocks-timing.md` — clock hierarchy, video timing, PIT formulas, memory map, bus priority
- `jtrm-register-map.md` — register addresses + bit fields (TOM, GPU, blitter, JERRY, DSP)
- `jtrm-gpu-dsp.md` — RISC ISA, pipeline, score-boarding, interrupts, MAC, wave table ROM
- `jtrm-blitter.md` — address generators, B_CMD, LFU truth table, modes of operation
- `jtrm-jerry.md` — PIT timers, JINTCTRL, I2S/DAC, UART, clock dividers, EEPROM
- `jtrm-object-processor.md` — object types, bit fields, display pipeline, colour space

True field rate: 524/624 halflines = 60.05445 / 50.08013 Hz; 59.94 is the INTERLACED rate.

## Known limitations

- Blitter not fully cycle-accurate (some games need fast mode).
- No bus contention modeling.
- VC register behavior not fully accurate.
