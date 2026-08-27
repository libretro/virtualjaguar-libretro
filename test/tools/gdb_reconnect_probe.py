#!/usr/bin/env python3
"""
gdb_reconnect_probe.py -- proves ack mode is per-CONNECTION, not
per-content-load (issue #652 follow-up).

RSP requires every new connection to start in ack mode: the stub must
prefix each reply with a lone '+' byte until THAT client negotiates
QStartNoAckMode. A real gdb/lldb does exactly that as part of its
qSupported handshake, so the flag gets set on essentially every real
debugging session.

The regression this guards: GDBTargetResetState() (src/debug/gdbtarget.c)
runs on client disconnect and resets breakpoints, watchpoints, halt
state, the rx buffer and Hg/Hc thread selection -- but originally left
gdbSession.noAckMode latched from the dead client. GDBSessionInit()
(src/debug/gdbstub.c) is the only other place it is cleared, and that
runs once per content load, not once per connection. So the SECOND
client to attach to one long-lived core inherited no-ack mode and its
very first qSupported reply arrived with no ack byte -- a protocol
violation the client blocks on. Observed with lldb-then-probe against
one running core.

test/tools/gdb_attach_probe.py cannot catch this: it is a single
connection, and as the first client it never sees the stale flag.

Sequence:
  1. Launch the core headlessly (test/tools/gdb_determinism_probe, same
     as gdb_breakpoint_probe.py).
  2. Client A: handshake in ack mode, negotiate QStartNoAckMode, confirm
     acks really stopped, then close -- i.e. behave like the gdb/lldb
     that leaves the flag set.
  3. Client B on the SAME core: its first reply must carry the ack byte
     again, and must keep doing so until B negotiates for itself.

Deliberately touches nothing but the ack preamble, so it needs no
halted-CPU assumptions and no particular ROM behaviour.
"""
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
# Fixed, but well below the ephemeral range and distinct from
# gdb_breakpoint_probe.py's 22349 (see the CI port-collision rule).
PORT = 22351

CORE_NAMES = {
    "darwin": "virtualjaguar_libretro.dylib",
    "linux": "virtualjaguar_libretro.so",
    "win32": "virtualjaguar_libretro.dll",
}


def cksum(payload: bytes) -> int:
    return sum(payload) & 0xFF


def pack(payload: str) -> bytes:
    b = payload.encode()
    return b"$" + b + b"#" + ("%02x" % cksum(b)).encode()


def cmd(sock, payload: str, expect_ack: bool, who: str, timeout=10.0) -> str:
    """Sends one packet and reads exactly one reply, requiring (or
    forbidding) the leading '+' ack byte."""
    sock.settimeout(timeout)
    sock.sendall(pack(payload))
    buf = b""

    if expect_ack:
        while not buf:
            chunk = sock.recv(1)
            if not chunk:
                raise RuntimeError(f"{who}: stub closed the connection waiting for ack")
            buf += chunk
        ack, buf = buf[0:1], buf[1:]
        if ack != b"+":
            raise RuntimeError(
                f"{who}: {payload!r} reply lacked its '+' ack byte (got {ack!r}) "
                f"-- ack mode leaked across connections")

    while b"#" not in buf or len(buf.split(b"#")[-1]) < 2:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError(f"{who}: stub closed the connection")
        buf += chunk

    if buf[0:1] != b"$":
        raise RuntimeError(
            f"{who}: expected a packet to start with '$', got {buf[:16]!r} "
            f"-- an unexpected ack byte before {payload!r}")

    body = buf[buf.index(b"$") + 1 : buf.rindex(b"#")]
    want = int(buf[buf.rindex(b"#") + 1 : buf.rindex(b"#") + 3], 16)
    if cksum(body) != want:
        raise RuntimeError(f"{who}: checksum mismatch: {buf!r}")
    return body.decode()


def connect(who: str):
    for _ in range(50):
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=1)
        except OSError:
            time.sleep(0.1)
    sys.exit(f"{who}: could not connect to the core's GDB stub on port {PORT}")


def main() -> int:
    core = REPO_ROOT / CORE_NAMES.get(sys.platform, "virtualjaguar_libretro.so")
    rom = REPO_ROOT / "test" / "roms" / "yarc.j64"
    tool = REPO_ROOT / "test" / "tools" / "gdb_determinism_probe"

    if not core.exists():
        sys.exit(f"Core not found at {core}. Run `make` first.")
    if not tool.exists():
        sys.exit(f"{tool} not built -- see its file header for the build command.")

    proc = subprocess.Popen(
        [str(tool), str(core), str(rom), "--frames", "100000",
         "--option", "virtualjaguar_gdb_stub=enabled",
         "--option", f"virtualjaguar_gdb_port={PORT}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    try:
        # --- Client A: a normal gdb/lldb, which turns no-ack mode on. ---
        a = connect("client A")
        a.sendall(b"+")
        sup = cmd(a, "qSupported:multiprocess+", True, "client A")
        assert "QStartNoAckMode+" in sup, f"client A: qSupported lacked QStartNoAckMode+: {sup!r}"

        # The reply to QStartNoAckMode itself is still acked (it was sent
        # while ack mode was live); nothing after it is.
        assert cmd(a, "QStartNoAckMode", True, "client A") == "OK"
        assert cmd(a, "?", False, "client A") == "S05"

        a.close()

        # The core notices the drop on its next GDBSockRecv() (once per
        # frame via GDBTargetServicePoll), and only then can accept B --
        # GDBSockPoll() refuses to accept while a client fd is still
        # valid, so B's packets can never be read before the reset runs.
        # This sleep just keeps the failure mode a timeout rather than a
        # confusing hang if that ever changes.
        time.sleep(0.5)

        # --- Client B: a fresh connection MUST be back in ack mode. ---
        b = connect("client B")
        b.sendall(b"+")
        sup_b = cmd(b, "qSupported:multiprocess+", True, "client B")
        assert "PacketSize=" in sup_b, f"client B: qSupported lacked PacketSize: {sup_b!r}"

        # Still in ack mode for every subsequent reply, until B asks.
        assert cmd(b, "?", True, "client B") == "S05"
        assert cmd(b, "QStartNoAckMode", True, "client B") == "OK"
        assert cmd(b, "?", False, "client B") == "S05"

        b.close()

        print("PASS: a second client on the same core starts in ack mode "
              "again -- QStartNoAckMode from the first client does not "
              "leak past its disconnect")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
