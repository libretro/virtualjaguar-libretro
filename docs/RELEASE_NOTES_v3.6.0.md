# Virtual Jaguar libretro v3.6.0

**Debugger & libretro surface.** A GDB remote stub you can attach a real
`m68k-elf-gdb` to, the disk-control interface that makes booting with no content
and swapping a disc mid-session possible, a user-authorable patch/hook file, and
a broad SIMD pass that speeds up the parts of the emulator that were actually
hot.

91 commits since v3.5.1.

---

## Debugging

**GDB remote serial protocol stub** (#652). Attach `m68k-elf-gdb` (or lldb) to a
running core over TCP and debug Jaguar code the way you would any other target:

- Breakpoints, single-step, register and memory access.
- The **GPU and DSP appear as GDB threads**, so you can inspect all three
  processors from one session.
- Connect scripts, editor plugins, and an installer ship in the repo; see
  `docs/gdb-stub-design.md`.

The per-instruction cost of the stub was measured, attributed per processor, and
reduced — see "Performance" below.

## libretro surface

**Disk control interface** (#651). The core now implements
`SET_DISK_CONTROL_EXT_INTERFACE`, which was the shared blocker behind two
features:

- Launch the core with **no content** and insert a disc afterwards.
- Reach the **Virtual Light Machine** by putting in an audio CD.

Two things to know about how it behaves. **Inserting a disc restarts the
console** — the boot strategy is derived from the disc itself (its session layout
decides HLE versus real BIOS), so the core has to re-read and boot it; real
hardware does not need to, so a swap here is a restart rather than a continuous
session. And **eject is a frontend-level flag, not an emulated tray**: the Jaguar
CD unit has no modelled disc-present line, so the previously mounted disc stays
readable until an insert replaces it.

**Savestates are now tied to the disc they were taken on.** Since a disc can be
swapped mid-session, a state records which disc was mounted (session count, track
count, total sectors) and is refused against a different one, naming both in the
log. States written by earlier versions carry no such record and load exactly as
before.

**Auto frameskip driven by audio buffer status** (#690), and an **enhancement
profile** option (#706) — `auto` / `quality` / `performance` — deciding whether
the per-title database may switch on expensive visual enhancements by default.

**No-content boot** (#646): the core can launch with no cartridge or disc and
show the boot ROM, as real hardware does with an empty slot.

## Patching a game

**One routing doc** (#637). We shipped three overlapping ways to modify a game
with no single door: cheats, soft patching, and enhancement hooks. There is now a
single page — `docs/patching-a-game.md` — that routes between them, linked from
the README and from each of the three existing docs.

**User-authorable hooks.** Drop a `vj_hooks.txt` in your system directory:

```
crc=DC187F82
hook=my-patch 0x0012A4 4E71 4E75
```

Turn on the **Enhancement Hooks** core option (off by default; a hook file cannot
switch on its own gate). Every fence from the built-in table applies: the
`expect` bytes are mandatory and checked, application is all-or-nothing, and any
malformed line discards the whole file rather than applying a patch set we only
partly understood.

## Performance

**RISC idle-loop fast-forward is now on by default** (#608). Previously opt-in
while the compatibility corpus grew; a sweep of **148 cart images plus 6 CD
spot-checks** came back byte-identical off-versus-on across framebuffer, audio and
savestate hash streams, so the largest single speed-up the core offers now reaches
everyone. It was also **ported to the GPU interpreter** (#699), not just the DSP.

**SIMD.** NEON and SSE2 fast paths for the parts that measurement said were hot:
Object Processor phrase stores and 16bpp fixed bitmaps, TOM scanline converters
(direct/24bpp/RGB16), the hi-res Stage-1 store and line resolve, and the voice-chat
mixer. Plus **ARMv8 hardware CRC32** with runtime dispatch, and byte-swap
intrinsics for the `GET`/`SET` macros.

**Per-SoC ARM tuning** and LTO by default for ELF targets.

**GDB stub overhead measured and fixed.** The hooks cost 4.5%, not the ~0% first
reported; caching the armed flag in a slice-entry local took that to 2.7% with the
GPU's share to zero. The measurement gates are permanent and listed in
`BUILD_AXES`, so the attribution can be repeated on any target in four runs.

## Game fixes

**White Men Can't Jump** (#635) — froze at its menus and produced **no audio at
all**, in both BIOS modes. The game stops its DSP by raising a CPU interrupt whose
handler is a shutdown routine; delivering that interrupt to an already-stopped DSP
left it pending, so the freshly reloaded DSP program serviced it during its own
init and shut itself down. The 68K then waited forever on a mailbox nothing would
ever clear. Audio went from zero non-silent samples to 1.8M.

**Audio-only CDs route through the real CD BIOS** (#683), whose player front-end
is the VLM — HLE cannot boot an audio disc, since it synthesizes its boot stub
from session-2 data a music CD does not have.

**Object Processor**: the object window is clamped to the visible window rather
than VBB; object processing stops at VBB when VDE runs past the end of the field
(#641); scaled 24bpp line-buffer writes are clamped to `tomRam8` (#604).

**Blitter**: a nested blit dispatched from inside a running one is refused and
logged (#663, #677).

**Widescreen statics** reset per load (#605).

## Build and packaging

**`Package.swift` ships in this repo** (#614), so Apple frontends no longer need
to vendor a fork. It is guarded: a CI job builds it, and a second fails if its
source list drifts from `Makefile.common`.

**Cross-platform core installer** (#640) for getting the core into RetroArch.

**SPM builds no longer inline the scalar blitter on arm64** (#612) — the arch
autodetect now tests for actual SSE2/NEON availability rather than the arch alone.

## Documentation

A **settings and performance tuning guide** (`docs/settings-and-performance-guide.md`)
covering what each option costs and when to turn it on, plus a site page. The
timing-accuracy work is documented as a non-versioned ongoing campaign rather than
being promised for a particular release.

---

## Known limitations

- Inserting a disc restarts the console (see above). A continuous swap depends on
  CD BIOS disc-presence polling that has not been verified.
- The enhancement profile governs `internal_resolution` and `true_color` only —
  performance-oriented per-title defaults such as idle-skip are unaffected by it,
  deliberately.
- `docs/WHATSNEW` has no sections for v3.1.0 through v3.5.1; the per-release
  notes in `docs/RELEASE_NOTES_v*.md` are complete and authoritative for those.
