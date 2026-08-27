"""tools/gdb/jaguar_lldb.py -- LLDB convenience commands for the Virtual
Jaguar GDB remote stub (issue #652).

    https://github.com/libretro/virtualjaguar-libretro

The stub speaks GDB's Remote Serial Protocol, and LLDB speaks RSP too, so
the transport is free -- `gdb-remote host:port` connects LLDB to the same
socket `target remote` connects gdb to. This file is the same convenience
layer as tools/gdb/jaguar_gdb.py (jag-disasm, jag-regs, jag-trace,
jag-halt, jag-watch, jag-info-registers), reimplemented against LLDB's
Python API instead of gdb's.

Load it with:
    (lldb) command script import tools/gdb/jaguar_lldb.py
    (lldb) gdb-remote 127.0.0.1:2345
tools/gdb/install.sh adds the import line to ~/.lldbinit for you.

KNOWN ISSUE, verified while building this file: `gdb-remote` itself can
hang indefinitely against this stub -- confirmed via a proxied, byte-
logged LLDB attach, root-caused to the stub replying with silence
instead of the RSP-mandated empty packet for a request it doesn't
implement (LLDB's own handshake sends one during attach). This is a
stub-side protocol bug, not something this plugin can work around --
see docs/gdb-stub-guide.md, "Known limitations", for the tracked
symptom. If `gdb-remote` never returns, that is why; it is not this
plugin failing to load or register commands (both happen before the
`gdb-remote` call and print a confirmation either way).

Runs under whatever Python 3 your lldb build embeds (Xcode's, or a
distro's liblldb). No third-party dependencies -- only the standard
library and the `lldb` module LLDB injects when it imports a script.

LLDB's gdb-remote process plugin forwards a top-level `monitor <text>`
command to the stub's qRcmd handler, exactly like gdb's `monitor` --
that is what every command below sends.

Register numbering for the GPU/DSP `org.atari.jaguar.{gpu,dsp}` target
descriptions is fixed forever once shipped (src/debug/gdbstub.h): R0-R31
= register indices 0-31, PC = 32, FLAGS = 33. This file never invents its
own numbering; it reads registers by the XML's own names ("r0".."r31",
"pc", "flags").

Every command degrades gracefully: no connection, an older core that
predates a `monitor` subcommand, or a target description mismatch all
print a short, friendly line instead of an LLDB error dump or a Python
traceback.
"""

import lldb


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

_RISC_GP_REGS = 32


def _decode_flags(flags, is_dsp):
    bits = _FLAG_BITS_COMMON + (_FLAG_BITS_DSP_EXTRA if is_dsp else ())
    active = [label for mask, label in bits if flags & mask]
    return ",".join(active) if active else "-"


def _monitor(debugger, text):
    """Run `monitor <text>` via the command interpreter and return its
    output, or None on any failure. Callers must handle None; a short
    explanation has already been printed."""
    interpreter = debugger.GetCommandInterpreter()
    result = lldb.SBCommandReturnObject()
    try:
        interpreter.HandleCommand("monitor " + text, result)
    except Exception as exc:  # pragma: no cover -- defensive only
        print("jaguar: monitor command raised %s" % exc)
        return None
    if not result.Succeeded():
        err = (result.GetError() or "").strip()
        print(
            "jaguar: %s"
            % (err or "monitor command failed -- is a process connected?")
        )
        return None
    return result.GetOutput() or ""


def _print_monitor_result(out, empty_hint):
    if out is None:
        return
    if out.strip() == "":
        print(empty_hint)
        return
    print(out, end="" if out.endswith("\n") else "\n")


def jag_disasm(debugger, command, result, internal_dict):
    """jag-disasm <gpu|dsp> <addr> [count] -- RISC disassembly via the
stub's `monitor disasm`. LLDB has no backend for the Jaguar RISC ISA,
same as GDB -- this is the only way to see GPU/DSP code. <addr> is hex,
with or without a leading 0x/$. [count] defaults to 4, capped at 64."""
    args = command.split()
    if len(args) not in (2, 3) or args[0] not in ("gpu", "dsp"):
        print("usage: jag-disasm <gpu|dsp> <addr> [count]")
        return
    addr = args[1].lstrip("$")
    if addr.lower().startswith("0x"):
        addr = addr[2:]
    rest = " ".join([args[0], addr] + args[2:])
    out = _monitor(debugger, "disasm " + rest)
    _print_monitor_result(
        out,
        "jaguar: no disassembly returned -- is a core with the GDB "
        "stub attached and reachable via gdb-remote? (an older core "
        "build may not implement `monitor disasm` at all)",
    )


def jag_regs(debugger, command, result, internal_dict):
    """jag-regs [gpu|dsp] -- full register + control-register dump via
the stub's `monitor regs`. With no argument, dumps the 68K."""
    arg = command.strip()
    if arg and arg not in ("gpu", "dsp"):
        print("usage: jag-regs [gpu|dsp]")
        return
    out = _monitor(debugger, "regs " + arg if arg else "regs")
    _print_monitor_result(
        out,
        "jaguar: no register dump returned -- is a core with the GDB "
        "stub attached and reachable via gdb-remote?",
    )


