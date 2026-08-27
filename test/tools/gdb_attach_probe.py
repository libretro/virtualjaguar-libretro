#!/usr/bin/env python3
"""
Minimal RSP client: attach to the core's GDB stub and validate the wire
protocol a real `m68k-elf-gdb` actually depends on.

Phase 1's version of this script passed without ever looking at the
low-level '+'/'-' acknowledgement byte, which is exactly the gap
docs/gdb-stub-design.md's Testing section calls out: a real gdb stalls on
its very first qSupported without it. This version reads and verifies
that byte explicitly, before every reply, until QStartNoAckMode is
negotiated -- and then verifies acks STOP arriving afterward, including
the off-by-one case (the OK reply to QStartNoAckMode itself is still
acked; nothing sent after it is).

Assumes something else has already launched a core with
virtualjaguar_gdb_stub=enabled on the given port (see
test/tools/gdb_determinism_probe.c for a way to do that headlessly, or
attach to a real running RetroArch/session).
"""
import socket, sys

def cksum(payload: bytes) -> int:
    return sum(payload) & 0xFF

def pack(payload: str) -> bytes:
    b = payload.encode()
    return b"$" + b + b"#" + ("%02x" % cksum(b)).encode()

def recv_reply(sock, expect_ack: bool) -> str:
    """Reads exactly one ack byte (if expect_ack) followed by one
    "$...#cs" packet. Raises if the ack is missing/wrong when expected,
    or if the packet checksum doesn't validate."""
    buf = b""

    if expect_ack:
        while len(buf) < 1:
            chunk = sock.recv(1)
            if not chunk:
                raise RuntimeError("stub closed the connection waiting for ack")
            buf += chunk
        ack, buf = buf[0:1], buf[1:]
        if ack != b"+":
            raise RuntimeError(f"expected '+' ack byte, got {ack!r}")

    while b"#" not in buf or len(buf.split(b"#")[-1]) < 2:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("stub closed the connection")
        buf += chunk

    if buf and buf[0:1] != b"$":
        raise RuntimeError(f"expected packet to start with '$', got {buf[:16]!r}")

    body = buf[buf.index(b"$") + 1 : buf.rindex(b"#")]
    want = int(buf[buf.rindex(b"#") + 1 : buf.rindex(b"#") + 3], 16)
    if cksum(body) != want:
        raise RuntimeError("checksum mismatch from stub")
    return body.decode()

def send_and_check(sock, payload: str, expect_ack: bool) -> str:
    sock.sendall(pack(payload))
    return recv_reply(sock, expect_ack)

def main(port: int) -> int:
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"+")

    # --- ack mode: every reply must be preceded by a lone '+' byte. ---
    sup = send_and_check(s, "qSupported:multiprocess+", expect_ack=True)
    assert "PacketSize=" in sup, f"qSupported lacked PacketSize: {sup!r}"

    assert send_and_check(s, "?", expect_ack=True) == "S05"

    regs = send_and_check(s, "g", expect_ack=True)
    assert len(regs) == 144, f"g returned {len(regs)} chars, want 144"
    int(regs, 16)                      # must be pure hex

    mem = send_and_check(s, "m0,10", expect_ack=True)
    assert len(mem) == 32, f"m0,10 returned {len(mem)} chars, want 32"
    int(mem, 16)

    # A wild address must be refused, not mirrored.
    assert send_and_check(s, "mffffffff,4", expect_ack=True) == "E01"

    # --- the no-ack transition, including its off-by-one: the OK for
    # QStartNoAckMode is STILL acked (it was sent while ack mode was
    # still active); nothing received after it is. ---
    assert send_and_check(s, "QStartNoAckMode", expect_ack=True) == "OK"

    # From here on, no ack byte should precede any reply.
    assert send_and_check(s, "?", expect_ack=False) == "S05"
    regs2 = send_and_check(s, "g", expect_ack=False)
    assert regs2 == regs, "registers changed between reads with nothing executing"

    # Z0/z0 (software breakpoint) round-trip -- protocol-level only here;
    # test/tools/gdb_determinism_probe.c and the C unit tests
    # (test/tools/test_gdbstub_proto.c) cover an actual breakpoint firing
    # and the armed-flag gate's cost, respectively.
    assert send_and_check(s, "Z0,1000,2", expect_ack=False) == "OK"
    assert send_and_check(s, "z0,1000,2", expect_ack=False) == "OK"

    s.close()
    print("PASS: attach, ack byte (every reply), no-ack transition "
          "(including the QStartNoAckMode-reply-is-still-acked case), "
          "halt reason, registers, memory, bounds refusal, breakpoint insert/remove")
    return 0

if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 2345))
