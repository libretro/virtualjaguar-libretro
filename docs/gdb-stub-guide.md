# GDB remote debugging — user guide

Issue [#652](https://github.com/libretro/virtualjaguar-libretro/issues/652). Design rationale,
architecture, and the invariants this feature must never violate live in
[`gdb-stub-design.md`](gdb-stub-design.md) — read that first if you are changing this feature,
not just using it. This page is for the developer on the other end of the socket.

## What this is, in one paragraph

The core can open a TCP socket that speaks GDB's Remote Serial Protocol (RSP), so a real debugger
can inspect and control the emulated Jaguar: set breakpoints, read and write registers and memory,
single-step, and watch a memory address. **Use a `gdb` built with m68k support** (see "Which
debugger" below — `brew install gdb` has it; LLDB cannot debug the 68K). The 68000 is the GDB
thread; the GPU and DSP RISC cores are reached through `monitor` commands. It is off by default,
developer-facing, and bound to loopback unless you opt in to the LAN.

## Quick start

1. In core options, set **GDB Debug Stub** to `enabled` (category: Diagnostics) and restart
   content — this option is read once at load, like the other developer-facing "Restart" options
   in this core.
2. Optionally set **GDB Stub Port** if the default (2345) collides with something else on your
   machine.
3. Load your content. You should see an on-screen message: `GDB stub listening on
   127.0.0.1:2345 -- halts will freeze this frontend`.
4. Point your debugger at `127.0.0.1:<port>`. With `gdb` (Homebrew's `brew install gdb` works as
   is):

   ```
   (gdb) set endian big
   (gdb) target remote 127.0.0.1:2345
   ```

   Nothing else is needed: the stub serves an m68k target description, so gdb selects the m68k
   architecture and the `d0-d7`/`a0-a7`/`ps`/`pc` register set by itself (no `set architecture`,
   no `set remote target-features-packet off`). **`set endian big` is still required**: a target
   description cannot declare byte order and a bare gdb assumes the host's, so without it every
   register and memory word is byte-swapped (`pc` shows `0x208000` instead of `0x802000`).
   Loading an m68k ELF/COFF with `file` also fixes it, since the binary carries its byte order.
   `tools/gdb/connect.sh` / `jconnect` do this for you.

   You'll see a second on-screen banner (`GDB client attached...`) the moment the socket accepts
   your connection.
5. Set a breakpoint and continue, same as debugging any other target:

   ```
   (gdb) break *0x802000
   (gdb) continue
   ```

When the breakpoint hits, the emulator freezes exactly at that instruction — see "Halts freeze
the frontend" below before you rely on this in a session you care about.

End the session with `detach` (or `disconnect`, or just quitting gdb). All three leave the emulator
running and the stub listening, so you can reconnect with another `target remote`. A halted machine
resumes and every breakpoint/watchpoint is disarmed.

A complete, verified session (yarc.j64 with **GDB Stub: Halt At Boot** on, stock gdb 18.1; output
trimmed to the interesting lines):

```
(gdb) set endian big
(gdb) target remote 127.0.0.1:2345
0x00802000 in ?? ()
(gdb) info registers pc
pc             0x802000            0x802000
(gdb) x/4i $pc
=> 0x802000:	movew #9984,%sr
   0x802004:	lea 0x1000,%sp
   0x802008:	lea 0xf00000,%a0
   0x80200e:	movel #458759,%d0
(gdb) break *0x802008
(gdb) continue
Breakpoint 1, 0x00802008 in ?? ()
(gdb) stepi
0x0080200e in ?? ()
(gdb) watch *(int*)0xf0210c
Hardware watchpoint 2: *(int*)0xf0210c
(gdb) continue
Program received signal SIGTRAP, Trace/breakpoint trap.
0x00802018 in ?? ()
(gdb) continue
Program received signal SIGTRAP, Trace/breakpoint trap.
0x00802018 in ?? ()
(gdb) monitor regs gpu
GPU PC=$F03000 FLAGS=$00000000 CTRL=$00002800
...
(gdb) detach
[Inferior 1 (Remote target) detached]
```

Note the watchpoint stops **twice** at the same `pc`: the 68K performs this 32-bit store as two
16-bit bus writes and both match the watched address, so the second `continue` is needed to get
past the same instruction. (Accesses inside main RAM are single bus accesses and stop once.)

### Faster: `tools/gdb/`

Steps 4 onward above are exactly what [`tools/gdb/connect.sh`](../tools/gdb/connect.sh)
(`connect.cmd` on Windows) automates: it finds a `gdb` on your machine (with a clear error if it
can't — see "Which debugger" below), and launches it pre-loaded with
[`tools/gdb/jaguar.gdbinit`](../tools/gdb/jaguar.gdbinit) (sane `set` defaults plus `jconnect`,
which also runs `set endian big`,
`jdisasm`, `jregs`, `jhalt`, `jtrace`, `jwatch` aliases over the `monitor` commands below) and
[`tools/gdb/jaguar_gdb.py`](../tools/gdb/jaguar_gdb.py) (richer `jag-disasm`, `jag-regs`,
`jag-trace`, `jag-halt`, `jag-watch`, `jag-info-registers` commands with argument checking and a
pretty-printed, FLAGS-decoded register dump). Run
`tools/gdb/install.sh` once to add the same commands to your personal `~/.gdbinit` so they exist
in every session, not just ones launched through `connect.sh`.

LLDB can attach (`gdb-remote 127.0.0.1:2345`) but **cannot debug the 68K**: it has no m68k target
triple, so there are no registers, no disassembly and no stepping UI, only `memory read` and the
`jag-*` monitor helpers. [`tools/gdb/jaguar_lldb.py`](../tools/gdb/jaguar_lldb.py) is the
convenience layer for that; `tools/gdb/install.sh` installs it into `~/.lldbinit` too. Use gdb.

## The three processors: one GDB thread, two via `monitor`

| Processor | How you reach it from gdb |
|---|---|
| 68000 (main CPU) | **GDB thread 1.** An m68k target description (`org.gnu.gdb.m68k.core`: `d0-d7`, `a0-a5`, `fp`, `sp`, `ps`, `pc`) is served over `qXfer:features:read`, so registers, `x/i`, `stepi`, breakpoints and watchpoints behave like any m68k target. |
| GPU RISC (TOM) | `monitor regs gpu`, `monitor disasm gpu <addr> [n]`, `monitor halt gpu`; `break` on it is protocol-level only (`Hg2` + `Z0`, for raw RSP clients). |
| DSP RISC (JERRY) | Same, with `dsp`. |

Why only one thread: gdb reads **one** target description per connection and applies it to every
thread it knows about. The GPU/DSP have a different register layout (34 registers, not 18), so
advertising them as threads 2/3 made `info threads` and `thread 2` fail with `Remote 'g' packet
reply is too long (expected 72 bytes, got 136 bytes)`. `qfThreadInfo` therefore reports only
thread 1. If the GPU or DSP halts (a breakpoint set from the app's debugger, or `monitor halt gpu`)
gdb is told the 68K stopped (thread 1, which is genuinely frozen with the rest of the machine) and
a console line `GPU halted at $F03010 -- see monitor regs gpu` is printed; inspect the RISC with
`monitor regs gpu|dsp` and `monitor disasm`, then `continue`.

A raw RSP client can still select threads 2/3 with `Hg`/`Hc` and read their 34-register
`org.atari.jaguar.gpu|dsp` description (R0-R31 = 0-31, PC = 32, FLAGS = 33, fixed permanently);
stock gdb simply isn't offered them.

## Breakpoints and watchpoints

- `break`/`hbreak` (GDB's `Z0`/`Z1`) set an execution breakpoint on any of the three processors.
  Software and hardware breakpoint requests are treated identically: **this stub never patches
  emulated memory** to implement a breakpoint (see the design doc for why — the short version is
  that patched instruction bytes would leak into savestates). Up to 64 breakpoints per processor.
- `watch`/`rwatch`/`awatch` (GDB's `Z2`/`Z3`/`Z4`) set a memory watchpoint on the 68000's bus
  (verified with stock gdb: `watch *(int*)0xf0210c` stops at the access). The match is on the exact
  access address, the stop is reported as `Program received signal SIGTRAP` (the stub's stop reply
  carries no `watch:` field), and it is taken as the 68K issues the access, before the store lands.
  A 32-bit access outside main RAM is two 16-bit bus accesses and stops twice at the same `pc`
  (`continue` once more). The stub does not de-duplicate: guessing "same instruction" could hide a
  real hit, and the dedupe would live in the memory hot path.
  **Only one memory watchpoint can be armed at a time** — this rides a single-slot mechanism
  that predates the GDB stub (an old, previously-unused breakpoint-on-memory-access hook in the
  core). Setting a second watchpoint while one is already armed at a different address is
  refused. Watchpoints are 68K-bus-scoped only; they are not available on the GPU/DSP threads.
- Stepping (`stepi`/`step`, or `s` at the protocol level) works on all three processors. The GPU
  and DSP do **not** use the real hardware single-step bit (`G_CTRL`/`D_CTRL` bit 3) for this —
  that bit is guest-visible state a game can read, and toggling it from the debugger would be an
  observable perturbation of the emulated machine. Stepping is implemented as a one-shot internal
  breakpoint instead.

## `monitor` commands

GDB forwards `monitor <text>` to the stub verbatim. Available commands:

```
monitor disasm <gpu|dsp> <addr> [count]   RISC disassembly (see below)
monitor regs [gpu|dsp]                    full register + control-register dump
monitor halt <68k|gpu|dsp>                halt one processor without a breakpoint
monitor trace [N]                         newest N (default 64, max 1024) 68K PCs from the traceback ring (issue #542), oldest first
monitor watch [<addr> [r|w]]              set/clear the memory watchpoint (same slot as Z2-Z4)
```

`monitor trace` is worth knowing about even outside a GDB session: the traceback ring it dumps is
already maintained unconditionally by the core (it backs the crash-detection watchdog), so this
command is free to use after any wild jump or crash to see where the 68K had been. The last line
is the most recent PC.

Output of any length arrives in full: the stub streams it as RSP `O` console packets (1 KB of text
each) followed by `OK`, the standard `qRcmd` shape, so gdb prints all of `monitor regs gpu` and
`monitor trace 1024`. (Before issue #936 replies were cut at 512 bytes, which lost the end of the
register dump and, for `trace`, every recent entry.)

## The RISCs have no GDB disassembly

GDB ships disassemblers for real-world architectures. The Jaguar's GPU and DSP are a bespoke
RISC ISA with no GDB (or LLVM, or binutils) backend, and none is coming. GDB cannot
disassemble the GPU/DSP at all — this is not a bug in the stub, it is GDB correctly telling you it
doesn't know this instruction set.

`monitor disasm <gpu|dsp> <addr> [count]` fills that gap using this repo's own RISC
disassembler (the same one `test/tools/gpu_disasm_dump` uses offline — there is exactly one
implementation of this disassembler in the tree, not two that could drift apart). Output is
plain text, one instruction per line, e.g.:

```
(gdb) monitor disasm gpu f03410 8
$F03410: 8442  move            r2, r18
$F03412: E840  jr               T, +0 -> $F03414
...
```

## Halts freeze the frontend

This is the single most important thing to understand before attaching to something you care
about: **when a breakpoint or watchpoint hits, the entire emulator freezes** — video, audio, and
input all stop, because the halt is implemented by blocking the same thread `retro_run()` runs
on. This is a deliberate design choice (see the design doc's "Architecture" section): freezing
in place, with every processor's state coherent at the exact point of the fault, is what makes
inspecting an emulator with a real debugger useful in the first place. The alternative — winding
back through savestates to approximate where a bug happened — is the status quo this feature
replaces.

Consequences:

- **The frontend UI will appear hung** for as long as you're halted. This is expected, not a
  crash. Resume with `continue`/`c`, `stepi`/`s`, or disconnect your debugger.
- **Disconnecting your debugger while halted resumes the machine immediately** and disarms every
  breakpoint and watchpoint. A dead or closed debugger connection can never leave the machine
  wedged.
- If you forget you're attached and walk away, two core options help:
  - **GDB Stub: Halt Timeout** — after N seconds halted with no client activity, the core
    auto-resumes and logs it loudly. Off by default: silently resuming a machine you're
    debugging is a worse surprise than a frozen frontend, for the audience this feature is built
    for. Note that auto-resuming does **not** disarm the breakpoint that triggered the halt — if
    execution reaches the same address again, expect the same halt (and the same log line) to
    repeat.
  - **GDB Stub: Halt At Boot** — halts before the 68000's very first instruction, so you can
    attach before anything has run. Without this, a boot-time fault may already be long past by
    the time you connect.

## Symbol-level, not source-level, debugging

Be precise about what this feature gives you. `rln`, the Jaguar toolchain's linker (see the
`tools/jaguar-toolchain` fetch in this repo), emits classic `mc68k COFF` object files — a
pre-ELF, Atari-era format, not ELF/DWARF. There is no line-number or source-file information
anywhere in that chain, and `rln`'s own `-g` ("output source-level debugging") flag was verified
during this feature's design to produce byte-identical output to a build without it. **This
repository cannot offer source-level debugging for Jaguar homebrew, and no future version of this
document should claim otherwise without new evidence that some other tool in the chain emits real
DWARF.**

What you do get: `rln -m`'s load map resolves your program's global symbol names to addresses,
so you can set breakpoints and read memory by the symbol names your own code defines, and GDB's
disassembly (on the 68K — see above for the RISC cores) shows raw instructions annotated
with whatever symbols it can resolve from the binary. That is a long way from a source debugger,
but it is a long way ahead of "reason about a hex dump," which was the alternative before this
feature existed.

## Which debugger

This project ships no debugger (`tools/jaguar-toolchain` pins `rmac`, `rln`, `lyxass`,
`pc_jagcrypt`, `new_bjl`). You bring one, and it has to be **built with m68k support**. Verified
against real debuggers (issue #936, 2026-10-14):

| Debugger | Works? | Notes |
|---|---|---|
| **gdb 18.1, Homebrew** (`brew install gdb`, macOS arm64) | **Yes** | Built `--enable-targets=all`, so it has m68k; no `m68k-elf-gdb` formula is needed (none exists). Registers, memory, `x/i`, breakpoints, `stepi`, watchpoints, `monitor`, `detach` all verified, against the headless harness and the desktop app (`--headless`). Needs `set endian big` (see Quick start). The "codesign gdb" notice from Homebrew applies only to debugging native processes, not remote targets. |
| gdb, Debian/Ubuntu | Only `gdb-multiarch` | Plain `gdb` is not built with m68k. Check: `gdb -nx -batch -ex 'set architecture m68k'` must not print `Undefined item`. |
| a gdb built for `m68k-elf` | Yes | Sets the architecture natively. |
| **LLDB** (Apple, swiftly) | **Memory only** | Re-checked against the stub that serves the m68k description: it still attaches without hanging, `memory read` works, `thread list` shows the one 68K thread, and `process detach` leaves the emulator running. But LLDB has no m68k triple (`invalid triple 'm68k'`): `register read` fails ("Command requires a process, which is currently stopped"), no disassembly, no stepping UI. Raw packets work via `process plugin packet send` (`Z0`, `vCont`; `qRcmd` text comes back as `O` console packets). |
| a scripted RSP client | Yes | `test/tools/gdb_*_probe.py`, the ground truth for the wire protocol. |

If you are on a **core older than issue #936** (v3.9.0 and earlier), the 68K had no target
description and `target remote` aborted with `Protocol error: qXfer:features:read (target-features)
conflicting enabled responses`. Work around it with `set architecture m68k` and
`set remote target-features-packet off` before `target remote`, and use `disconnect` instead of
`detach` (`D` was not implemented).

## Security notes (short version — see the design doc for the full reasoning)

- **The listener binds `127.0.0.1` by default.** On desktop, if you only need it from another
  machine occasionally, forwarding the port yourself is still the safest route
  (`ssh -L 2345:127.0.0.1:2345 <host>`) — it needs no core option and no open port.
- **`GDB Stub: Network Binding` (`virtualjaguar_gdb_bind`) can widen that to `lan`.** This exists
  for the case SSH cannot cover: debugging a game running on a phone, tablet or TV, where there is
  no shell on the device to forward from. Understand what you are turning on:
  - **The GDB remote protocol has no authentication of any kind.** While the stub is open on the
    LAN, anyone who can reach the port can read and write the emulated machine's memory and control
    its execution. There is no password, no token, and no handshake to add one to.
  - Connections from **public (non-private) addresses are refused and logged** even in `lan` mode —
    only RFC1918, CGNAT (100.64/10), link-local and loopback peers are accepted. That stops the
    silent accidental case (carrier NAT, a misconfigured hotspot). It does **not** make a hostile
    LAN safe, and it will also refuse a legitimate VPN peer on a public range.
  - It is **latched once at content load**, so changing it mid-session does nothing until you reload.
  - It **fails closed**: absent, unset or unrecognised values resolve to loopback.
  - When `lan` is active the core logs a warning naming the address and port, and shows an on-screen
    banner saying the stub is open to your network. Turn it back to `loopback` when you are done.
- Only one client at a time; a second connection attempt is closed immediately.
- Every memory read and write is bounds-checked against the emulated map — a malformed or
  hostile packet cannot read or write outside emulated space.
- Register and memory writes issued through the debugger genuinely change emulated state (that's
  the point of a debugger). A savestate taken after such a write reflects it. This is intentional
  and not guarded against.

## Known limitations

- One memory watchpoint at a time, matched on the exact access address (see "Breakpoints and
  watchpoints" above).
- `set endian big` is required (a target description cannot declare byte order).
- Attaching never halts a running machine and Ctrl-C (`0x03`) is not implemented: a bare `continue`
  on a running machine just waits for the next breakpoint. Use **GDB Stub: Halt At Boot** to attach
  before anything runs, or `monitor halt 68k` to halt a running machine (the stop reply for that
  asynchronous halt is consumed by gdb as the reply to its NEXT command, which can leave gdb's idea
  of stopped/running one step behind; prefer a breakpoint or Halt At Boot).
- gdb ignores the stub's `vCont?` reply and drives `c`/`s` instead; this is harmless.
- No debugging across a savestate load: breakpoints stay armed at addresses that may no longer
  mean anything once new content or a different program state loads.
- No support for run-ahead: a second core instance under run-ahead will simply fail to bind the
  port, and that instance's stub stays disabled — debugging with run-ahead active is not
  supported.
- Non-stop mode is not implemented. All three processors halt together; you cannot halt the 68K
  while leaving the GPU running.
- Tracepoints (`QTDP` and friends) and GDB's file-I/O extension (`vFile`) are not implemented, and
  the latter never will be — see the design doc's "Out of scope".
- An unimplemented packet gets the RSP-mandated empty `$#00` reply (issue #697), so strict clients
  no longer hang on it.
