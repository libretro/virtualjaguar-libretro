#!/usr/bin/env python3
"""
gdb_threaded_halt.py -- a gdb client against the standalone app halted at boot (virtualjaguar_gdb_wait) with the
emulation thread on (#967 phase 3b).

  1. A headless run with the stub and gdb_wait enabled parks at boot: the core's first 68000 instruction halts, and the
     app's silent debugger host sees it (the "[debug] halt:" line).  A client that connects later asks `?` and gets S05.
  2. Memory is served while halted (`m0,10`), and the process is still alive half a second later (the headless loop
     waits for gdb; it does not release a halt gdb caused).
  3. `c` ends the halt: the run finishes and exits 0 with the threaded OK line.
  4. A second run, same arguments: the client sees the halt, and SIGTERM arrives while gdb still holds it.  The signal
     handler (headless only) sets the quit flag, the pump's abortHalt reads it, and the app exits 0 within 3 s with the
     "quit requested" line and neither of the 3 s warnings (a thread that did not stop, a call that was not answered).

usage: gdb_threaded_halt.py <virtualjaguar> <rom> [on|off [<ra fixture dir>]]
The optional third argument is the --emu-thread mode (default on); "off" runs the same four steps inline (the headless loop
then syncs the halt itself after each frame).  A fourth argument (the RetroAchievements mock fixture dir) switches to the
hardcore variant: the stub comes from a saved setting, the client continues, a mock sign-in at frame 40 turns hardcore on,
and the app must reload the game to close the stub -- which it can only do once the finished halt no longer reads as live
(the headless inline loop syncs the halt after each frame).  That run has no SIGTERM step.
Every wait here has its own timeout; a hung child is killed in the finally blocks.
"""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time


def cksum(payload: bytes) -> int:
    return sum(payload) & 0xFF


def pack(payload: str) -> bytes:
    b = payload.encode()
    return b"$" + b + b"#" + ("%02x" % cksum(b)).encode()


def recv_packet(sock, rxbuf: bytearray, timeout=10.0) -> str:
    """Exactly the FIRST complete "$...#cs" packet in rxbuf (reading more only if there is none); the bytes after it stay
    in rxbuf for the next call (two packets can arrive in one recv())."""
    deadline = time.monotonic() + timeout
    while True:
        dollar = rxbuf.find(b"$")
        if dollar >= 0:
            hash_at = rxbuf.find(b"#", dollar + 1)
            if hash_at >= 0 and len(rxbuf) >= hash_at + 3:
                body = bytes(rxbuf[dollar + 1:hash_at])
                want = int(rxbuf[hash_at + 1:hash_at + 3], 16)
                del rxbuf[:hash_at + 3]
                if cksum(body) != want:
                    raise RuntimeError(f"checksum mismatch: {body!r}")
                return body.decode()
        left = deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError("no packet within %.1f s" % timeout)
        sock.settimeout(left)
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("stub closed the connection")
        rxbuf.extend(chunk)


def cmd(sock, rxbuf: bytearray, payload: str, timeout=10.0) -> str:
    """Sends payload and returns the reply meant for it, discarding an unsolicited T05 stop notification in the way."""
    sock.sendall(pack(payload))
    while True:
        reply = recv_packet(sock, rxbuf, timeout)
        if reply.startswith("T"):
            continue
        return reply


