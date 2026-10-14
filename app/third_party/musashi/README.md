# Musashi 68000 disassembler (vendored)

`m68kdasm.c` is the disassembler from [Musashi](https://github.com/kstenerud/Musashi) by Karl Stenerud (MIT license, see
`LICENSE`).  It is used only by the standalone app's CPU debugger (`app/src/debug/cpu_view.cpp`); it is compiled as C by
the app's CMake and is **not** part of the C89 libretro core build.

| | |
| --- | --- |
| upstream | https://github.com/kstenerud/Musashi |
| commit | `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd` (master, 2026-03-08) |
| file | `m68kdasm.c`, byte-for-byte unmodified (its header says "Version 3.32"; upstream's readme says 4.10) |
| license | MIT; the permission notice is also at the top of `m68kdasm.c` and in `LICENSE` |

Files here that are **ours**, not upstream's:

- `m68k.h` -- a minimal stand-in for Musashi's `m68k.h`.  The real one declares the entire 68000 emulator (`m68k_get_reg`,
  `m68k_register_t`, the memory callbacks...) which would collide with the core's `src/m68000/m68kinterface.h`.  The
  disassembler needs only the CPU-type enum, `M68K_ARRAY_LENGTH` and the three `m68k_read_disassembler_*` hooks.
  It renames every exported symbol to `vj_dasm_*` with `#define`s: the core itself defines `m68k_read_disassembler_*` (they read
  emulated memory) and `m68k_is_valid_instruction`, and both link into the app.  It also defines `M68K_COMPILE_FOR_MAME` as off: `m68kdasm.c` tests `M68K_COMPILE_FOR_MAME == M68K_OPT_ON`, which is
  true when both macros are undefined.
- `vj_m68kdasm.{h,c}` -- the wrapper.  It calls `m68k_disassemble_raw()` on a byte buffer the caller filled with
  `vj_debug_iface::peek`, so the disassembler never reads emulated memory and never touches a core global.  The three
  `m68k_read_disassembler_*` hooks are stubs (only the non-raw path uses them).

To update: copy a newer `m68kdasm.c` over the old one, update the commit above, rebuild, run `debug_cpu_unit`.
