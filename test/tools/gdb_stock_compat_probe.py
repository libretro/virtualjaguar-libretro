#!/usr/bin/env python3
"""
gdb_stock_compat_probe.py -- the wire-level half of "stock gdb works out of
the box" (issue #936), against a LIVE core.

What a stock gdb needs that the engine unit tests (test_gdbstub_proto)
cannot prove, because they use fake targets:

  1. qXfer:features:read:target.xml for the real 68K ops returns a document
     (it used to be an empty packet, which aborts gdb with "conflicting
     enabled responses"), declaring m68k and 18 registers.
  2. qfThreadInfo reports ONLY the 68K (gdb applies one tdesc to every
     thread it knows).
  3. Z2 (write watchpoint) is accepted (it was always refused by a stale
     #ifndef ALPINE_FUNCTIONS gate) and a second one at a different
     address is still refused (single slot).
  4. `monitor trace 1024` and `monitor regs gpu` arrive complete, as 'O'
     console packets followed by OK (they were cut at 512 bytes).
  5. D (detach) replies OK and the SAME core keeps running and accepts a
     new client (still listening, new connection starts in ack mode).
  6. vKill likewise, and `k` closes the connection without a reply.

Launches the core headlessly the same way gdb_reconnect_probe.py does
(test/tools/gdb_determinism_probe; see its header for the build line) and
needs only a public ROM. Run from the repo root after `make`.
"""
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
# Distinct from the other gdb probes (22349 / 22351); see the CI
# port-collision rule: never an ephemeral-range port.
PORT = 22353

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


class Client:
    def __init__(self, who):
        self.who = who
        self.buf = b""
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        else:
            sys.exit(f"{who}: could not connect on port {PORT}")
        self.sock.settimeout(15)
        self.sock.sendall(b"+")
        self.first_reply = True

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise EOFError(f"{self.who}: stub closed the connection")
        self.buf += chunk

    def _packet(self):
        """Next '$...#cs' packet (acking it), skipping stray '+' bytes."""
        while True:
            while self.buf[:1] == b"+":
                self.buf = self.buf[1:]
            if self.buf[:1] == b"$":
                end = self.buf.find(b"#")
                if end >= 0 and len(self.buf) >= end + 3:
                    body = self.buf[1:end]
                    want = int(self.buf[end + 1:end + 3], 16)
                    assert cksum(body) == want, f"{self.who}: bad checksum"
                    self.buf = self.buf[end + 3:]
                    self.sock.sendall(b"+")
                    return body.decode()
            self._fill()

    def send(self, payload):
        self.sock.sendall(pack(payload))

    def cmd(self, payload):
        self.send(payload)
        return self._packet()

    def monitor(self, text):
        """qRcmd: returns (console text from O packets, final reply)."""
        self.send("qRcmd," + text.encode().hex())
        out = b""
        while True:
            pkt = self._packet()
            if pkt.startswith("O") and pkt != "OK":
                out += bytes.fromhex(pkt[1:])
                continue
            return out.decode(), pkt

    def closed_by_peer(self, timeout=5):
        self.sock.settimeout(timeout)
        try:
            while True:
                if not self.sock.recv(4096):
                    return True
        except socket.timeout:
            return False
        except OSError:
            return True


def read_xml(c):
    xml, off = "", 0
    while True:
        r = c.cmd(f"qXfer:features:read:target.xml:{off:x},fff")
        assert r[0] in "ml", f"qXfer reply was {r[:20]!r} (empty = the #936 bug)"
        xml += r[1:]
        off += len(r) - 1
        if r[0] == "l":
            return xml


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
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        a = Client("A")
        sup = a.cmd("qSupported:multiprocess+")
        assert "qXfer:features:read+" in sup, sup

        # 1. target.xml
        xml = read_xml(a)
        assert "<architecture>m68k</architecture>" in xml, xml
        assert xml.count("<reg ") == 18, xml
        assert 'name="pc"' in xml and 'name="ps"' in xml, xml

        # 2. only the 68K is enumerated
        assert a.cmd("qfThreadInfo") == "m1"
        assert a.cmd("qsThreadInfo") == "l"
        assert len(a.cmd("g")) == 144          # 18 x 32-bit

        # 3. watchpoints are accepted; the single slot still holds
        assert a.cmd("Z2,f0210c,4") == "OK"
        assert a.cmd("Z2,f02200,4") == "E01"
        assert a.cmd("z2,f0210c,4") == "OK"

        # 4. long monitor output arrives whole
        text, fin = a.monitor("trace 1024")
        assert fin == "OK", fin
        lines = [l for l in text.splitlines() if l.startswith("$")]
        assert len(lines) == 1024, f"trace delivered {len(lines)} lines"
        assert "newest" in text.splitlines()[0], text.splitlines()[0]
        text, fin = a.monitor("regs gpu")
        assert fin == "OK", fin
        assert text.count("=$") == 32 + 3, text   # r0..r31 + PC/FLAGS/CTRL
        assert "r31=$" in text and text.endswith("\n"), text[-40:]
        text, fin = a.monitor("trace 5")
        assert len([l for l in text.splitlines() if l.startswith("$")]) == 5

        # 5. D: OK, then the core is still up and accepts a new client
        assert a.cmd("D") == "OK"
        assert a.closed_by_peer(), "stub did not close the client after D"
        b = Client("B")
        assert "PacketSize=" in b.cmd("qSupported:multiprocess+")
        assert len(b.cmd("g")) == 144
        # 6. vKill
        assert b.cmd("vKill;1") == "OK"
        assert b.closed_by_peer(), "stub did not close the client after vKill"
        c = Client("C")
        assert c.cmd("?") == "S05"
        c.send("k")                          # no reply, just a close
        assert c.closed_by_peer(), "stub did not close the client after k"
        d = Client("D")
        assert d.cmd("?") == "S05"
        d.sock.close()

        assert proc.poll() is None, "the emulator exited -- detach/kill must not stop it"
        print("PASS: target.xml, 68K-only thread list, Z2, long monitor output, "
              "D/vKill/k keep the emulator running and reconnectable")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
