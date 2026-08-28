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
`m68k` architecture: full disassembly and backtraces come free. **Source-
level debugging with DWARF does not** — see Open Question 2, answered during
Phase 1 implementation: `rln` emits classic mc68k COFF, not ELF/DWARF, and its
one debug-info flag (`-g`) produced byte-identical output to a build without
it on a real test case. The honest claim is symbol-level (global symbol
addresses via `rln -m`'s load map), not source-level.

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

   **Measured, twice, and the first result was wrong (2026-08-27).**

   A re-run found the hooks cost **~4.5%**, not nothing:

   | Run | median delta | best-run delta | Mann-Whitney |
   |---|---|---|---|
   | 1 | +4.1% | +4.5% | z=-5.20, p=0.0000 |
   | 2 | +4.7% | +4.5% | z=-4.97, p=0.0000 |

   Both via `test/tools/opt_ab.sh 'VJ_GDB_STUB_DISABLE_HOOKS=0'
   'VJ_GDB_STUB_DISABLE_HOOKS=1' 12` (12 quartets, 24 samples per arm),
   host load average 5-7 -- *not* idle, but interleaving is the defense
   against exactly that, and the best-run figures (least contaminated)
   agree exactly across both runs.

   **The earlier measurement recorded here claimed +1.9% at p=0.50,
   "within noise".** It also described the host as idle while reporting a
   load average of 9-10, which cannot both be true. The likely mechanism
   for the null result: `VJ_GDB_STUB_DISABLE_HOOKS` was a local Makefile
   edit that was never added to `BUILD_AXES` and was reverted after the
   run, so it is plausible both arms were the same binary. `opt_ab.sh`
   prints a per-arm binary hash precisely so this can be checked; the
   re-runs above show two distinct hashes (`c861b6628cdf` vs
   `236f3467e128`), identical across both runs.

   The switch is now a permanent, documented make variable **listed in
   `BUILD_AXES`**, so the comparison is reproducible and cannot silently
   benchmark one binary against itself.

   **Consequence: the armed-flag hypothesis does not hold as stated, and
   per this document's own rule that revisits shipping the stub enabled by
   default.** Before reversing that decision, the cost should be localised
   -- `VJ_GDB_STUB_DISABLE_HOOKS` gates two different things, and they have
   very different fixes:

   1. the per-instruction `gdbArmed*` PC check in
      `M68KInstructionHook()`/`GPUExec()`/`DSPExec()` -- the thing the
      hypothesis was actually about; and
   2. `if (gdbArmed{GPU,DSP}) idleSkipActive = 0;` on the **idle-skip**
      path. Idle-loop fast-forward is where this core spends most of its
      budget (#569 measured 99.7% of budget in idle loops for some
      titles), so a branch there plausibly dominates the 4.5%.

   **Fully attributed (2026-08-27).** Each component was measured on its
   own control arm (all `test/tools/opt_ab.sh ... 12`, 24 samples/arm,
   host load 5-7):

   | Component | Cost | Significance |
   |---|---|---|
   | 68K hook (`M68KInstructionHook`) | ~0% | p=0.8366 |
   | Idle-skip gating (`idleSkipActive = 0`) | ~0% | p=0.4897 |
   | GPU per-instruction check | 1.8% | fixed, see below |
   | DSP per-instruction check | ~2.3-2.7% | p=0.0000 |

   **Fix applied: cache the armed flag in a slice-entry local**, exactly as
   #532 does for `pipeTiming`/`riscScale` in the same function. The globals
   were being reloaded GOT-indirect once per emulated instruction because
   the opcode call is opaque -- the branch was never the problem, the
   reload was. That took **4.5% -> 2.7%**, and the GPU's share to zero.

   **~2.0% remains, in the DSP loop, and is NOT explained.** Loop
   specialisation was tried (body extracted to a header, included into a
   plain loop and an armed loop, so the non-debug path contains no GDB
   code at all): it recovered only a further 0.7% on the DSP and **zero**
   on the GPU. Parked on branch `perf/gdb-dsp-loop-specialisation`
   (digest-verified, C89-clean) rather than merged -- 0.7% did not justify
   an extracted-body header in the emulator's hottest function.

   **Next step is real-hardware profiling, not more host measurement.**
   Every number here comes from an arm64 Mac that never dropped below load
   5. The residual should be re-attributed on a real target (tvOS/A-series,
   RPi) before any further surgery: the per-processor control arms
   `VJ_GDB_STUB_DISABLE_{HOOKS,IDLE_GATE,68K_HOOK,DSP_HOOK}` exist for
   exactly that and are listed in `BUILD_AXES`, so the attribution can be
   repeated there in four runs without re-deriving any of this.

   Shipped-by-default stands for now at a measured ~2% cost, deliberately,
   pending those numbers.



   **This is not merely a stand-in for gdb being unavailable.** Phase 1's
   `GDBHandlePacket` never sent the low-level `+`/`-` acknowledgement byte a
   real GDB client requires for every packet before `QStartNoAckMode` is
   negotiated (and that negotiation itself needs one such ack to complete).
   Phase 1's `gdb_attach_probe.py` passed without noticing this because it
   never checked for an ack — a real `m68k-elf-gdb` would have stalled on its
   very first `qSupported`.

   **Phase 2 closed that gap and extended this layer:**
   `test/tools/gdb_attach_probe.py` now verifies a `+` precedes every reply in
   ack mode, and that acks stop arriving after `QStartNoAckMode` — including
   the off-by-one (the `OK` reply to `QStartNoAckMode` itself is still acked;
   only packets after it are not). `test/tools/gdb_breakpoint_probe.py` is new:
   it launches the core headlessly (via `test/tools/gdb_determinism_probe` as
   a free-running host process), halts it at boot (`virtualjaguar_gdb_wait`),
   injects a single real 68K instruction (`BRA.S *`, a branch-to-self loop)
   into scratch RAM via `M`, redirects PC to it via `G`, arms a `Z0` there,
   continues, and asserts the stop reply names thread 1 and a fresh register
   read shows PC still at that exact address — a real breakpoint actually
   halting real (if synthetic) execution, not just a protocol-level OK reply.
   The gdb_wait halt is load-bearing, not just convenient: an early version of
   this script picked an arbitrary "probably unused" RAM address without
   halting first, and the ROM's own boot code overwrote the injected
   instruction before the CPU fetched it, within a single frame — confirmed
   by reading the address back and finding the ROM's own data there instead.

   `test/tools/gdb_reconnect_probe.py` covers what a single-connection probe
   structurally cannot: ack mode is per-CONNECTION, not per-content-load. It
   attaches twice to one long-lived core — the first client negotiating
   `QStartNoAckMode` exactly as a real gdb/lldb does, then dropping — and
   asserts the second client's very first `qSupported` reply is acked again.
   Before `GDBTargetResetState()` cleared `noAckMode`, it wasn't: the flag
   stayed latched from the dead client and the second client's handshake
   blocked on an ack that never came (observed with lldb, then this repo's own
   `gdb_attach_probe.py`, against one running core).

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

1. **Does the #581 toolchain include `m68k-elf-gdb`?** **Answered 2026-08-26,
   Phase 1 implementation: no.** `tools/jaguar-toolchain/PIN` pins exactly
   five tools — `rmac`, `rln`, `lyxass`, `pc_jagcrypt`, `new_bjl` — and
   `make jaguar-toolchain-build` was actually run to confirm: the built tree
   (`tools/vendor/jaguar-toolchain/`) contains no `gdb` binary anywhere, and
   none of the five tools is a debugger. There is no supported source of a
   Jaguar-flavoured GDB in this repo's toolchain. Consequence: test layer 5 is
   permanently a scripted RSP client (`test/tools/gdb_attach_probe.py`) rather
   than a stopgap until gdb shows up, and the user-facing docs must tell
   developers to bring their own `m68k-elf-gdb` (a stock `--target=m68k-elf`
   GNU binutils/gdb build, e.g. from the m68k bare-metal toolchains
   distributed for classic Mac/Amiga/Atari ST development, works against a
   plain `m68k` architecture with no custom target description) rather than
   implying this repo provides one.

   **Further verified 2026-08-27:** no bottled `m68k-elf-gdb` exists on
   Homebrew either (`brew search`: `aarch64-elf-gdb`, `arm-none-eabi-gdb`,
   `i386-elf-gdb`, `riscv64-elf-gdb`, `x86_64-elf-gdb` are all real bottles;
   `m68k-elf-gdb` is not among them) — a developer has to build GDB from
   source with `--target=m68k-elf` themselves; upstream GDB does support the
   architecture, there is just no pre-built binary to point users at.
   Homebrew *does* bottle a genuinely separate, actively-maintained C
   toolchain for this target — `m68k-elf-gcc` (16.2.0) and
   `m68k-elf-binutils` (2.47) — but that is GCC+GNU `ld`, unrelated to this
   repo's `rmac`/`rln` assembler pipeline (#581); using it would mean
   writing C and building a new path from its ELF+DWARF output to something
   loadable on the emulated Jaguar, not something this repo's build wires up
   today.
2. **What DWARF, if any, does `rln` emit?** **Answered 2026-08-26, Phase 1
   implementation: none.** Verified by building the toolchain and actually
   linking a test object: `rln -e` output identifies as `mc68k COFF object
   not stripped` (`file`(1)) — a pre-ELF Atari-era object format, not ELF —
   so `readelf -S` does not apply at all, and `objdump -h` (Xcode's
   `llvm-objdump`) fails outright with "not recognized as a valid object
   file" against it: no toolchain on this host can parse it as DWARF-bearing.
   `rln`'s own `-g` flag ("output source-level debugging") produced a file
   byte-for-byte identical (SHA-256) to the same link without `-g`, and
   neither contains any string referencing the source filename or a
   `.debug_*`-style section name. What Jaguar-era linking *does* give you is
   `rln -m`'s load map: global symbol names resolved to segment-relative
   addresses (confirmed working — see the TEXT/DATA/BSS-SEGMENT
   RELOCATABLE SYMBOLS tables it printed for the smoke test). **Conclusion:
   the honest claim is symbol-level debugging (breakpoints and reads by
   symbol name/address, resolved from `rln -m`'s map or the object's own
   symbol table), never source-level or line-level.** Every "source-level
   debugging" claim in this document has been corrected accordingly; do not
   reintroduce it without new evidence that some other tool in the chain
   (not `rln`) emits real DWARF.

   **Further verified 2026-08-27, by reading `rmac`/`rln` source directly**
   (mwenge/rmac@50e66ea, rln@a617009, both pinned in `PIN`) rather than only
   testing one linked output: exhaustive grep for `dwarf` and `.debug_`
   (case-insensitive) across both tools' entire source trees returns zero
   matches, under any flag. `rln`'s object-file magic-number detection only
   recognises BSD a.out (`0107`), DRI/Alcyon (`601A`), and `ar` archives —
   there is no ELF magic (`0x7F454C46`) check anywhere in it. `rmac` *can*
   emit an ELF object with a real `.symtab`/`.strtab` (`object.c`'s
   `ELFSectionNames` / `AddELFSymEntry`), but that path is orphaned: `rln`
   structurally cannot read it, so it is never exercised by the actual
   working `rmac`→`rln` pipeline. And `-g`'s real mechanism, per `rln.c`'s
   `OSTAdd()`, is a filter that keeps or drops symbols whose type carries a
   `0xF0000000` "debug" tag in `rln`'s own proprietary Atari-era symbol
   table — which is what explains the byte-identical link above: whatever
   symbol category that tag marks was never present to filter in the tested
   case, so the flag had nothing to do either way.
3. **Register numbering for the RISC target descriptions.** **Answered
   2026-08-27, Phase 3 implementation: R0-R31 = index 0-31, PC = 32,
   FLAGS = 33 (34 registers total), identical for the GPU and DSP
   descriptions.** Fixed now — do not renumber; see "GPU/DSP register
   files" below.

## Phase 2/3 implementation notes (2026-08-27)

Phases 2-4 landed together. A few things resolved or turned out different
from how this document originally described them:

- **The `ALPINE_FUNCTIONS` six-site claim ("Watchpoints") was verified
  true, but not for the reason it looked true at first.** `ALPINE_FUNCTIONS`
  is not a build-system flag passed by any Makefile target — it is
  `#define`d unconditionally near the top of `src/core/jaguar.c` itself
  (line 80), so the six `m68k_read/write_memory_{8,16,32}` sites really are
  compiled into every build, exactly as this document said. (An earlier
  pass at this implementation grepped only the Makefiles for
  `-DALPINE_FUNCTIONS`, found nothing, and nearly reported this as a design-
  doc inaccuracy before re-checking the source directly and finding the
  `#define` in `jaguar.c`.) What *was* true is that the six sites were
  functionally dead: `bpmActive` was default-`false` and nothing in the
  tree ever set it, and the six sites' only consumer, `M68KDebugHalt()`,
  sets `SPCFLAG_DEBUGGER`, which makes `m68k_execute()` return early
  (unwind) rather than block — and nothing ever called the matching
  `M68KDebugResume()`, so the pre-existing "feature" would have wedged the
  68K forever if anything had ever armed it. The six sites now call
  `GDBMemWatchHit()` instead of `M68KDebugHalt()`; this module is the
  first and only thing that ever sets `bpmActive`/`bpmAddress1`.
- **Single watchpoint slot, not a table.** "Watchpoints ride the existing
  exported memory-access breakpoint vars" was implemented literally: one
  slot, matching `bpmAddress1`'s singular shape. A second `Z2`/`Z3`/`Z4`
  while one is already armed at a different address is refused (E01)
  rather than silently displacing it or growing a table. Documented as a
  known limitation in the user guide.
- **Stepping does NOT use the 68K's `SPCFLAG_DEBUGGER` path**, contrary to
  what this document originally specified. All three processors' stepping
  (and `monitor halt <target>`, and the `gdb_wait` boot halt) share one
  one-shot primitive on the same per-target breakpoint-table structure
  `GDBCheckPC()` already uses: arm a flag that matches unconditionally on
  that processor's very next instruction hook, consumed immediately on
  match. Reasoning for the deviation: `SPCFLAG_DEBUGGER` makes
  `m68k_execute()` return early to its *caller* rather than block in
  place, so something outside the hook would have to notice "a step just
  completed" and re-invoke `GDBHalt()` — an extra layer of scheduler
  plumbing the one-shot primitive doesn't need, and which wasn't worth the
  risk of getting subtly wrong under time pressure when a uniform
  mechanism across all three processors was available and already
  necessary for the GPU/DSP anyway. `SPCFLAG_DEBUGGER`/`M68KDebugHalt`/
  `M68KDebugResume` remain unused by this module.
- **GPU/DSP register/PC/flags accessors needed adding or un-gating.**
  `GPUGetReg`/`GPUGetPC` predate this feature (issue #406) and were
  already unconditional; `GPUGetFlags` and `DSPGetReg` existed but were
  `#ifdef VJ_TRACE`-only and are now unconditional (matching `GPUGetReg`'s
  existing precedent, not a new exception). `DSPGetPC`, and the write side
  of all of these (`GPUSetReg`/`GPUSetPC`/`GPUSetFlags`/`DSPSetReg`/
  `DSPSetPC`/`DSPSetFlags`), plus read-only `GPUGetControl`/`DSPGetControl`
  for `monitor regs`, did not exist at all and were added. The writes are
  raw pokes (like `m68k_set_reg`), not simulated MMIO writes, specifically
  so that GDB setting PC or FLAGS does not also trigger a G_CTRL/D_CTRL-
  style side effect (e.g. toggling GPUGO) as a surprise side effect of a
  register-inspection action.
- **The RISC disassembler is genuinely shared, not just "not duplicated
  in spirit."** `src/debug/gdbdisasm.h` is a header-only module included
  by both `src/debug/gdbtarget.c` (`monitor disasm`, live) and
  `test/tools/gpu_disasm_dump.c` (offline, dlsym-based) — the latter was
  refactored to drop its own inline copy of the mnemonic table and operand
  decode logic in favour of the shared one.
- **Multi-architecture GDB sessions (native m68k thread 1 + custom-
  description threads 2/3 in one inferior) are implemented to the RSP
  specification and covered by this repo's own scripted test client, but
  never verified against a real `gdb` binary** — consistent with Open
  Question 1's answer (no `m68k-elf-gdb` ships anywhere in this repo's
  toolchain). Documented as an explicit caveat in the user guide rather
  than claimed as verified.

## Sequencing

Each phase is independently useful and independently abandonable.

1. **Protocol layer + 68K target, no breakpoints.** Attach, read registers,
   read memory, detach. Proves the transport and framing. **Shipped**
   (issue #652, PR #662).
2. **68K breakpoints, watchpoints and stepping**, on the armed-flag gate. Run
   test layer 4 here — this is the go/no-go for the whole shipped-by-default
   premise. **Shipped** — see the PR for this phase for the perf
   measurement's result and the machine load it was (or wasn't) taken
   under; do not treat this document as the record of that number.
3. **GPU and DSP threads**, XML descriptions, `monitor disasm`. **Shipped**,
   with the multi-architecture-session caveat above.
4. **Documentation**: a user guide covering attaching, the frontend-freeze
   behaviour, and the RISC disassembly limitation. **Shipped** as
   `docs/gdb-stub-guide.md`.
