#!/usr/bin/env python3
"""
gdb_breakpoint_probe.py -- end-to-end proof that a Z0 breakpoint actually
halts execution at the armed address (issue #652, Phase 2).

test/tools/test_gdbstub_proto.c proves the Z0/z0 PROTOCOL (insert/remove
call the target ops, replies are correct) with a fake target. It cannot
prove a real breakpoint actually STOPS the emulated 68000 at the right
PC, because that requires the machine to actually be running. This
script does that, deterministically, without depending on any
particular ROM's boot behaviour:

  1. Launch the core headlessly (test/tools/gdb_determinism_probe as a
     free-running host process -- it already knows how to open the GDB
     port from a core option and just run frames).
  2. Attach, negotiate QStartNoAckMode.
  3. Write two bytes forming a single 68K instruction -- BRA.S * (opcode
     0x60FE, "branch to self", i.e. a tight infinite loop) -- into RAM
     at a scratch address, via the GDB 'M' (write memory) packet.
  4. Redirect the 68K's PC to that address via 'G' (write registers).
  5. Arm a Z0 breakpoint at that exact address.
  6. Continue.
  7. Because nothing has "used up" the PC value we just poked (a
     register write is an out-of-band state change, not a hook call),
     the very next M68KInstructionHook() invocation is for that address
     and matches the just-armed breakpoint immediately -- the stop reply
     must name thread 1 (68K) and 'g' must show PC still at that exact
     address.

This is deterministic and ROM-independent: steps 3-4 make the "what PC
will the breakpoint hit at" question something WE decide, not something
that depends on knowing a specific ROM's code layout.
"""
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
LOOP_ADDR = 0x1000
PORT = 22349

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


def recv_packet(sock, timeout=10.0) -> str:
    sock.settimeout(timeout)
    buf = b""
    while b"#" not in buf or len(buf.split(b"#")[-1]) < 2:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("stub closed the connection")
        buf += chunk
    body = buf[buf.index(b"$") + 1 : buf.rindex(b"#")]
    want = int(buf[buf.rindex(b"#") + 1 : buf.rindex(b"#") + 3], 16)
    if cksum(body) != want:
        raise RuntimeError(f"checksum mismatch: {buf!r}")
    return body.decode()


def cmd(sock, payload: str, timeout=10.0) -> str:
    sock.sendall(pack(payload))
    return recv_packet(sock, timeout)


def main() -> int:
    core = REPO_ROOT / CORE_NAMES.get(sys.platform, "virtualjaguar_libretro.so")
    rom = REPO_ROOT / "test" / "roms" / "yarc.j64"
    tool = REPO_ROOT / "test" / "tools" / "gdb_determinism_probe"

    if not core.exists():
        sys.exit(f"Core not found at {core}. Run `make` first.")
    if not tool.exists():
        sys.exit(f"{tool} not built -- see its file header for the build command.")

    # virtualjaguar_gdb_wait halts before the 68000's very first
    # instruction. This isn't just convenient -- it's load-bearing for
    # this test's determinism: an already-running ROM's own boot code
    # (stack usage, RAM clears, blitter/OP destinations) can and does
    # overwrite an arbitrarily-chosen scratch address within a single
    # frame, racing our injected instruction before the CPU ever fetches
    # it (observed directly while developing this script: a plain
    # `M`-write to 0x1000 without gdb_wait read back as the ROM's own
    # data moments later). Halting before anything has executed means
    # nothing else can be writing to RAM yet.
    proc = subprocess.Popen(
        [str(tool), str(core), str(rom), "--frames", "100000",
         "--option", "virtualjaguar_gdb_stub=enabled",
         "--option", "virtualjaguar_gdb_wait=enabled",
         "--option", f"virtualjaguar_gdb_port={PORT}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    try:
        s = None
        for _ in range(50):
            try:
                s = socket.create_connection(("127.0.0.1", PORT), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        if s is None:
            sys.exit("could not connect to the core's GDB stub")

        s.sendall(b"+")
        sup = cmd(s, "qSupported:multiprocess+")
        assert "PacketSize=" in sup

        assert cmd(s, "QStartNoAckMode") == "OK"

        # We should already be halted (gdb_wait fired on the very first
        # M68KInstructionHook() call, before our connection even
        # existed -- its own unsolicited stop reply went to no one).
        # Confirm via '?' rather than assuming.
        assert cmd(s, "?") == "S05"

        # 0x60FE = BRA.S -2 (branch to self): a single-instruction,
        # two-byte infinite loop. Written to scratch RAM while nothing
        # is running to contend for it.
        assert cmd(s, f"M{LOOP_ADDR:x},2:60fe") == "OK"
        assert cmd(s, f"m{LOOP_ADDR:x},2") == "60fe", "write didn't stick before anything ran"

        # Read the current register file, then rewrite only the PC field
        # (the last 8 of the 144 hex chars: D0-D7, A0-A7, SR, PC) so we
        # don't disturb SR (interrupt mask / supervisor bit).
        regs = cmd(s, "g")
        assert len(regs) == 144, f"g returned {len(regs)} chars, want 144"
        new_regs = regs[:-8] + f"{LOOP_ADDR:08x}"
        assert cmd(s, f"G{new_regs}") == "OK"

        assert cmd(s, f"Z0,{LOOP_ADDR:x},2") == "OK"

        # 'c' gets no immediate reply -- the eventual stop reply arrives
        # out of band, whenever the halted processor actually reports in.
        # This resumes from the gdb_wait halt; the CPU's very next fetch
        # is our injected instruction at LOOP_ADDR, which immediately
        # re-visits LOOP_ADDR (it's a branch to itself) and matches the
        # Z0 we just armed there -- a REAL breakpoint hit, not the
        # one-shot that produced the original gdb_wait halt.
        s.sendall(pack("c"))
        stop = recv_packet(s, timeout=15.0)
        assert stop.startswith("T05"), f"expected a T05 stop reply, got {stop!r}"
        assert "thread:1" in stop, f"expected thread 1 (68K) to report, got {stop!r}"

        pc_regs = cmd(s, "g")
        halted_pc = pc_regs[-8:]
        assert halted_pc.lower() == f"{LOOP_ADDR:08x}", (
            f"halted at PC={halted_pc}, want {LOOP_ADDR:08x} -- the breakpoint "
            f"fired at the wrong address (or didn't fire and this is a stale "
            f"reply)")

        assert cmd(s, f"z0,{LOOP_ADDR:x},2") == "OK"
        s.close()

        print("PASS: injected a real 68K instruction, redirected PC to it, "
              f"armed Z0 at ${LOOP_ADDR:04x}, continued, and the stub halted "
              f"there and reported it -- confirmed via a fresh register read "
              f"(PC=${halted_pc}) after the stop reply named thread 1.")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
