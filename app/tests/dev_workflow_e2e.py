#!/usr/bin/env python3
"""The debugger's command line, on the real app and the real core, headless (docs/app-debug-tools.md, "Homebrew workflow").

usage: dev_workflow_e2e.py <virtualjaguar> <yarc.j64> <fixtures/symbols dir>

A headless run has no window to press Continue in, so the app releases its own halts at once (App::haltedFrame): each
case below therefore sees exactly one "[debug] halt:" line and then runs to the end.

  1. --break-at-boot: the halt is the very first thing the 68000 does, it is the app's own (released, not left to a gdb
     client), and the frame hash at the end is the one a plain run gives: halting changes nothing the game can see.
  2. --break by symbol: armed before the first frame, hit at the symbol's address.
  3. --break by address and with an offset; a name that does not exist is reported and the run goes on.
  4. --symbols: names for a program that carries none.
  5. --watch by symbol: the 68000's write to the variable halts with the watched address.
  6. A saved "Break at start" does not park a headless run: only the flag does.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HALT = re.compile(r"\[debug\] halt: (\S+) at \$([0-9A-F]{6}) \((\w+)\)")
HASH = re.compile(r"^HASH ([0-9a-f]{8}) ", re.M)


def run(exe, data, *args):
    cmd = [exe, "--headless", "--data-dir", str(data), *args]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    log = r.stdout + r.stderr
    if r.returncode:
        raise AssertionError(f"exit {r.returncode}: {' '.join(cmd)}\n{log}")
    return log


def halts(log):
    return [(m.group(1), int(m.group(2), 16), m.group(3)) for m in HALT.finditer(log)]


def main():
    exe, rom, fixtures = (str(Path(v).resolve()) for v in sys.argv[1:4])
    cof = str(Path(fixtures) / "globals.cof")        # _start $4000, main_loop $400A, frame_count (bss), with symbols
    stripped = str(Path(fixtures) / "stripped.cof")  # the same program linked without a symbol table
    # (the fixture is a counting loop that draws nothing: --allow-blank keeps its blank last frame from failing the run)
    with tempfile.TemporaryDirectory(prefix="vj-dev-e2e-") as root:
        data = Path(root)

        # 1. break at boot
        plain = run(exe, data, "--frames", "60", rom)
        assert not halts(plain), plain
        boot = run(exe, data, "--frames", "60", "--break-at-boot", rom)
        assert "armed before the first instruction: 0 breakpoints, break at start" in boot, boot
        h = halts(boot)
        assert len(h) == 1 and h[0][0] == "68000" and h[0][2] == "break", boot
        want, got = HASH.search(plain), HASH.search(boot)
        assert want and got and want.group(1) == got.group(1), f"hash {got and got.group(1)} != {want and want.group(1)}\n{boot}"

        # 2. a breakpoint by name, armed before the first frame
        log = run(exe, data, "--allow-blank", "--frames", "20", "--break", "main_loop", cof)
        assert "armed before the first instruction: 1 breakpoint" in log, log
        assert halts(log) == [("68000", 0x400A, "breakpoint")], log

        # 3. by address, with an offset, and a name that is not there
        log = run(exe, data, "--allow-blank", "--frames", "20", "--break", "$400A", cof)
        assert halts(log) == [("68000", 0x400A, "breakpoint")], log
        log = run(exe, data, "--allow-blank", "--frames", "20", "--break", "68k:_start+A", cof)
        assert halts(log) == [("68000", 0x400A, "breakpoint")], log
        log = run(exe, data, "--allow-blank", "--frames", "20", "--break", "no_such_label", cof)
        assert "--break no_such_label: no symbol 'no_such_label'" in log and not halts(log), log
        log = run(exe, data, "--allow-blank", "--frames", "20", "--break", "z80:main_loop", cof)
        assert "is not a processor" in log and not halts(log), log

        # 4. names for a program without any
        log = run(exe, data, "--allow-blank", "--frames", "20", "--break", "main_loop", stripped)
        assert "no symbol 'main_loop'" in log and not halts(log), log
        log = run(exe, data, "--allow-blank", "--frames", "20", "--symbols", cof, "--break", "main_loop", stripped)
        assert halts(log) == [("68000", 0x400A, "breakpoint")], log

        # 5. the watchpoint: main_loop is `addq.l #1,frame_count`
        log = run(exe, data, "--allow-blank", "--frames", "20", "--watch", "frame_count:4:w", cof)
        assert "the watchpoint" in log, log
        h = halts(log)
        assert len(h) == 1 and h[0][0] == "68000" and h[0][2] == "watchpoint", log
        watched = h[0][1]
        log = run(exe, data, "--allow-blank", "--frames", "20", "--watch", f"${watched:06X}:1:r", cof)   # addq reads it before it writes it
        assert halts(log) == [("68000", watched, "watchpoint")], log
        log = run(exe, data, "--allow-blank", "--frames", "20", "--watch", f"${watched + 0x100:06X}:4:rw", cof)   # nobody touches that
        assert not halts(log), log
        log = run(exe, data, "--allow-blank", "--frames", "20", "--watch", "frame_count:0", cof)
        assert "--watch frame_count:0:" in log and not halts(log), log

        # 6. the saved switch is for windows: a headless run takes the flag only
        (data / "config.ini").write_text("debug.break_at_boot=1\n")
        log = run(exe, data, "--allow-blank", "--frames", "20", cof)
        assert not halts(log) and "armed before the first instruction" not in log, log
    print("PASS: dev workflow (break at boot, --break, --symbols, --watch)")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print(f"FAIL {e}")
        sys.exit(1)
