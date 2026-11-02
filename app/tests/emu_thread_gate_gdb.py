#!/usr/bin/env python3
"""G1 gate probe (#967, tracking #1079): a gdb client for the Studio gate (emu_thread_gate_check.sh, run B).

The app runs with virtualjaguar_gdb_wait=enabled, so the stub parks the 68000 at its first instruction.  This probe
connects, checks the stub answers while the core is halted (and the window keeps presenting), holds the halt for
<hold-seconds>, then continues the core.

  usage: emu_thread_gate_gdb.py <port> <hold-seconds>          exit 0 + "gdb gate: OK", else "FAIL gdb gate: ..." and exit 1
"""
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdb_threaded_halt import pack, recv_packet, cmd   # noqa: E402  (the helpers; that file's main() is behind __main__)


def fail(msg):
    print("FAIL gdb gate: " + msg)
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    port, hold = int(sys.argv[1]), float(sys.argv[2])
    sock = None
    deadline = time.monotonic() + 10.0
    while sock is None:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1.0)
        except OSError:
            if time.monotonic() > deadline:
                fail("no stub on port %d" % port)
            time.sleep(0.1)
    rx = bytearray()
    sock.sendall(b"+")

    def check(packet, ok, what):
        try:
            reply = cmd(sock, rx, packet)
        except Exception as e:   # a timeout or a closed socket is a wrong answer too
            fail("%s answered %s" % (packet, e))
        if not ok(reply):
            fail("%s answered %s (%s)" % (packet, reply, what))
        return reply

    check("qSupported:multiprocess+", lambda r: "PacketSize=" in r, "expected PacketSize=")
    check("?", lambda r: r == "S05", "gdb_wait parks the 68000 at its first instruction")
    check("g", lambda r: bool(r) and r != "E01", "registers")
    check("m0,8", lambda r: len(r) == 16 and all(c in "0123456789abcdefABCDEF" for c in r), "8 bytes of memory")
    check("Z0,1000,2", lambda r: r == "OK", "set a breakpoint")
    check("z0,1000,2", lambda r: r == "OK", "clear it")
    end = time.monotonic() + hold
    while time.monotonic() < end:
        print("holding halt", flush=True)
        time.sleep(min(1.0, max(0.0, end - time.monotonic())))
    sock.sendall(pack("c"))   # no reply is read: the next stop is the end of the run
    print("gdb gate: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
