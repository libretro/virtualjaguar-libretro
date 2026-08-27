#!/usr/bin/env python3
"""Minimal RSP client: attach to the core's GDB stub and validate Phase 1."""
import socket, sys

def cksum(payload: bytes) -> int:
    return sum(payload) & 0xFF

def pack(payload: str) -> bytes:
    b = payload.encode()
    return b"$" + b + b"#" + ("%02x" % cksum(b)).encode()

def recv_packet(sock) -> str:
    buf = b""
    while b"#" not in buf or len(buf.split(b"#")[-1]) < 2:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("stub closed the connection")
        buf += chunk
    body = buf[buf.index(b"$") + 1 : buf.rindex(b"#")]
    want = int(buf[buf.rindex(b"#") + 1 : buf.rindex(b"#") + 3], 16)
    if cksum(body) != want:
        raise RuntimeError("checksum mismatch from stub")
    return body.decode()

def main(port: int) -> int:
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"+")

    s.sendall(pack("qSupported:multiprocess+"))
    sup = recv_packet(s)
    assert "PacketSize=" in sup, f"qSupported lacked PacketSize: {sup!r}"

    s.sendall(pack("?"))
    assert recv_packet(s) == "S05"

    s.sendall(pack("g"))
    regs = recv_packet(s)
    assert len(regs) == 144, f"g returned {len(regs)} chars, want 144"
    int(regs, 16)                      # must be pure hex

    s.sendall(pack("m0,10"))
    mem = recv_packet(s)
    assert len(mem) == 32, f"m0,10 returned {len(mem)} chars, want 32"
    int(mem, 16)

    # A wild address must be refused, not mirrored.
    s.sendall(pack("mffffffff,4"))
    assert recv_packet(s) == "E01"

    s.close()
    print("PASS: attach, qSupported, halt reason, registers, memory, bounds refusal")
    return 0

if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 2345))
