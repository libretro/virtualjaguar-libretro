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

Two wire-level subtleties this script has to get right, both stemming
from the fact that TCP is a byte stream with no message boundaries of
its own:

  - recv_packet() must extract exactly ONE "$...#cs" packet per call and
    carry any trailing bytes over to the next call. GDBHalt() (see
    src/debug/gdbtarget.c) can and does send its own reply back-to-back
    with a reply the core's normal per-frame polling just sent for one
    of OUR requests, and the two can arrive coalesced in a single
    recv() -- observed directly: the boot-wait halt's unsolicited
    "T05thread:1;" landing immediately behind the qSupported reply in
    one chunk, corrupting the length used to check the SECOND packet's
    checksum. A version of this script that re-created a fresh, empty
    buffer on every call (rather than a buffer that persists across
    calls) could parse two coalesced packets as one and raise a spurious
    checksum-mismatch error under exactly that timing.
  - cmd() must not mistake that same unsolicited notification for the
    reply to whatever it just asked. GDBHalt() fires the instant the
    68000's very first instruction executes under the boot-wait
    one-shot, independent of what the client is doing on the wire at
    that moment, so its "T05thread:..." packet can land ahead of,
    behind, or interleaved with any of this script's own early
    request/reply round trips (qSupported, QStartNoAckMode, even the
    explicit '?' status query below). This is safe to always discard
    transparently: per the RSP payload switch in src/debug/gdbstub.c,
    '?' replies with the literal "S05" (never a 'T'-prefixed string),
    and no other command this script sends (qSupported, QStartNoAckMode,
    m/M/g/G, Z/z) can legitimately reply with anything 'T'-prefixed
    either -- only an out-of-band stop notification ever does. So any
    'T'-prefixed packet seen where this script asked something else is
    unambiguously that notification arriving out of turn, never the
    answer being waited for.
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


def recv_packet(sock, rxbuf: bytearray, timeout=10.0) -> str:
    """Extracts exactly the FIRST complete "$...#cs" packet from rxbuf,
    reading more off the socket only if rxbuf doesn't already hold one.
    Leaves any bytes after that packet in rxbuf, untouched, for the next
    call -- the start of a second, already-arrived packet (see the
    module docstring) must never be consumed as part of this one."""
    sock.settimeout(timeout)
    while True:
        dollar = rxbuf.find(b"$")
        if dollar >= 0:
            hash_at = rxbuf.find(b"#", dollar + 1)
            if hash_at >= 0 and len(rxbuf) >= hash_at + 3:
                body = bytes(rxbuf[dollar + 1 : hash_at])
                want = int(rxbuf[hash_at + 1 : hash_at + 3], 16)
                del rxbuf[: hash_at + 3]
                if cksum(body) != want:
                    raise RuntimeError(f"checksum mismatch: {body!r}")
                return body.decode()
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("stub closed the connection")
        rxbuf.extend(chunk)


def cmd(sock, rxbuf: bytearray, payload: str, timeout=10.0) -> str:
    """Sends payload and returns the reply meant for it, transparently
    discarding a leading unsolicited stop-reply notification if one is
    in the way (see the module docstring: GDBHalt()'s boot-wait
    notification can land ahead of the real reply). Every command this
    script sends via cmd() legitimately replies with something other
    than a 'T'-prefixed stop-reply, so that shape is unambiguous noise
    here -- the one command that DOES expect a 'T'-prefixed reply ('c',
    to resume) is issued directly via sendall()/recv_packet() below, not
    through cmd()."""
    sock.sendall(pack(payload))
    while True:
        reply = recv_packet(sock, rxbuf, timeout)
        if reply.startswith("T"):
            continue
        return reply


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

        rxbuf = bytearray()
        s.sendall(b"+")
        sup = cmd(s, rxbuf, "qSupported:multiprocess+")
        assert "PacketSize=" in sup

        assert cmd(s, rxbuf, "QStartNoAckMode") == "OK"

        # We should already be halted (gdb_wait fired on the very first
        # M68KInstructionHook() call, before our connection even
        # existed -- its own unsolicited stop reply went to no one).
        # Confirm via '?' rather than assuming.
        assert cmd(s, rxbuf, "?") == "S05"

        # 0x60FE = BRA.S -2 (branch to self): a single-instruction,
        # two-byte infinite loop. Written to scratch RAM while nothing
        # is running to contend for it.
        assert cmd(s, rxbuf, f"M{LOOP_ADDR:x},2:60fe") == "OK"
        assert cmd(s, rxbuf, f"m{LOOP_ADDR:x},2") == "60fe", "write didn't stick before anything ran"

        # Read the current register file, then rewrite only the PC field
        # (the last 8 of the 144 hex chars: D0-D7, A0-A7, SR, PC) so we
        # don't disturb SR (interrupt mask / supervisor bit).
        regs = cmd(s, rxbuf, "g")
        assert len(regs) == 144, f"g returned {len(regs)} chars, want 144"
        new_regs = regs[:-8] + f"{LOOP_ADDR:08x}"
        assert cmd(s, rxbuf, f"G{new_regs}") == "OK"

        assert cmd(s, rxbuf, f"Z0,{LOOP_ADDR:x},2") == "OK"

        # 'c' gets no immediate reply -- the eventual stop reply arrives
        # out of band, whenever the halted processor actually reports in.
        # This resumes from the gdb_wait halt; the CPU's very next fetch
        # is our injected instruction at LOOP_ADDR, which immediately
        # re-visits LOOP_ADDR (it's a branch to itself) and matches the
        # Z0 we just armed there -- a REAL breakpoint hit, not the
        # one-shot that produced the original gdb_wait halt.
        s.sendall(pack("c"))
        stop = recv_packet(s, rxbuf, timeout=15.0)
        assert stop.startswith("T05"), f"expected a T05 stop reply, got {stop!r}"
        assert "thread:1" in stop, f"expected thread 1 (68K) to report, got {stop!r}"

        pc_regs = cmd(s, rxbuf, "g")
        halted_pc = pc_regs[-8:]
        assert halted_pc.lower() == f"{LOOP_ADDR:08x}", (
            f"halted at PC={halted_pc}, want {LOOP_ADDR:08x} -- the breakpoint "
            f"fired at the wrong address (or didn't fire and this is a stale "
            f"reply)")

        assert cmd(s, rxbuf, f"z0,{LOOP_ADDR:x},2") == "OK"
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
