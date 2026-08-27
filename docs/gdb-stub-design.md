# GDB remote serial protocol stub — design

Issue [#652](https://github.com/libretro/virtualjaguar-libretro/issues/652).
Approved design, not yet implemented (2026-08-26).

## Purpose

Expose the emulated Jaguar to a real debugger over GDB's Remote Serial Protocol
(RSP), so that homebrew developers can set breakpoints, inspect and modify
registers and memory, and single-step the 68000, GPU and DSP — from `gdb`,
`lldb`, VS Code, or any other RSP client they already use.

This is also this project's settled answer to "should we have a scripting
system". GDB ships a complete Python scripting environment on top of
breakpoints and memory access, which is the entire feature list BigPEmu's
bespoke C-to-bytecode VM sells. Implementing a well-documented wire protocol
gets us that capability permanently, with the scripting engine maintained by
someone else. See #370, which rejected a VM twice on scope grounds, and #637.

## Decisions settled before design

These were decided with the maintainer and are not open for re-litigation
inside the implementation.

1. **Shipped in every build, off by default**, activated by a core option that
   opens a TCP listener on demand — *not* a dev-only facility gated behind
   `TEST_EXPORTS=1` like `vjtrace`. Rationale: the audience is homebrew
   developers, and requiring them to build the core themselves excludes most of
   them. Consequence: the design must prove near-zero cost when disabled.
2. **A breakpoint hit blocks inside `retro_run()`.** The machine freezes exactly
   at the fault with registers, memory and cycle position coherent. The frontend
   UI freezing is accepted as the cost; it is mitigated, not designed away.
3. **All three processors are exposed.** The 68000 as a native GDB target; the
   GPU and DSP as additional GDB threads described by custom XML target
   descriptions, with disassembly served by our own tooling through `monitor`
   commands, because GDB has no backend for the Jaguar RISC ISA and never will.
4. **No emulated memory is ever patched to implement a breakpoint** (see
   "Breakpoint detection" below).

## What already exists, and why this is smaller than it looks

The three hardest-sounding pieces are already in the tree:

| Need | Already present |
|---|---|
| Per-instruction 68K hook | `M68KInstructionHook()`, `src/core/jaguar.c:364` — already called after every instruction in shipped builds for #542's traceback ring. `M68K_HOOK_FUNCTION` is already defined in `m68kinterface.h:101`. |
| 68K halt/resume | `M68KDebugHalt()` / `M68KDebugResume()`, `m68kinterface.c:90` — set and clear `SPCFLAG_DEBUGGER`. Already wired to six memory-breakpoint sites in `jaguar.c`. |
| Memory watchpoints | Exported "breakpoint on memory access" vars, `jaguar.c:356`, checked at six sites in the memory path. |
| RISC single-step | Real hardware: `G_CTRL`/`D_CTRL` bit 3 (`SINGLE_STEP`). `gpu.c` and `dsp.c` already reason about a single-step-parked core's interrupt behaviour. |
| RISC disassembly | `test/tools/gpu_disasm_dump`, `test/tools/disasm_gpu_isr.py`, `test/tools/bios_disasm.py`. |
| Cross-platform TCP | `src/jerry/jlink_tcp.c` — netlink's transport, already built on every target in `Makefile.common`. |

The genuinely new code is the RSP protocol layer and the target glue.

## Architecture

```
                    gdb / lldb / VS Code
                            |
                       TCP 127.0.0.1
                            |
   +------------------------v-------------------------+
   |  src/debug/gdbsock.c   listener, one client,      |
   |                        non-blocking accept        |
   +------------------------+-------------------------+
                            |
   +------------------------v-------------------------+
   |  src/debug/gdbstub.c   RSP framing: $..#cs, ack/  |
   |                        nak, no-ack mode, dispatch |
   +------------------------+-------------------------+
                            |
   +------------------------v-------------------------+
   |  src/debug/gdbtarget.c register file per target,  |
   |                        memory r/w, bp table,      |
   |                        step/continue, halt loop   |
   +----+-----------------+------------------+--------+
        |                 |                  |
   M68KInstructionHook  GPUExec()         DSPExec()
   (jaguar.c:364)       (tom/gpu.c)       (jerry/dsp.c)
```

Three new files under a new `src/debug/` directory, plus a handful of call
sites. Nothing existing is refactored.

### Why a new directory

`src/debug/` rather than `src/core/` because the stub depends on TOM, JERRY,
the 68K and the memory map, i.e. on everything — putting it under any one
subsystem's directory would misrepresent it. It also keeps the protocol layer
unit-testable without linking the emulator (see Testing).

## Target and thread model

GDB threads map to processors:

| GDB thread | Processor | Register file |
|---|---|---|
| 1 | 68000 | D0-D7, A0-A7, PC, SR — native `m68k` description, GDB knows it |
| 2 | GPU RISC (TOM) | R0-R31, PC, FLAGS, plus the memory-mapped control registers |
| 3 | DSP RISC (JERRY) | R0-R31, PC, FLAGS, plus its control registers |

`qfThreadInfo` / `qsThreadInfo` enumerate them; `Hg` / `Hc` select one;
stop replies (`T05thread:N;`) name the thread that halted.

Thread 1 is served with no target description at all, so GDB applies its own
`m68k` architecture: full disassembly, source-level debugging with DWARF from
`rln`, and backtraces come free.

Threads 2 and 3 are served a custom description via `qXfer:features:read`,
defining an `org.atari.jaguar.risc` feature with the 32 general registers and
the flags. GDB will display and modify them by name and set breakpoints on
them. It will **not** disassemble them — no GDB backend for this ISA exists.
That gap is filled by `monitor` commands rather than pretended away.

### `monitor` commands

GDB forwards `monitor <text>` verbatim as `qRcmd`. The stub implements:

```
monitor disasm <addr> [count]   RISC disassembly via our own disassembler
monitor regs [gpu|dsp]          full control-register dump, decoded
monitor halt <gpu|dsp|68k>      halt one processor without a breakpoint
monitor trace                   dump the #542 68K PC traceback ring
monitor watch <addr> [r|w|rw]   memory watchpoint (also via GDB's own Z2/Z3/Z4)
```

`monitor trace` is deliberately included: the traceback ring is already
maintained unconditionally, so exposing it costs nothing and is exactly what a
developer wants after a wild jump.

## Breakpoint detection — the armed-flag gate

Three approaches were considered.

**Rejected: patch the instruction.** The classic stub technique — overwrite the
target instruction with a trap opcode — costs nothing when unused, but the
patched bytes live in *emulated* memory. They would be captured by savestates,
which is precisely the class of bug #400 was; they interact badly with
self-modifying code; and cart ROM versus RAM need different handling. Rejected
on the savestate hazard alone.

**Rejected: check a breakpoint table every instruction.** Simple and safe, but
an unconditional per-instruction cost in every shipped build on every target,
including the Raspberry Pi and classic ARM builds tuned in #567. This is the
mistake `vjtrace` made and then had to gate behind `VJ_TRACE`.

**Chosen: per-target armed counters, no memory modification.**

```c
/* src/debug/gdbtarget.h -- read by the hot paths, written only by the stub. */
extern int gdbArmed68K;
extern int gdbArmedGPU;
extern int gdbArmedDSP;
```

Each is the count of armed breakpoints for that processor. The hot-path check
is one load of a global that is in cache and one never-taken branch:

```c
/* in M68KInstructionHook(), after the existing traceback ring write */
if (gdbArmed68K && GDBCheckPC(GDB_TGT_68K, m68kPC))
   GDBHalt(GDB_TGT_68K, GDB_STOP_BREAKPOINT);
```

Per-target counters matter: a developer debugging 68K code must not make
`GPUExec()` pay for it.

When a counter is nonzero, `GDBCheckPC()` consults a 256-entry direct-mapped
cache indexed by `(pc >> 1) & 0xFF` holding the full PC, so the common case is
one load and one compare; a miss falls through to a linear scan of the
breakpoint array (breakpoint counts are small — the array is capped at 64 per
target).

### Why this should be free, with evidence

The `vjtrace` investigation measured its 6-8% regression and attributed it
specifically to the **per-instruction ring buffer writes**, not to the hook
mechanism: a plain function call and an inlined macro writing the ring directly
measured within 0.02% of each other across 11 paired interleaved samples. The
cost was doing work, not deciding whether to. A predicted-not-taken branch on a
hot global is the cheap half of that finding.

**This is a hypothesis and the implementation must prove it.** See Testing.

### Watchpoints

GDB's `Z2`/`Z3`/`Z4` (write/read/access watchpoints) map onto the existing
exported memory-breakpoint variables at `jaguar.c:356`, already checked at six
sites. Those sites currently call `M68KDebugHalt()`; they gain a branch to
`GDBHalt()` when the stub owns the watchpoint. No new instrumentation.

## The halt loop and `retro_run()`

When `GDBHalt()` fires, the call stack is
`retro_run() -> JaguarExecuteNew() -> ... -> M68KInstructionHook()`. The stub
does not unwind. It enters a loop that blocks on the socket, services RSP
packets, and returns only on `c` (continue), `s` (step), `vCont`, or client
disconnect. Because we never return, all emulator state is frozen exactly where
it stopped — which is the whole reason this halt model was chosen.

Consequences and mitigations:

- **The frontend UI freezes and audio starves.** Accepted. Mitigated by a
  `SET_MESSAGE_EXT` banner shown when the stub is *enabled* and again when a
  client *attaches* (both are moments we can still reach the frontend), warning
  that halts will freeze the frontend. A log line is emitted at every halt.
- **A user who forgets a stub is attached sees an apparent hang.** Mitigated by
  the optional `virtualjaguar_gdb_halt_timeout` core option: after N seconds
  halted with no client activity, auto-continue and log loudly. Default is
  `off`, because silently resuming a debugged machine is worse than a freeze
  for the audience this is built for.
- **Client disconnect while halted** resumes immediately and disarms every
  breakpoint. A dead debugger must never leave the machine wedged.
- **`retro_reset()` / `retro_unload_game()` arriving while halted** cannot
  happen — the frontend is blocked in `retro_run()`. Nothing to handle.

### Single-step

`s` / `vCont;s` for thread 1 uses the 68K's existing `SPCFLAG_DEBUGGER` path.
For threads 2 and 3 the stub sets a one-shot internal breakpoint at "any next
instruction" by arming the target's counter and having `GDBCheckPC()` return
true unconditionally for one instruction, then re-halting. The hardware
`SINGLE_STEP` bit is deliberately *not* used: it is guest-visible state that a
game can read, and writing it would be an observable perturbation.

## Security

The stub accepts a socket connection that can read and write emulated memory.
Treated accordingly:

- **Binds `127.0.0.1` only.** Never `0.0.0.0`, and no core option to change
  that. Anyone needing remote access can forward a port deliberately.
- **One client at a time.** A second connection is accepted and immediately
  closed, so a stale client cannot be silently displaced.
- **No GDB file-I/O extension.** `vFile` and `F` packets are not implemented —
  that is the part of RSP that reaches the host filesystem, and we have no use
  for it.
- **Every address is bounds-checked against the emulated memory map** before
  any read or write. A malformed or hostile `M`/`X` packet must never write
  outside emulated space. This is the single most important invariant in the
  implementation and gets direct unit tests.
- **Packet sizes are bounded** and parsed without unbounded allocation. The RSP
  parser is the attack surface; it is written to be boring.

## Savestate and determinism guarantees

- **The stub adds no savestate fields and requires no version bump.** The
  breakpoint table, the armed counters and the socket are host-side only.
- **With the stub enabled but no breakpoint armed, emulation is bit-identical
  to the stub being disabled.** This is a test, not a claim (see Testing).
- **Register and memory writes issued by GDB do change emulated state** — that
  is the user explicitly asking for it. A savestate taken after such a write is
  legitimately different, and this is documented rather than guarded.
- **Run-ahead**: a second core instance would fail to bind the port. Bind
  failure is logged once and the stub stays disabled for that instance rather
  than retrying. Debugging with run-ahead active is not supported and says so.

## Portability

`src/jerry/jlink_tcp.c` already provides working TCP on every target in
`Makefile.common`, including Windows, so the stub reuses that abstraction
rather than inventing one. On any target where the listener cannot be created,
the core option is hidden via `SET_CORE_OPTIONS_DISPLAY` and the stub reports
disabled — it never fails a content load.

C89/GNU89 throughout, like the rest of the core: declarations at the top of
every block, no C99.

## Core options

| Key | Values | Default |
|---|---|---|
| `virtualjaguar_gdb_stub` | `disabled` / `enabled` | `disabled` |
| `virtualjaguar_gdb_port` | 2345 / 2346 / 2347 / 3333 | `2345` |
| `virtualjaguar_gdb_wait` | `disabled` / `enabled` — halt at boot until a client attaches | `disabled` |
| `virtualjaguar_gdb_halt_timeout` | `off` / 30s / 60s / 300s | `off` |

`virtualjaguar_gdb_wait` matters more than it looks: debugging a boot-time
fault is impossible if the machine has already run past it before you attach.

## Testing

Five layers, in increasing cost:

1. **Protocol unit tests, no emulator.** `test/tools/test_gdbstub_proto.c`
   links `gdbstub.c` alone against a fake transport: packet framing, checksum
   validation, escape handling, `qSupported` negotiation, no-ack mode, and
   malformed-packet rejection. This is where the bounds-checking invariant is
   proven directly, including adversarial `M`/`X` addresses.
2. **Target glue tests.** Register read/write round-trips for all three
   processors, breakpoint arm/disarm counting, and the direct-mapped cache's
   behaviour on collisions.
3. **Determinism.** Savestate digest identical across three configurations:
   stub disabled; stub enabled and unattached; stub enabled, attached, zero
   breakpoints armed. Any difference is a bug in the gate.
4. **Perf, and this one gates the design.** A/B/B/A interleaved benchmark of a
   build with the hooks present against a build with them `#ifdef`'d out. The
   armed-flag hypothesis is only validated if the difference is within noise on
   an idle machine. Per this repo's documented history, measurements must be
   interleaved, never sequential, and `uptime` checked first — a sequential
   "-O3 +12.5%" once flipped to -11.7% when interleaved. **If the cost is not
   within noise, the shipped-by-default decision must be revisited rather than
   the number explained away.**
5. **End-to-end against real GDB.** Drive `m68k-elf-gdb` (from the toolchain in
   #581, if it ships gdb — otherwise a scripted RSP client) against a test ROM:
   attach, set a breakpoint at a known symbol, continue, assert the stop reply
   and PC, read a register, write memory, continue to exit.

## Out of scope

- **Tracepoints** (`QTDP` and the rest of GDB's tracepoint machinery).
- **GDB file I/O** (`vFile`), permanently — see Security.
- **Reverse debugging** (`bc`/`bs`). Interesting given we have savestates, but
  it is a separate feature with its own design.
- **Debugging across savestate load.** Loading a state while attached leaves
  breakpoints armed at addresses that may no longer mean anything; documented,
  not solved.
- **Non-stop mode.** All three processors halt together. Halting the 68K while
  the GPU runs is not modelled.

## Open questions

1. **Does the #581 toolchain include `m68k-elf-gdb`?** If not, test layer 5
   needs a scripted RSP client instead, and the user-facing documentation needs
   to tell people where to get a GDB that speaks m68k.
2. **What DWARF, if any, does `rln` emit?** Source-level debugging of the 68K
   is the headline benefit; if `rln` emits no usable debug info, the honest
   claim is symbol-level, not source-level, and the docs must say so.
3. **Register numbering for the RISC target descriptions.** Needs to be fixed
   once and then never changed, since clients cache it.

## Sequencing

Each phase is independently useful and independently abandonable.

1. **Protocol layer + 68K target, no breakpoints.** Attach, read registers,
   read memory, detach. Proves the transport and framing.
2. **68K breakpoints, watchpoints and stepping**, on the armed-flag gate. Run
   test layer 4 here — this is the go/no-go for the whole shipped-by-default
   premise.
3. **GPU and DSP threads**, XML descriptions, `monitor disasm`.
4. **Documentation**: a user guide covering attaching, the frontend-freeze
   behaviour, and the RISC disassembly limitation.
