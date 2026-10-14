# Debug tools in the standalone app

> Skeleton from WP1 (the core interface and the session).  The tool windows land in later work packages and fill in
> the sections marked *(WP n)*.  Design record: issues #652 (GDB stub) and #833 (app).

The app's **Tools** menu is a set of windows over the core's debug interface: memory viewer/editor, CLUT editor, cheat
scanner, CPU debugger (68000, GPU, DSP), Object Processor list, blitter log, texture browser.  None of it exists under
RetroArch: the interface is only handed to a frontend that asks for it (`VJ_CAP_DEBUG`, see
[`app-frontend-extension.md`](app-frontend-extension.md)).

## Pieces

| | file |
| --- | --- |
| the contract (C89, shared verbatim by core and app) | `src/debug/vj_debug_iface.h` |
| core side | `src/debug/vjdebug.c` (+ the halt pump in `src/debug/gdbtarget.c`, OP snapshot in `src/tom/op.c`, blit log in `src/tom/blitter_mmio.c`, `TexReplaceReload` in `src/tom/texreplace.c`) |
| app side, no ImGui | `app/src/debug/debug_session.{h,cpp}` |
| app side, ImGui | `app/src/debug/debug_ui.cpp` (Tools menu, halted bar), one `*_ui.cpp` per tool *(WP 2-7)* |
| a fake core for tests | `app/tests/debug_mock.h` |

## Halt model

All calls are made on the thread that runs `retro_run()`: between two `retro_run()` calls, or from inside the halt pump.

1. A breakpoint (or **Break**, F9) makes the core call `GDBHalt()`, which blocks **in place**, deep inside `retro_run()`.
   Every processor is frozen exactly where it stopped.
2. With an app host attached, `GDBHalt()` calls the host's `halt_pump` every iteration.  The app answers by running **one UI
   frame** (`App::haltedFrame()`: events, ImGui, present -- no emulation step) and returning the verdict set by the
   buttons: continue (F9), step (F10), detach.
3. While parked, `CoreHost::inFrame()` is true and everything that would re-enter the core is refused: state save/load,
   reset, loading or closing a game, option changes, controller and disc changes.  Audio is paused and cleared on the
   way out.  Closing the window while halted resumes the core and detaches.
4. Turning RetroAchievements **hardcore** on (from anywhere, including inside a halt) detaches, resumes and closes every
   tool; the Tools menu goes grey with the reason.

Without an attached host and without the GDB socket `GDBHalt()` returns at once, so a stale "break" can never hang the
core.  When the GDB TCP stub is on as well, either side may resume the machine; a TCP client disconnecting clears the
breakpoint tables as before (the app re-applies its list on the next attach).

## Memory and the poke policy

`peek` is `JaguarReadByte(..., DEBUGGER)`.  `poke` is all-or-nothing and policy-checked (see `VJ_DBG_POKE_*` in the header):
RAM-like regions are free, ROM and `$DFFF00-$DFFFFF` are never written, other registers need "Allow register writes",
and the six side-effecting registers need a second, separate confirmation.  Register space is written as aligned 16-bit
words.

## Tools

*(WP 2)* Memory viewer and CLUT editor.  *(WP 3)* Cheat scanner (results become ordinary cheats through `CheatManager`,
so hardcore blocks them like any other).  *(WP 4)* CPU debugger.  *(WP 5)* Object list.  *(WP 6)* Textures.  *(WP 7)* Blitter log.

## Testing

`debug_session_unit` (logic against the mock), `debug_e2e` (real core), and the core's `test/test_debug_iface.c`.
