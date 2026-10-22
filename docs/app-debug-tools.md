# Debug tools in the standalone app

> Design record: issues #652 (GDB stub) and #833 (app).  The CPU debugger window (68000 / GPU / DSP registers with both
> RISC banks, disassembly, breakpoints, Break / Continue / Step / Step over / Run to cursor) is under **Tools > CPU
> debugger...**; F9 breaks or continues and F10 steps while halted (PR #924).

The app's **Tools** menu is a set of windows over the core's debug interface: Memory viewer and Palette (CLUT) editor,
Cheat scanner, Object list (Object Processor), Blitter log, Textures and an Audio scope.  None of it exists under
RetroArch: the interface is only handed to a frontend that asks for it (`VJ_CAP_DEBUG`, see
[`app-frontend-extension.md`](app-frontend-extension.md)).  Every window is a tool for developers and tinkerers; none
of them changes how the game plays unless you press an edit button.

## Pieces

| | file |
| --- | --- |
| the contract (C89, shared verbatim by core and app) | `src/debug/vj_debug_iface.h` |
| core side | `src/debug/vjdebug.c` (+ the halt pump in `src/debug/gdbtarget.c`, OP snapshot in `src/tom/op.c`, blit log in `src/tom/blitter_mmio.c`, `TexReplaceReload` in `src/tom/texreplace.c`) |
| app side, no ImGui | `app/src/debug/debug_session.{h,cpp}` (the UI thread's object: availability, breakpoints, the Tools registry) |
| app side, the core's half | `app/src/debug/debug_core.{h,cpp}` (owns the core's debug interface and wraps every entry point; emulation thread) and `debug_views.{h,cpp}` (the windows' requests and the buffers the emulation thread fills) |
| app side, ImGui | `app/src/debug/debug_ui.cpp` (Tools menu, halted bar, `addDebugTools`), one `*_ui.cpp` per tool, each with its pure logic beside it (`memory_view`, `cheat_scan`, `op_decode` / `op_view` / `op_render`, `texture_pack`, `blitter_log`, `audio_ring`) |
| a fake core for tests | `app/tests/debug_mock.h` |

## Halt model

All calls are made on the thread that runs `retro_run()` (the UI thread inline, the emulation thread when Settings > Interface > Emulation thread is on): between two `retro_run()` calls, or from inside the halt pump.

1. A breakpoint (or **Break**, F9) makes the core call `GDBHalt()`, which blocks **in place**, deep inside `retro_run()`.
   Every processor is frozen exactly where it stopped.
2. With an app host attached, `GDBHalt()` calls the host's `halt_pump` every iteration.  Threaded, the pump
   (`DebugCore::pump`) serves the debug lane, waits at most 4 ms and returns NONE, so the core's own loop keeps the gdb socket and
   the halt timeout; the UI mirrors the halt once per frame (`DebugSession::syncHalt`) and its verdict (continue F9, step F10,
   detach) is tagged with the halt serial it saw, so a verdict for a halt that is gone is dropped.  Inline the pump runs one UI
   frame (`App::haltedFrame()`: events, ImGui, present -- no emulation step) as before.  A halt gdb or the halt timeout ends is
   closed by `frameEnded()` after the frame, and a STEP the core never re-halted (the stepped instruction ended the frame) is
   closed the same way.
3. While parked, anything that would re-enter the core is held or refused.  Frame-lane commands (option changes, cheats,
   controller, SRAM autosave) wait in the queue and apply after Continue; the halt bar and the Core options tab say so.  Save/load
   state and boot refuse with a toast.  Open, close, reset, disc changes and quit detach first (threaded) and then proceed;
   inline they keep refusing.  Every UI call into the core (`EmuThread::call`, `advance`) gives up when a halt begins, so
   the window never waits on a parked core.  Audio is paused and cleared on the way out.  Closing the window while halted
   resumes the core and detaches (`beginQuit()` ends the pump within one wait).
4. Turning RetroAchievements **hardcore** on (from anywhere, including inside a halt) detaches, resumes and closes every
   tool; the Tools menu goes grey with the reason.

Without an attached host and without the GDB socket `GDBHalt()` returns at once, so a stale "break" can never hang the
core.  When the GDB TCP stub is on as well, either side may resume the machine; a TCP client disconnecting clears the
breakpoint tables as before (the app re-applies its list on the next attach).  A headless run has no UI to release a halt:
a halt gdb holds waits for the gdb client or a quit signal (below); a halt the app itself caused is released at once.

## Memory and the poke policy

`peek` is `JaguarReadByte(..., DEBUGGER)`.  `poke` is all-or-nothing and policy-checked (see `VJ_DBG_POKE_*` in the header):
RAM-like regions are free, ROM and `$DFFF00-$DFFFFF` are never written, other registers need "Allow register writes",
and the six side-effecting registers need a second, separate confirmation.  Register space is written as aligned 16-bit
words.

## How the windows plug in

`DebugSession` (`debug_session.h`) is the registry.  Each tool file contributes one `DebugProvider` --
`registerTools` (runs once: puts its entry in the Tools menu in place of the greyed placeholder and hooks the
forced-close path) and `draw` (runs every UI frame, also during a halt; the window checks its own open state).
`addDebugTools(App&)` in `debug_ui.cpp` is the single place that lists them, so adding a tool is one `addProvider`
call.  Nothing else in the app calls a tool's draw function.

Every window follows the same rules:

- **Greyed with a reason** when `DebugSession::enabled()` is false: the core did not offer the interface, no game is
  loaded, or RetroAchievements hardcore is on.  The Tools menu shows the reason on its first line.
- **Live refresh at 10 Hz** while the game runs, and **once per request** while it is paused or halted.  Each window
  keeps the `refreshSerial()` it last drew and re-reads when it changes; the serial also bumps after a state load, a
  reset, a content change, a halt and every poke, so a paused game costs nothing and a loaded state shows at once.
- **Opaque, resizable windows** placed through `ui::nextWindow`, text that wraps rather than clips.
- Core calls follow the **Threading** rules below.

Developers can open a window at start-up with `--show-window memory|palette|scanner|objects|blitter|textures|scope`
(with `--screenshot PNG` for a picture); it works with the emulation thread on too.

### Threading

Only the thread that runs `retro_run()` calls the core, and no window ever does.  Inline that is the UI thread and the
rules below cost nothing; with the emulation thread on they are what keeps the windows off the core:

- **Periodic readers** (Memory, Palette, the CPU window's registers and disassembly, the Object list, the Blitter log,
  the scanner's live column) set a `DebugViews` request -- a memory range, a register set, a disassembly window -- and
  read the buffer the emulation thread filled after the last frame.  A changed request, or a content switch (an epoch
  that bumps when the core hands over a new interface), invalidates its buffer, so a window never draws the previous
  game's bytes.  A paused game still fills: the UI's `wakeViews()` posts one fill to the emulation thread.
- **One-shot actions** (poke, register edit, arm the OP snapshot, enable the blit log, texture pack reload, attach, the
  scanner's 2 MB scan pass) go through `EmuThread::debugCall` and wait at most one frame.  Slower work whose result
  the window picks up later (the CPU window's backwards disassembly walk) is posted (`CoreJob`) instead.
- Break, Step, breakpoints and the GDB stub work with the thread on too: the halt pump runs on the emulation thread
  (`DebugCore`), the UI mirrors the halt once per frame, and every UI call into the core gives up while the core is halted.

## The tools

### Memory viewer and Palette (CLUT)

Memory viewer: a hex / ASCII grid over every bus region the core lists (DRAM, cartridge, BIOS, TOM and JERRY
registers, CLUT, line buffers, GPU and DSP RAM).  Values are big-endian bus order, shown as 8-, 16- or 32-bit cells.  It has
go-to, back, "Follow pointer", an incremental hex / text search, change highlighting between refreshes, an in-place
editor (click a cell, double-click or Enter, type hex) and **Add as cheat** for the selected cell.

The **Palette** window shows the 256 CLUT entries as swatches (CRY or RGB16, your choice, since indexed objects can be
either depending on the video mode), the raw word, and an editor with a colour picker.  The CLUT is mirrored in two tables
(`$F00400` and `$F00600`); an edit writes both.

**Write policy** (enforced by the core's `poke`, so no window can bypass it):

| address | rule |
| --- | --- |
| DRAM, GPU RAM, DSP RAM, line buffers, CLUT | writable |
| cartridge, BIOS | never (read-only) |
| `$DFFF00-$DFFFFF` (BUTCH, the CD registers) | **always blocked**, reads are fine |
| other hardware registers (TOM, GPU / blitter, JERRY) | only with **Allow register writes** ticked; written as aligned 16-bit words |
| `B_CMD` `$F02238`, `G_CTRL` `$F02114`, `D_CTRL` `$F1A114`, `OLP` `$F00020`, `LTXD` `$F1A148`, `RTXD` `$F1A14C` | **always blocked from the app's windows**: writing them starts a blit, starts or stops a processor, moves the object list or pushes serial data.  The core would accept them behind a second `VJ_DBG_POKE_DANGEROUS` flag; no window sets it |

A write is all or nothing: if any byte of the range is refused, no byte is written, and the status line says why.  The
Palette editor also needs the register switch (the same checkbox), to keep a stray click from recolouring the screen.
Reads use the side-effect-free byte path (`JaguarReadByte(..., DEBUGGER)`), so looking never acknowledges a CD or DSP interrupt.

### Cheat scanner

Finds the address of a value that changes while you play.  It snapshots the 2 MB of main RAM, searches 8, 16 or 32-bit
big-endian values (signed or unsigned, aligned or not), and narrows with *Unknown initial value* (first scan only), *Equal*, *Not equal*, *Greater*, *Less*,
*In range*, *Changed*, *Unchanged*, *Increased*, *Decreased*, *Increased by* and *Decreased by*.  Undo steps back one narrowing.  Double-click a
candidate's value to poke it, or **Add as cheat**: the result goes into the Cheats window through the same
`CheatManager` as codes typed by hand, so it can be switched on and off, and hardcore blocks it exactly like any other cheat.
Only the first 1000 candidates are listed; the rest are kept and shown once the search is narrower.

### Object list

Decodes the display list the Object Processor walked.  The core arms a one-shot capture and, at the top of the next field
(before the OP rewrites `HEIGHT`, `DATA` and `REMAINDER` while it draws), reads the **whole** list from DRAM: links followed,
both arms of every branch object, each object once, up to 1024.  The window shows every object (bitmap, scaled bitmap, GPU,
branch, stop) with its fields decoded per the JTRM, renders bitmap objects (1, 2, 4, 8, 16 and 24 bpp; CRY, RGB16 or
mixed; the CLUT for indexed ones; TRANS and REFLECT honoured) into a zoomable preview, outlines the selected object over
the game picture, exports a bitmap to PNG, and has a pixel painter that writes RAM only (never registers, whatever the
Memory window's switch says).  **Live** re-arms at the refresh cadence; **Arm snapshot** takes one now (a paused game needs a
frame to complete it, so the window offers *Step a frame*).

Fallback: if the core returns no snapshot (an older core, or no list yet), the window walks the list from `OLP` through DRAM
instead and dims those rows: they are read after the frame, so `HEIGHT` / `DATA` may already be advanced.

### Blitter log

The last 256 blitter launches the core logged (command, A1 / A2 base, flags, pixel and step, `B_COUNT`, which engine ran it,
the frame), newest first, with a decode of the selected launch and the live blitter registers.  The core only logs while the
window is open.  *Freeze* stops the list updating; launches that scroll out of the ring between refreshes are counted as missed.

### Textures

A browser for the core's texture dump (`virtualjaguar_texdump`, `docs/texture-dump.md`).  It turns dumping on, shows the
tiles the game has copied (thumbnail, size, bpp, first frame, palette sightings), exports a tile's PNG, and **replaces** its art:
the PNG you choose is copied into `<system>/vj_texpacks/<CRC32>/<hash16>.png`, the replacement option is enabled, and the core
re-reads the packs (`texreplace_reload`).  Dumps go to `<system>/vj_texdump/<CRC32>/`.

Scope and limits: only what the **blitter copies as 16 bpp source windows** is captured and replaceable (sprites, tiles,
UI).  Anything the GPU computes (Doom's texture mapper, most 3D) is out of scope, as it is for the dump itself.  Replacement
art must be an 8-bit PNG of exactly the tile's size (or exactly twice it with Internal Resolution at 2x).  The list is read from the manifest about once a second (and after a state load or
reset), not on every frame.

### Audio scope

Left and right waveforms and peak meters of the audio block the core produces, tapped in the app before volume and mute.
App only: nothing is recorded and the core is not involved; while the window is closed the tap is one flag test per audio block.

## Break, continue, step (F9, F10)

**F9** breaks into the game at the next instruction of the 68000 (or continues when already halted); **F10** steps one
instruction while halted (it does nothing otherwise).  The Tools menu and a "HALTED" bar over the picture show the same
actions plus **Detach debugger**, which clears every breakpoint and resumes (with the GDB stub enabled the silent host is back one frame
later, see below).  While halted the tool windows keep drawing and
refreshing, so you can read memory, palette and registers of the frozen machine.  Breakpoints are per game (they are dropped when
a different game loads).

## Hardcore (RetroAchievements)

Hardcore turns the whole Tools menu off: the entries grey out with "RetroAchievements hardcore is on", every open window
closes, the session detaches and any halt resumes.  Turning hardcore on from inside a halt does the same.  The scanner's
cheats are ordinary cheats, so the Cheats window's own hardcore block applies to them too.

Hardcore also locks the GDB stub (`virtualjaguar_gdb_stub`) to `disabled`: the lock is applied at the next load, because the
core reads the stub options only then.  **Turning hardcore on while the stub is enabled reloads the game** (a restart is
what hardcore needs anyway, and the reload is the only way the lock reaches the core and the socket closes); the log says
`[ach] hardcore on: reloading the game to close the GDB stub`.  If hardcore turns off mid-game the stub stays disabled until
the next load.

## Several app instances

Each process has its own session and windows.  The system folder (and with it `vj_texdump` / `vj_texpacks`) is shared between
copies, so the Textures window **disables dumping in extra copies** (instance 2 and up) to stop two processes writing one
manifest; browsing and replacing still work there.  Memory, scanner, objects, blitter and scope are per process and need no care.

## With the external GDB stub

The built-in debugger and the GDB TCP stub (`virtualjaguar_gdb`) share one set of halt machinery and one breakpoint table.
`attach()` initialises the tables **without a socket**, so the app works with the option off; with it on, both can be used:
either side may resume a halted machine, and a GDB client disconnecting clears the table (the app re-applies its list on the next
attach).  With no app host and no GDB client attached, a halt returns at once and can never hang the core.

Whenever the stub is enabled the app keeps a **silent host** attached (logged as `[debug] gdb host attached (silent)`): it
never halts anything by itself, it only makes a GDB halt visible -- the halt bar says "HALTED by gdb", F9 continues, calls
into the core give up instead of hanging, and quitting detaches.  It is attached on the emulation thread inside the load,
before the first frame.  Two consequences:

- **`virtualjaguar_gdb_wait`** parks the boot even before a GDB client connects; the UI stays
  live, the halt bar shows it, and F9 (or **Detach**) continues.  A client that connects later asks `?` and gets `S05`.
- **Detach debugger** drops every app breakpoint and the host, and with the stub still enabled the silent host comes back
  one frame later (it re-attaches on the emulation thread before the next frame, so a gdb breakpoint is never missed).  Detach
  does not close the stub; turn the option off and reload for that.  Breakpoints you want after a Detach are set again.
- **Quitting** detaches (which clears gdb's breakpoints too) and closes the socket.  A **headless** run that gdb holds halted
  waits for the gdb client or a quit signal: SIGINT or SIGTERM ends it cleanly (exit 0, the socket closed); a second
  signal kills it at once, in case the core is wedged.

## Cost when you are not looking

Detached, nothing on a per-instruction or per-pixel path looks at the interface: attaching stores a pointer, and the
instruction hooks stay the shipped GDB `gdbArmed*` gates.  The only new always-present work is one global test per
`OPProcessList` call and one per blitter launch, both a plain load and branch.  All tool state is host-transient: no savestate
field, nothing the emulated machine can observe (the core test hashes the frame and all of main RAM with every tool on and off).

## Testing

`debug_session_unit` (logic against the mock; label `emu_thread`: the verdict serial, `frameEnded`, detach and quit mid-halt, the lane
order of Detach and Attach) and `debug_e2e` (real core, inline); `debug_views_unit` (the request table, the buffers and
the epoch, under ThreadSanitizer too) and `debug_views_threaded` (the real core on the emulation thread: the views equal
a `debugCall` peek, a paused request fills with no frame run, the first read after a reload is the new ROM, inline and
threaded give the same Dram view).

The halt on the emulation thread: `emu_thread_unit` (`testCallGivesUp`, `testAdvanceGivesUp`, `testServeDebugLane`),
`emu_commands_unit` (`testDebugLaneWait`) and `debug_e2e_threaded` (the real core on a threaded `EmuThread`, wired as `App`
wires it: break, step, continue, detach, hardcore and quit mid-halt, every UI operation within 50 ms while the core is parked, and
the 3 s join).  The GDB stub: `gdb_threaded_halt` (a gdb client finds the headless app halted at boot, reads memory and
continues; SIGTERM mid-halt exits cleanly; `gdb_inline_halt(_hardcore)` run the same inline), `debug_on_threaded`,
`gdb_enabled(_threaded)` (the silent host attaches at load), `gdb_hardcore_locked_*` and `gdb_hardcore_flip_*` (hardcore locks the
stub; a mid-game flip reloads).

Per tool, a unit test over the pure logic
(`debug_memory_unit`, `debug_scan_unit`, `debug_op_unit`, `debug_texture_unit`, `debug_blitter_unit`), a headless ImGui smoke test
of each window over the mock (`debug_memory_ui_smoke`, `debug_scan_ui_smoke`) and end-to-end runs on the public `yarc.j64`
(`debug_scan_e2e`, `debug_op_e2e`: at least one bitmap object comes from the core's snapshot itself); and the core's
`test/test_debug_iface.c`.
