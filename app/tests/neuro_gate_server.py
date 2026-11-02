#!/usr/bin/env python3
"""G1 gate Neuro server (#967, tracking #1079): a minimal Neuro API server (RFC 6455 over a plain socket, stdlib only).

Binds 127.0.0.1:0 and writes the port to <result-file>.port, accepts the app's one connection, waits for `startup` and
`actions/register`, then sends 10 `press_buttons` actions one second apart and counts the successful `action/result`
answers.  Writes <result-file>: "ok presses=<n>" (n >= 5) or "FAIL presses=<n>" plus the exception text.
Randy (the SDK's test bot) is not used: the gate must not fetch code.

  usage: neuro_gate_server.py <result-file>
"""
import base64
import hashlib
import json
import socket
import struct
import sys
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class Closed(Exception):
    pass


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise Closed("connection closed")
        buf += chunk
    return buf


def read_frame(sock):
    """One client frame -> (opcode, payload).  Client frames are masked."""
    b0, b1 = recv_exact(sock, 2)
    op, ln = b0 & 0x0F, b1 & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", recv_exact(sock, 2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", recv_exact(sock, 8))[0]
    mask = recv_exact(sock, 4) if b1 & 0x80 else b""
    data = recv_exact(sock, ln)
    if mask:
        data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))
    return op, data


def send_frame(sock, op, payload=b""):
    n = len(payload)
    head = bytes([0x80 | op])
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + struct.pack(">H", n)
    else:
        head += bytes([127]) + struct.pack(">Q", n)
    sock.sendall(head + payload)   # server frames are unmasked


def next_message(sock, deadline):
    """The next JSON text message before `deadline` (answers pings on the way); None on timeout."""
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            return None
        sock.settimeout(left)
        try:
            op, data = read_frame(sock)
        except socket.timeout:
            return None
        if op == 1:
            return json.loads(data.decode())
        if op == 9:
            send_frame(sock, 0xA, data)
        elif op == 8:
            raise Closed("the app closed the connection")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    result = sys.argv[1]
    presses, err = 0, None
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    with open(result + ".port", "w") as fh:
        fh.write(str(srv.getsockname()[1]))
    try:
        srv.settimeout(60)
        sock, _ = srv.accept()
        sock.settimeout(10)
        req = b""
        while b"\r\n\r\n" not in req:
            chunk = sock.recv(4096)
            if not chunk:
                raise Closed("closed during the handshake")
            req += chunk
        key = None
        for line in req.decode("latin-1").split("\r\n"):
            if line.lower().startswith("sec-websocket-key:"):
                key = line.split(":", 1)[1].strip()
        if not key:
            raise RuntimeError("no Sec-WebSocket-Key in the handshake")
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        sock.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: %s\r\n\r\n" % accept).encode())
        seen = set()
        deadline = time.monotonic() + 30
        while not {"startup", "actions/register"} <= seen:
            m = next_message(sock, deadline)
            if m is None:
                raise RuntimeError("no startup + actions/register within 30 s (got %s)" % sorted(seen))
            seen.add(m.get("command"))
        for i in range(1, 11):
            aid = "g%d" % i
            act = {"command": "action", "data": {"id": aid, "name": "press_buttons",
                                                  "data": json.dumps({"buttons": ["a"], "frames": 3})}}
            send_frame(sock, 1, json.dumps(act).encode())
            until = time.monotonic() + 1.0
            while True:
                m = next_message(sock, until)
                if m is None:
                    break
                d = m.get("data") or {}
                if m.get("command") == "action/result" and d.get("id") == aid:
                    if d.get("success") is True:
                        presses += 1
                    break
            rest = until - time.monotonic()
            if rest > 0:
                time.sleep(rest)   # once a second
        send_frame(sock, 8, struct.pack(">H", 1000))
    except (Closed, ConnectionResetError, BrokenPipeError) as e:
        if presses < 5:   # the app may exit (a faster display ends its iteration-driven run sooner) once 5+ presses were answered
            err = "%s: %s" % (type(e).__name__, e)
    except Exception as e:   # noqa: BLE001  (every failure goes into the result file)
        err = "%s: %s" % (type(e).__name__, e)
    ok = err is None and presses >= 5
    with open(result, "w") as fh:
        fh.write(("ok presses=%d\n" if ok else "FAIL presses=%d\n") % presses)
        if err:
            fh.write(err + "\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
