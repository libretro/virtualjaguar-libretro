"""tools/gdb/jaguar_gdb.py -- GDB convenience commands for the Virtual
Jaguar GDB remote stub (issue #652).

    https://github.com/libretro/virtualjaguar-libretro

Load it with `source tools/gdb/jaguar_gdb.py` (tools/gdb/connect.sh and
tools/gdb/jaguar.gdbinit do this for you), or install it permanently with
tools/gdb/install.sh.

POSSIBLE KNOWN ISSUE: while building this file, a real LLDB `gdb-remote`
attach to the stub was confirmed (via a proxied, byte-logged connection)
to hang indefinitely, root-caused to the stub replying with silence
instead of the RSP-mandated empty packet for a request it doesn't
implement. That was verified against LLDB specifically, not gdb -- this
project's own toolchain ships no gdb to test against (see "No shipped
m68k-elf-gdb" in the user guide) -- but the same stub-side bug plausibly
affects a real gdb's `target remote` too, since gdb's handshake also
sends packet types this stub doesn't recognize. If `target remote` never
completes, see docs/gdb-stub-guide.md, "Known limitations", before
assuming your gdb build or this plugin is at fault.

Runs under whatever Python 3 your gdb build embeds. No third-party
dependencies -- only the standard library and the `gdb` module gdb
injects into any script it sources.

What this adds, over typing `monitor ...` by hand:
  - `jag-disasm`, `jag-regs`, `jag-trace`, `jag-halt`, `jag-watch` --
    thin, validated wrappers with usage messages instead of gdb's raw
    "empty reply" behaviour for a bad `monitor` command.
  - `jag-info-registers` -- a labelled, grid-formatted dump of the
    current thread's registers when it is the GPU or DSP RISC core
    (thread 2 or 3), including a decoded FLAGS register. The 68K thread
    (1) already gets this for free from GDB's own `info registers`, so
    this command defers to that message rather than reimplementing it.

Register numbering for the GPU/DSP `org.atari.jaguar.{gpu,dsp}` target
descriptions is fixed forever once shipped (clients cache it across a
session -- src/debug/gdbstub.h): R0-R31 = register indices 0-31, PC = 32,
FLAGS = 33. This file never invents its own numbering; it reads registers
by the XML's own names ("r0".."r31", "pc", "flags").

Every command here is written to degrade gracefully rather than throw a
traceback: no connection, an older core that predates a `monitor`
subcommand, or a target description mismatch all print a short, friendly
line and return, instead of leaking a Python backtrace into the user's
gdb session.
"""

import gdb


# GPU_FLAGS / D_FLAGS bit layout, from src/tom/gpu.c and src/jerry/dsp.c
# (both define the same bits; the DSP additionally has a sixth CINT/
# INT_ENA line). This is a debugging convenience decode, not a hardware
# accuracy claim -- if it drifts from those source files, that is a bug
# in this file, not in the emulator.
_FLAG_BITS_COMMON = (
    (0x0001, "Z"),
    (0x0002, "C"),
    (0x0004, "N"),
    (0x0008, "IMASK"),
    (0x0010, "IE0"),
    (0x0020, "IE1"),
    (0x0040, "IE2"),
    (0x0080, "IE3"),
    (0x0100, "IE4"),
    (0x4000, "REGPAGE"),
    (0x8000, "DMAEN"),
)
_FLAG_BITS_DSP_EXTRA = ((0x10000, "IE5"),)

_RISC_GP_REGS = 32  # r0..r31


def _decode_flags(flags, is_dsp):
    bits = _FLAG_BITS_COMMON + (_FLAG_BITS_DSP_EXTRA if is_dsp else ())
    active = [label for mask, label in bits if flags & mask]
    return ",".join(active) if active else "-"


def _monitor(text):
    """Run `monitor <text>` and return its output, or None on any
    failure -- a missing connection, an old core with no qRcmd handler
    for this subcommand, or anything else gdb.execute can raise. Callers
    must handle None; this function has already printed a one-line
    explanation, so callers should just return quietly."""
    try:
        out = gdb.execute("monitor " + text, to_string=True)
    except gdb.error as exc:
        print("jaguar: %s" % exc)
        return None
    return out


def _print_monitor_result(out, empty_hint):
    if out is None:
        return
    if out.strip() == "":
        print(empty_hint)
        return
    print(out, end="" if out.endswith("\n") else "\n")


