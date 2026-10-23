#!/usr/bin/env python3
"""
with_free_port.py -- run a command with a TCP port the OS just handed out (#967 phase 3b).

usage: with_free_port.py [--config-ini <data dir>] -- <command> [args...]

Every "{PORT}" in the command's arguments becomes a free port: bind 127.0.0.1:0, read the port back, close.  Never a fixed
number: tests that run in parallel must not collide, and a fixed port can be taken or excluded on a shared runner.
(The hosted-Windows "failed to open port" was NOT a port problem: the app never started Winsock; see platform::initNetworking.)  --config-ini writes <data dir>/config.ini with the GDB stub enabled on that same port (a SAVED
setting, for the tests where the key is locked and an --option would be an error), rewritten on every run.

The command's stdout and stderr are inherited, so ctest's PASS / FAIL regular expressions see the app's output unchanged,
and its exit status is returned.  A SIGTERM / SIGINT (ctest's timeout) ends the child before this script exits.
"""
import os
import signal
import socket
import subprocess
import sys


def free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def main(argv) -> int:
    args = argv[1:]
    data_dir = None
    if args[:1] == ["--config-ini"] and len(args) >= 2:
        data_dir, args = args[1], args[2:]
    if args[:1] != ["--"] or len(args) < 2:
        sys.stderr.write(__doc__)
        return 2
    cmd = args[1:]
    port = free_port()
    if data_dir is not None:
        os.makedirs(data_dir, exist_ok=True)
        with open(os.path.join(data_dir, "config.ini"), "w") as f:
            f.write(f"opt.virtualjaguar_gdb_stub=enabled\nopt.virtualjaguar_gdb_port={port}\n")
    cmd = [a.replace("{PORT}", str(port)) for a in cmd]
    proc = subprocess.Popen(cmd)

    def stop(signum, frame):
        proc.kill()

    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, stop)
    return proc.wait()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
