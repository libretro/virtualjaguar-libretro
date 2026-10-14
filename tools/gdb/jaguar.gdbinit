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
#   - You need a gdb BUILT WITH m68k support. Homebrew's `gdb` has it
#     (`brew install gdb`); Debian/Ubuntu need `gdb-multiarch`, not `gdb`.
#     Check: `gdb -nx -batch -ex "set architecture m68k"` must not print
#     "Undefined item". LLDB cannot debug the 68K at all (no m68k triple).
#   - Nothing here sets `architecture` or `remote target-features-packet`:
#     the stub serves an m68k target description, so a plain
#     `target remote` selects m68k by itself. (Cores older than #936 did
#     not; for those add `set architecture m68k` and
#     `set remote target-features-packet off` before jconnect.)
#   - ONE thing a target description cannot say is byte order. jconnect
#     runs `set endian big` for you; without it every register and memory
#     word is byte-swapped. It is deliberately NOT a top-level setting
#     here: tools/gdb/install.sh sources this file from your global
#     ~/.gdbinit, and a global `set endian big` would corrupt every
#     unrelated gdb session. (Loading an m68k ELF/COFF with `file` also
#     fixes it, since the binary carries its own byte order.)
#   - Only the 68K is a gdb thread. The GPU/DSP are reached through
#     `monitor regs|disasm|halt gpu|dsp` (the j* aliases below), because
#     gdb applies one register description to every thread.
#   - GDB cannot disassemble the GPU/DSP RISC ISA. Use `jdisasm` (wraps
#     `monitor disasm`), not `disassemble`.
#   - This is symbol-level debugging (rln -m load-map names), not
#     source-level -- rln emits classic mc68k COFF, not ELF/DWARF.
#   - A halt freezes the WHOLE frontend (video, audio, input) until you
#     `continue`, `stepi`, or disconnect. That is expected, not a hang.
#   - End a session with `detach` or `disconnect`; both leave the
#     emulator running. A watchpoint (`watch *(int*)0xf0210c`) stops with
#     "Program received signal SIGTRAP" at the access; only one watch
#     slot exists.

set confirm off
set pagination off

# ---------------------------------------------------------------------
# jconnect [host port] -- connect to the stub.
#
# Kept separate from a bare `target remote` so this file can be sourced
# globally (see above) without side effects until you actually ask to
# connect. tools/gdb/connect.sh always calls this explicitly with the
# host/port you asked it for.
# ---------------------------------------------------------------------
define jconnect
  set endian big
  if $argc == 2
    target remote $arg0:$arg1
  else
    target remote 127.0.0.1:2345
  end
end
document jconnect
jconnect [host port] -- connect to the GDB stub. With no arguments,
connects to 127.0.0.1:2345, matching the core's virtualjaguar_gdb_port
default. Sets `endian big` first (the Jaguar is big-endian and a target
description cannot say so). Needs a gdb with m68k support.
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
  if $argc == 1
    monitor trace $arg0
  else
    monitor trace
  end
end
document jtrace
jtrace [N] -- the newest N (default 64, max 1024) 68K PCs from the
traceback ring (monitor trace), oldest first, so the last line is the
most recent. Needs an attached stub like everything else in this file.
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