class JagDisasm(gdb.Command):
    """jag-disasm <gpu|dsp> <addr> [count] -- RISC disassembly via the
stub's `monitor disasm`.

GDB ships no backend for the Jaguar RISC ISA (see docs/gdb-stub-
guide.md, "The RISCs have no GDB disassembly") -- this is the only way
to see GPU/DSP code from inside gdb. <addr> is hex, with or without a
leading 0x/$. [count] defaults to 4 instructions, capped at 64."""

    def __init__(self):
        super(JagDisasm, self).__init__("jag-disasm", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        args = argument.split()
        if len(args) not in (2, 3) or args[0] not in ("gpu", "dsp"):
            print("usage: jag-disasm <gpu|dsp> <addr> [count]")
            return
        addr = args[1].lstrip("$")
        if addr.lower().startswith("0x"):
            addr = addr[2:]
        rest = " ".join([args[0], addr] + args[2:])
        out = _monitor("disasm " + rest)
        _print_monitor_result(
            out,
            "jaguar: no disassembly returned -- is a core with the GDB "
            "stub attached and reachable? (an older core build may not "
            "implement `monitor disasm` at all)",
        )


class JagRegs(gdb.Command):
    """jag-regs [gpu|dsp] -- full register + control-register dump via
the stub's `monitor regs`. With no argument, dumps the 68K."""

    def __init__(self):
        super(JagRegs, self).__init__("jag-regs", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        arg = argument.strip()
        if arg and arg not in ("gpu", "dsp"):
            print("usage: jag-regs [gpu|dsp]")
            return
        out = _monitor("regs " + arg if arg else "regs")
        _print_monitor_result(
            out,
            "jaguar: no register dump returned -- is a core with the "
            "GDB stub attached and reachable?",
        )


class JagTrace(gdb.Command):
    """jag-trace -- dump the 68K PC traceback ring (monitor trace,
issue #542). Free to use after any wild jump or crash: the ring is
already maintained unconditionally by the core."""

    def __init__(self):
        super(JagTrace, self).__init__("jag-trace", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        out = _monitor("trace")
        _print_monitor_result(
            out,
            "jaguar: no trace returned -- is a core with the GDB stub "
            "attached and reachable? (an older core build may not "
            "implement `monitor trace`)",
        )


class JagHalt(gdb.Command):
    """jag-halt <68k|gpu|dsp> -- halt one processor without a
breakpoint (monitor halt). Takes effect the next time that processor
actually runs an instruction."""

    def __init__(self):
        super(JagHalt, self).__init__("jag-halt", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        arg = argument.strip()
        if arg not in ("68k", "gpu", "dsp"):
            print("usage: jag-halt <68k|gpu|dsp>")
            return
        out = _monitor("halt " + arg)
        _print_monitor_result(
            out,
            "jaguar: no response -- is a core with the GDB stub "
            "attached and reachable?",
        )


class JagWatch(gdb.Command):
    """jag-watch [addr [r|w]] -- set the 68K memory watchpoint (monitor
watch). No arguments clears it. Only one watchpoint slot exists across
the whole stub -- see "Breakpoints and watchpoints" in the user guide;
GDB's own `watch`/`rwatch`/`awatch` use the same slot."""

    def __init__(self):
        super(JagWatch, self).__init__("jag-watch", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        arg = argument.strip()
        args = arg.split()
        if len(args) not in (0, 1, 2) or (len(args) == 2 and args[1] not in ("r", "w")):
            print("usage: jag-watch [addr [r|w]]")
            return
        out = _monitor("watch " + arg if arg else "watch")
        _print_monitor_result(
            out,
            "jaguar: no response -- is a core with the GDB stub "
            "attached and reachable?",
        )


class JagInfoRegisters(gdb.Command):
    """jag-info-registers -- pretty-print the currently selected
thread's Jaguar registers. On the GPU/DSP threads (2/3) this decodes
FLAGS and lays out R0-R31 in a grid; on the 68K thread (1) it just
points you at gdb's own `info registers`, which already understands
that architecture natively.

Reads registers directly (not via `monitor regs`), so it reflects
exactly what GDB's target description says thread 2/3 register 0-31,
32, 33 are -- see src/debug/gdbstub.h's GDB_RISC_REG_* numbering, which
is fixed forever and never reinvented here."""

    def __init__(self):
        super(JagInfoRegisters, self).__init__("jag-info-registers", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        try:
            thread = gdb.selected_thread()
        except gdb.error as exc:
            print("jaguar: %s" % exc)
            return
        if thread is None:
            print(
                "jaguar: no thread selected -- connect first "
                "(tools/gdb/connect.sh, or `jconnect`)."
            )
            return

        num = thread.num
        if num == 1:
            print(
                "jaguar: thread 1 is the 68K -- it has no custom target "
                "description, so gdb's own `info registers` already "
                "knows it natively."
            )
            return
        if num not in (2, 3):
            print(
                "jaguar: unrecognized thread %d (expected 1=68K, 2=GPU, "
                "3=DSP -- see docs/gdb-stub-guide.md, \"The three "
                "targets\")." % num
            )
            return

        name = "GPU" if num == 2 else "DSP"
        is_dsp = num == 3

        try:
            frame = gdb.selected_frame()
            regs = [
                int(frame.read_register("r%d" % i)) & 0xFFFFFFFF
                for i in range(_RISC_GP_REGS)
            ]
            pc = int(frame.read_register("pc")) & 0xFFFFFFFF
            flags = int(frame.read_register("flags")) & 0xFFFFFFFF
        except (gdb.error, ValueError) as exc:
            print(
                "jaguar: could not read %s registers (%s) -- this may "
                "be an older core build with a different target "
                "description, or nothing is halted right now." % (name, exc)
            )
            return

        print(
            "%s  PC=$%06X  FLAGS=$%08X [%s]"
            % (name, pc, flags, _decode_flags(flags, is_dsp))
        )
        for row_start in range(0, _RISC_GP_REGS, 4):
            row = regs[row_start:row_start + 4]
            print(
                "  ".join(
                    "r%-2d=$%08X" % (row_start + i, v) for i, v in enumerate(row)
                )
            )


def _register(command_cls):
    try:
        command_cls()
    except Exception as exc:  # pragma: no cover -- defensive only
        print("jaguar-gdb: could not register %s (%s)" % (command_cls.__name__, exc))


for _cls in (JagDisasm, JagRegs, JagTrace, JagHalt, JagWatch, JagInfoRegisters):
    _register(_cls)

print(
    "jaguar-gdb: loaded (jag-disasm, jag-regs, jag-trace, jag-halt, "
    "jag-watch, jag-info-registers)"
)
