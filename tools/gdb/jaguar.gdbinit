# jaguar.gdbinit -- convenience defaults for the Virtual Jaguar GDB stub (issue #652).
#
# docs/gdb-stub-guide.md is the full reference for what is on the other end
# of the socket; this file is just the "make it pleasant" layer over the
# stub's `monitor` commands and `target remote`.
#
# Load it directly:
#   gdb -x tools/gdb/jaguar.gdbinit -ex "jconnect"
# or, more conveniently, let the wrapper do all of this for you:
#   tools/gdb/connect.sh                    # 127.0.0.1:2345, the core's default
#   tools/gdb/connect.sh --port 3333
# or install it into ~/.gdbinit once with tools/gdb/install.sh, so these
# commands exist in every gdb session without sourcing anything by hand.
#
# Deliberately does NOT connect automatically when sourced -- only the
# `jconnect` command below does that. This file is meant to be safe to
# source from a GLOBAL ~/.gdbinit (see tools/gdb/install.sh): if it dialed
# 127.0.0.1:2345 unconditionally, every unrelated gdb session on your
# machine would print "Connection refused" on startup. `jconnect` only
# runs when you (or connect.sh) ask for it.
#
# Honesty, up front -- see docs/gdb-stub-guide.md for the long version:
#   - GDB cannot disassemble the GPU/DSP RISC ISA. Use `jdisasm` (wraps
#     `monitor disasm`), not `disassemble`, on threads 2/3.
#   - This is symbol-level debugging (rln -m load-map names), not
#     source-level -- rln emits classic mc68k COFF, not ELF/DWARF.
#   - A halt freezes the WHOLE frontend (video, audio, input) until you
#     `continue`, `stepi`, or disconnect. That is expected, not a hang.
#   - This project ships no gdb at all (tools/jaguar-toolchain pins
#     rmac/rln/lyxass/pc_jagcrypt/new_bjl -- no debugger). You bring your
#     own; see "No shipped m68k-elf-gdb" in the user guide.
#   - `jconnect` (below) can hang instead of returning. A real LLDB
#     attach was confirmed, byte-logged through a proxy, to hang exactly
#     like this -- the stub replies with silence instead of an empty
#     packet for a request it doesn't implement, and a strict client
#     just waits forever. See docs/gdb-stub-guide.md, "Known
#     limitations", before assuming this file or your gdb is at fault.

set confirm off
set pagination off

# GDB defaults to the host's own architecture. The 68K thread (thread 1)
# is served with no custom target description at all (see the guide's
# "three targets" table), so nothing else tells GDB what it is talking
# to. Most host `gdb` builds (Homebrew, apt) are NOT built with m68k
# support, so the next line commonly prints "Undefined item: \"m68k\"."
# and changes nothing -- harmless. Threads 2 and 3 (GPU/DSP) work
# regardless: their register layout comes from the stub's own
# qXfer:features XML, not from this setting.
set architecture m68k

# ---------------------------------------------------------------------
# jconnect [host port] -- connect to the stub.
#
# Kept separate from a bare `target remote` so this file can be sourced
# globally (see above) without side effects until you actually ask to
# connect. tools/gdb/connect.sh always calls this explicitly with the
# host/port you asked it for.
# ---------------------------------------------------------------------
define jconnect
  if $argc == 2
    target remote $arg0:$arg1
  else
    target remote 127.0.0.1:2345
  end
end
document jconnect
jconnect [host port] -- connect to the GDB stub. With no arguments,
connects to 127.0.0.1:2345, matching the core's virtualjaguar_gdb_port
default.
end

# ---------------------------------------------------------------------
# Convenience aliases over the stub's `monitor` commands (docs/gdb-stub-
# guide.md, "monitor commands"). These are the zero-dependency versions
# that work on any gdb, Python-enabled or not. tools/gdb/jaguar_gdb.py
# defines richer `jag-*` commands (argument checking, pretty-printed
# register dumps, friendlier errors against an older core) under
# different names on purpose, so loading both never collides.
# ---------------------------------------------------------------------

define jdisasm
  if $argc == 3
    monitor disasm $arg0 $arg1 $arg2
  else
    if $argc == 2
      monitor disasm $arg0 $arg1
    else
      help jdisasm
    end
  end
end
document jdisasm
jdisasm <gpu|dsp> <addr> [count] -- RISC disassembly (monitor disasm).
GDB has no backend for this ISA; this is the only way to see it.
end

define jregs
  if $argc == 1
    monitor regs $arg0
  else
    monitor regs
  end
end
document jregs
jregs [gpu|dsp] -- full register + control-register dump (monitor regs).
With no argument, dumps the 68K.
end

define jhalt
  if $argc == 1
    monitor halt $arg0
  else
    help jhalt
  end
end
document jhalt
jhalt <68k|gpu|dsp> -- halt one processor without a breakpoint
(monitor halt).
end

define jtrace
  monitor trace
end
document jtrace
jtrace -- dump the 68K PC traceback ring (monitor trace). Works even
outside a live debug session in spirit -- see the user guide -- but here
it needs an attached, halted-or-running stub like everything else in
this file.
end

define jwatch
  if $argc == 2
    monitor watch $arg0 $arg1
  else
    if $argc == 1
      monitor watch $arg0
    else
      monitor watch
    end
  end
end
document jwatch
jwatch [addr [r|w]] -- set the 68K memory watchpoint (monitor watch).
No arguments clears it. Only one watchpoint slot exists -- see
"Breakpoints and watchpoints" in the user guide.
end