def jag_trace(debugger, command, result, internal_dict):
    """jag-trace -- dump the 68K PC traceback ring (monitor trace,
issue #542). Free to use after any wild jump or crash."""
    out = _monitor(debugger, "trace")
    _print_monitor_result(
        out,
        "jaguar: no trace returned -- is a core with the GDB stub "
        "attached and reachable via gdb-remote? (an older core build "
        "may not implement `monitor trace`)",
    )


def jag_halt(debugger, command, result, internal_dict):
    """jag-halt <68k|gpu|dsp> -- halt one processor without a
breakpoint (monitor halt)."""
    arg = command.strip()
    if arg not in ("68k", "gpu", "dsp"):
        print("usage: jag-halt <68k|gpu|dsp>")
        return
    out = _monitor(debugger, "halt " + arg)
    _print_monitor_result(
        out,
        "jaguar: no response -- is a core with the GDB stub attached "
        "and reachable via gdb-remote?",
    )


def jag_watch(debugger, command, result, internal_dict):
    """jag-watch [addr [r|w]] -- set the 68K memory watchpoint (monitor
watch). No arguments clears it. Only one watchpoint slot exists across
the whole stub -- see "Breakpoints and watchpoints" in the user guide."""
    arg = command.strip()
    args = arg.split()
    if len(args) not in (0, 1, 2) or (len(args) == 2 and args[1] not in ("r", "w")):
        print("usage: jag-watch [addr [r|w]]")
        return
    out = _monitor(debugger, "watch " + arg if arg else "watch")
    _print_monitor_result(
        out,
        "jaguar: no response -- is a core with the GDB stub attached "
        "and reachable via gdb-remote?",
    )


def jag_info_registers(debugger, command, result, internal_dict):
    """jag-info-registers -- pretty-print the currently selected
thread's Jaguar registers. On the GPU/DSP threads (2/3) this decodes
FLAGS and lays out R0-R31 in a grid; on the 68K thread (1) it points
you at LLDB's own `register read`, which already understands that
architecture (LLDB ships an m68k disassembler; see the user guide for
what that still doesn't give you -- source-level debugging)."""
    target = debugger.GetSelectedTarget()
    if not target or not target.IsValid():
        print(
            "jaguar: no target selected -- connect first "
            "(gdb-remote 127.0.0.1:2345)."
        )
        return
    process = target.GetProcess()
    if not process or not process.IsValid():
        print("jaguar: no process -- connect first (gdb-remote 127.0.0.1:2345).")
        return
    thread = process.GetSelectedThread()
    if not thread or not thread.IsValid():
        print("jaguar: no thread selected.")
        return

    num = thread.GetIndexID()
    if num == 1:
        print(
            "jaguar: thread 1 is the 68K -- LLDB's own `register read` "
            "already knows that architecture natively."
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

    frame = thread.GetSelectedFrame()
    if not frame or not frame.IsValid():
        print("jaguar: no frame selected.")
        return

    def read(reg_name):
        val = frame.FindRegister(reg_name)
        if not val or not val.IsValid():
            return None
        return val.GetValueAsUnsigned()

    regs = [read("r%d" % i) for i in range(_RISC_GP_REGS)]
    pc = read("pc")
    flags = read("flags")
    if pc is None or flags is None or any(r is None for r in regs):
        print(
            "jaguar: could not read %s registers -- this may be an "
            "older core build with a different target description, or "
            "nothing is halted right now." % name
        )
        return

    print(
        "%s  PC=$%06X  FLAGS=$%08X [%s]"
        % (name, pc & 0xFFFFFFFF, flags & 0xFFFFFFFF, _decode_flags(flags, is_dsp))
    )
    for row_start in range(0, _RISC_GP_REGS, 4):
        row = regs[row_start:row_start + 4]
        print(
            "  ".join(
                "r%-2d=$%08X" % (row_start + i, v & 0xFFFFFFFF)
                for i, v in enumerate(row)
            )
        )


_COMMANDS = (
    ("jag-disasm", jag_disasm),
    ("jag-regs", jag_regs),
    ("jag-trace", jag_trace),
    ("jag-halt", jag_halt),
    ("jag-watch", jag_watch),
    ("jag-info-registers", jag_info_registers),
)


def __lldb_init_module(debugger, internal_dict):
    """Called once by LLDB when this file is `command script import`ed.
    Registers each function above as a top-level LLDB command, wired
    through this module so `internal_dict` (and therefore the functions
    themselves) stay reachable regardless of how LLDB reloads modules."""
    module = __name__
    loaded = []
    for command_name, func in _COMMANDS:
        try:
            debugger.HandleCommand(
                "command script add -f %s.%s %s" % (module, func.__name__, command_name)
            )
            loaded.append(command_name)
        except Exception as exc:  # pragma: no cover -- defensive only
            print("jaguar-lldb: could not register %s (%s)" % (command_name, exc))
    print("jaguar-lldb: loaded (%s)" % ", ".join(loaded))
