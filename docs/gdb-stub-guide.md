# GDB remote debugging — user guide

Issue [#652](https://github.com/libretro/virtualjaguar-libretro/issues/652). Design rationale,
architecture, and the invariants this feature must never violate live in
[`gdb-stub-design.md`](gdb-stub-design.md) — read that first if you are changing this feature,
not just using it. This page is for the developer on the other end of the socket.

## What this is, in one paragraph

The core can open a TCP socket that speaks GDB's Remote Serial Protocol (RSP), so a real debugger
— `gdb`, `lldb`, VS Code, or any RSP-speaking tool — can inspect and control the emulated Jaguar:
set breakpoints, read and write registers and memory, single-step, and watch memory addresses.
All three processors are exposed: the 68000 as a native GDB target, and the GPU and DSP RISC
cores as two additional GDB "threads" with a custom register description. It is off by default,
developer-facing, and never reachable from another machine.

## Quick start

1. In core options, set **GDB Debug Stub** to `enabled` (category: Diagnostics) and restart
   content — this option is read once at load, like the other developer-facing "Restart" options
   in this core.
2. Optionally set **GDB Stub Port** if the default (2345) collides with something else on your
   machine.
3. Load your content. You should see an on-screen message: `GDB stub listening on
   127.0.0.1:2345 -- halts will freeze this frontend`.
4. Point your debugger at `127.0.0.1:<port>`. With `gdb`:

   ```
   (gdb) target remote 127.0.0.1:2345
   ```

   You'll see a second on-screen banner (`GDB client attached...`) the moment the socket accepts
   your connection.
5. Set a breakpoint and continue, same as debugging any other target:

   ```
   (gdb) break *0x802000
   (gdb) continue
   ```

When the breakpoint hits, the emulator freezes exactly at that instruction — see "Halts freeze
the frontend" below before you rely on this in a session you care about.

### Faster: `tools/gdb/`

Steps 4 onward above are exactly what [`tools/gdb/connect.sh`](../tools/gdb/connect.sh)
(`connect.cmd` on Windows) automates: it finds a `gdb` on your machine (with a clear error if it
can't — see "No shipped `m68k-elf-gdb`" below), and launches it pre-loaded with
[`tools/gdb/jaguar.gdbinit`](../tools/gdb/jaguar.gdbinit) (sane `set` defaults plus `jconnect`,
`jdisasm`, `jregs`, `jhalt`, `jtrace`, `jwatch` aliases over the `monitor` commands below) and
[`tools/gdb/jaguar_gdb.py`](../tools/gdb/jaguar_gdb.py) (richer `jag-disasm`, `jag-regs`,
`jag-trace`, `jag-halt`, `jag-watch`, `jag-info-registers` commands with argument checking and a
pretty-printed, FLAGS-decoded register dump for the GPU/DSP threads). Run
`tools/gdb/install.sh` once to add the same commands to your personal `~/.gdbinit` so they exist
in every session, not just ones launched through `connect.sh`.

LLDB works too — it speaks RSP as well, so `gdb-remote 127.0.0.1:2345` is LLDB's equivalent of
`target remote`. [`tools/gdb/jaguar_lldb.py`](../tools/gdb/jaguar_lldb.py) is the same
convenience layer reimplemented against LLDB's Python API; `tools/gdb/install.sh` installs it
into `~/.lldbinit` too.

## The three targets (GDB threads)

| GDB thread | Processor | What GDB knows about it |
|---|---|---|
| 1 | 68000 (main CPU) | Native `m68k` architecture — GDB already understands it. Full register names, disassembly, and backtraces work exactly as they would for any other m68k target. |
| 2 | GPU RISC (TOM) | A custom 34-register description (`R0`-`R31`, `PC`, `FLAGS`) served over `qXfer:features:read`. GDB can display and set these registers by name and place breakpoints on them. It **cannot** disassemble this processor — see below. |
| 3 | DSP RISC (JERRY) | Same custom description as the GPU (same underlying RISC core, different opcode table from R32 up — see `monitor disasm`). Same disassembly limitation. |

Switch threads in `gdb` with `thread 2` / `thread 3`, or address a specific one with `Hg`/`Hc` at
the protocol level if you're driving the socket by hand.

Register numbering for threads 2/3 is fixed permanently (clients cache it across a session):
index 0-31 are `R0`-`R31`, 32 is `PC`, 33 is `FLAGS`.

**Multi-architecture caveat:** mixing one native-architecture thread (68K) with two
custom-description threads (GPU/DSP) in a single GDB session is not a mainstream use of RSP, and
this repo has no way to test it against a real `gdb` binary (see "No shipped `m68k-elf-gdb`"
below). The protocol-level pieces (register numbering, XML shape, thread enumeration) are
implemented to the RSP specification and covered by this repo's own scripted test client, but
real GDB's behavior when a target description changes mid-session between threads has not been
verified end to end. If you find a `gdb` build that handles this cleanly (or doesn't), the
project would like to know.

## Breakpoints and watchpoints

- `break`/`hbreak` (GDB's `Z0`/`Z1`) set an execution breakpoint on any of the three processors.
  Software and hardware breakpoint requests are treated identically: **this stub never patches
  emulated memory** to implement a breakpoint (see the design doc for why — the short version is
  that patched instruction bytes would leak into savestates). Up to 64 breakpoints per processor.
- `watch`/`rwatch`/`awatch` (GDB's `Z2`/`Z3`/`Z4`) set a memory watchpoint on the 68000's bus.
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
monitor trace                             dump the 68K PC traceback ring (issue #542)
monitor watch [<addr> [r|w]]              set/clear the memory watchpoint (same slot as Z2-Z4)
```

`monitor trace` is worth knowing about even outside a GDB session: the traceback ring it dumps is
already maintained unconditionally by the core (it backs the crash-detection watchdog), so this
command is free to use after any wild jump or crash to see where the 68K had been.

## The RISCs have no GDB disassembly

GDB ships disassemblers for real-world architectures. The Jaguar's GPU and DSP are a bespoke
RISC ISA with no GDB (or LLVM, or binutils) backend, and none is coming. When you `disassemble` a
function on thread 2 or 3, GDB will fail or print garbage — this is not a bug in the stub, it is
GDB correctly telling you it doesn't know this instruction set.

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
disassembly (on the 68K thread — see above for the RISC threads) shows raw instructions annotated
with whatever symbols it can resolve from the binary. That is a long way from a source debugger,
but it is a long way ahead of "reason about a hex dump," which was the alternative before this
feature existed.

## No shipped `m68k-elf-gdb`

This project's toolchain (`tools/jaguar-toolchain`, see the fetch script referenced from the
design doc) pins exactly five tools — `rmac`, `rln`, `lyxass`, `pc_jagcrypt`, `new_bjl` — and
none of them is a debugger. **You bring your own `m68k-elf-gdb`.** A stock GNU binutils/gdb build
targeting `m68k-elf` (the kind distributed for classic Mac/Amiga/Atari ST bare-metal development)
works against a plain `m68k` architecture with no custom target description needed for thread 1.
This repository's own test coverage for the wire protocol uses a small scripted Python RSP client
(`test/tools/gdb_attach_probe.py`, `test/tools/gdb_breakpoint_probe.py`,
`test/tools/gdb_reconnect_probe.py`) rather than a real `gdb`
binary, for exactly this reason — there wasn't one to test against. If something in this guide
doesn't match what your particular `gdb` build does, the scripted client is the ground truth this
implementation was actually verified against; please file an issue with what you saw.

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

- One memory watchpoint at a time (see "Breakpoints and watchpoints" above).
- No debugging across a savestate load: breakpoints stay armed at addresses that may no longer
  mean anything once new content or a different program state loads.
- No support for run-ahead: a second core instance under run-ahead will simply fail to bind the
  port, and that instance's stub stays disabled — debugging with run-ahead active is not
  supported.
- Non-stop mode is not implemented. All three processors halt together; you cannot halt the 68K
  while leaving the GPU running.
- Tracepoints (`QTDP` and friends) and GDB's file-I/O extension (`vFile`) are not implemented, and
  the latter never will be — see the design doc's "Out of scope".
- **A packet the stub doesn't implement gets no reply at all, not the empty `$#00` RSP defines
  for "unsupported."** Verified with a real LLDB `gdb-remote` attach (proxied and logged
  byte-for-byte while building `tools/gdb/`): LLDB's handshake sent `QThreadSuffixSupported`,
  which this stub doesn't implement, got silence instead of `$#00`, and hung indefinitely rather
  than treating it as "not supported" and moving on. This is plausibly why "no way to test
  against a real gdb binary" turned out true beyond mere toolchain availability — real `gdb`
  handshakes send other packets this stub doesn't recognize too (`qTStatus`, `qSymbol::`,
  `vMustReplyEmpty`, whose entire purpose is checking exactly this). Tracked for a fix; until
  then, a hang immediately after `target remote`/`gdb-remote` with no stop-reply is a known
  symptom, not a sign your setup is wrong.