class Child:
    """The app, with a reader thread per stream collecting lines."""

    def __init__(self, app, rom, port, mode, ra_fix=None):
        self.tmp = None
        if ra_fix:
            # Hardcore locks the stub key, so the stub is a SAVED setting here (an --option on a locked key is an error).
            self.tmp = tempfile.TemporaryDirectory(prefix="vj-gdb-ra-")
            with open(os.path.join(self.tmp.name, "config.ini"), "w") as f:
                f.write(f"opt.virtualjaguar_gdb_stub=enabled\nopt.virtualjaguar_gdb_port={port}\n")
            args = [app, "--headless", "--emu-thread", mode, "--frames", "300", "--verbose", "--data-dir", self.tmp.name,
                    "--option", "virtualjaguar_gdb_wait=enabled",
                    "--ra-mock", ra_fix, "--ra-hardcore", "--ra-login-at", "40", "--ra-expect-reset", "1", rom]
        else:
            args = [app, "--headless", "--emu-thread", mode, "--frames", "300",
                    "--option", "virtualjaguar_gdb_stub=enabled",
                    "--option", "virtualjaguar_gdb_wait=enabled",
                    "--option", f"virtualjaguar_gdb_port={port}", rom]
        self.out, self.err = [], []
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
        self.threads = [threading.Thread(target=self._pump, args=(self.proc.stdout, self.out), daemon=True),
                        threading.Thread(target=self._pump, args=(self.proc.stderr, self.err), daemon=True)]
        for t in self.threads:
            t.start()

    @staticmethod
    def _pump(stream, sink):
        for line in stream:
            sink.append(line)

    def text(self, which):
        return "".join(self.out if which == "out" else self.err)

    def wait_for(self, which, needle, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if needle in self.text(which):
                return True
            time.sleep(0.02)
        return needle in self.text(which)

    def finish(self):
        """Kill a child that is still alive; let the readers drain."""
        if self.proc.poll() is None:
            self.proc.kill()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        for t in self.threads:
            t.join(timeout=2)
        if self.tmp:
            self.tmp.cleanup()


def stderr_problem(child, extra=()):
    """The child's stderr is not ctest's: look for what ctest's FAIL patterns would have caught there."""
    for needle in ("owner violation", "ThreadSanitizer") + tuple(extra):
        if needle in child.text("err"):
            return f"stderr has '{needle}'"
    return None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def connect(child, port, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if child.proc.poll() is not None:
            return None
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1)
        except OSError:
            time.sleep(0.05)
    return None


def fail(step, why):
    print(f"FAIL: {step}: {why}")
    return 1


def halted_client(app, rom, mode, ra_fix=None):
    """Launch, connect, qSupported, `?` -> S05.  Returns (child, sock, rxbuf, None) or (child, None, None, failure text)."""
    port = free_port()
    child = Child(app, rom, port, mode, ra_fix)
    sock = connect(child, port)
    if sock is None:
        return child, None, None, "could not connect to the stub within 5 s\n" + child.text("err")[-600:]
    rxbuf = bytearray()
    try:
        sock.sendall(b"+")
        sup = cmd(sock, rxbuf, "qSupported:multiprocess+")
        if "PacketSize=" not in sup:
            return child, sock, rxbuf, f"qSupported answered {sup!r}"
        status = cmd(sock, rxbuf, "?", timeout=10.0)
        if status != "S05":
            return child, sock, rxbuf, f"'?' answered {status!r}, want 'S05'"
    except (TimeoutError, RuntimeError, OSError) as e:
        return child, sock, rxbuf, f"{type(e).__name__}: {e}"
    return child, sock, rxbuf, None


def run(app, rom, mode, ra_fix=None):
    ok_line = "headless: OK (1 load(s), emulation thread)" if mode == "on" else "headless: OK"
    # ---- 1-5: a halted boot, memory, continue ------------------------------------------------------------
    child, sock, rxbuf, bad = halted_client(app, rom, mode, ra_fix)
    try:
        if bad:
            return fail("connect and '?'", bad)
        if not child.wait_for("err", "[debug] gdb host attached (silent)", 2.0):
            return fail("silent host", "stderr never showed '[debug] gdb host attached (silent)'\n" + child.text("err")[-600:])
        if not child.wait_for("err", "[debug] halt: 68000 at $", 2.0):
            return fail("halt line", "stderr never showed '[debug] halt: 68000 at $'\n" + child.text("err")[-600:])
        mem = cmd(sock, rxbuf, "m0,10")
        if len(mem) != 32 or any(c not in "0123456789abcdefABCDEF" for c in mem):
            return fail("memory while halted", f"m0,10 answered {mem!r}, want 32 hex chars")
        time.sleep(0.5)
        if child.proc.poll() is not None:
            return fail("halt held", f"the app exited ({child.proc.returncode}) while gdb held the halt")
        sock.sendall(pack("c"))
        try:
            rc = child.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            return fail("continue", "the app did not exit within 30 s of `c`")
        for t in child.threads:
            t.join(timeout=2)
        if rc != 0:
            return fail("continue", f"exit code {rc}\n" + child.text("err")[-600:])
        if ok_line not in child.text("out"):
            return fail("continue", f"stdout lacks {ok_line!r}: " + repr(child.text("out")[-300:]))
        noisy = stderr_problem(child)
        if noisy:
            return fail("continue", noisy)
        if ra_fix and "[ach] hardcore on: reloading the game to close the GDB stub" not in child.text("err"):
            return fail("hardcore reload", "stderr lacks the hardcore reload line (a stale halt mirror blocks it)\n" + child.text("err")[-600:])
    finally:
        if sock:
            sock.close()
        child.finish()

    if ra_fix:
        print("PASS: gdb halt on the UI thread (inline), continue, then the hardcore reload once the halt is over")
        return 0

    # ---- 6: SIGTERM while gdb holds the halt -------------------------------------------------------------
    if sys.platform == "win32":
        print("PASS: gdb halt on the " + ("emulation thread" if mode == "on" else "UI thread (inline)") +
              ", memory while halted, continue (step 6, SIGTERM mid-halt, skipped on "
              "Windows: SIGTERM there is a hard kill)")
        return 0
    child, sock, rxbuf, bad = halted_client(app, rom, mode)
    try:
        if bad:
            return fail("second run: connect and '?'", bad)
        child.proc.send_signal(signal.SIGTERM)
        t0 = time.monotonic()
        try:
            rc = child.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            return fail("SIGTERM mid-halt", "the app was still running 3 s after SIGTERM")
        for t in child.threads:
            t.join(timeout=2)
        if rc != 0:
            return fail("SIGTERM mid-halt", f"exit code {rc} after {time.monotonic() - t0:.2f} s\n" + child.text("err")[-600:])
        if "headless: quit requested, exiting cleanly" not in child.text("out"):
            return fail("SIGTERM mid-halt", "stdout lacks 'headless: quit requested, exiting cleanly': " + repr(child.text("out")[-300:]))
        noisy = stderr_problem(child, ("did not stop within 3 s", "did not answer within 3 s"))
        if noisy:
            return fail("SIGTERM mid-halt", noisy)
    finally:
        if sock:
            sock.close()
        child.finish()

    print("PASS: gdb halt on the " + ("emulation thread" if mode == "on" else "UI thread (inline)") +
          ", memory while halted, continue, quit mid-halt")
    return 0


def main():
    if len(sys.argv) not in (3, 4, 5) or (len(sys.argv) >= 4 and sys.argv[3] not in ("on", "off")):
        print("usage: gdb_threaded_halt.py <virtualjaguar> <rom> [on|off [<ra fixture dir>]]")
        return 2
    if not os.path.exists(sys.argv[1]) or not os.path.exists(sys.argv[2]):
        return fail("arguments", "the app or the rom does not exist")
    return run(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) >= 4 else "on", sys.argv[4] if len(sys.argv) == 5 else None)


if __name__ == "__main__":
    # A watchdog: this script as a whole never outlives 100 s (ctest's TIMEOUT is 120).
    wd = threading.Timer(100.0, lambda: (print("FAIL: watchdog: the script ran over 100 s"), sys.stdout.flush(), os._exit(1)))
    wd.daemon = True
    wd.start()
    sys.exit(main())
