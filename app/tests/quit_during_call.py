#!/usr/bin/env python3
"""
quit_during_call.py -- a quit (SIGTERM) that arrives while the UI thread waits in a slow EmuThread::call (#967, #1039).

  VJ_TEST_SLOW_CALL_MS stalls the load call() on the emulation thread (as smoke_busy_threaded does).  Once the app has
  logged "[emu] busy" the UI thread is parked in that call; the script sends SIGTERM then.  The headless signal handler
  only sets the quit flag, so the call must finish on its own, the frame loops must be skipped, and the app must leave
  cleanly: exit 0, the "quit requested" line, no owner violation, and neither 3 s warning (a thread that did not stop,
  a call that was not answered).

usage: quit_during_call.py <virtualjaguar> <rom>
"""
import os
import signal
import subprocess
import sys
import threading
import time

SLOW_MS = 2000


def main():
    exe, rom = sys.argv[1:3]
    env = dict(os.environ, VJ_TEST_SLOW_CALL_MS=str(SLOW_MS))
    p = subprocess.Popen([exe, "--headless", "--emu-thread", "on", "--frames", "30", rom], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines = []
    busy = threading.Event()

    def reader():
        for line in p.stdout:
            lines.append(line)
            if "[emu] busy" in line:
                busy.set()

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        if not busy.wait(30):
            print("FAIL the app never logged the busy state\n" + "".join(lines))
            return 1
        t0 = time.monotonic()
        p.send_signal(signal.SIGTERM)    # the UI thread is inside the call right now
        try:
            rc = p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            print("FAIL the app did not exit within 20 s of SIGTERM\n" + "".join(lines))
            return 1
        t.join(5)
        out = "".join(lines)
        if rc != 0:
            print("FAIL exit code %d, expected 0\n%s" % (rc, out)); return 1
        for bad in ("owner violation", "ThreadSanitizer", "did not stop within 3 s", "did not answer within 3 s"):
            if bad in out:
                print("FAIL the log says %r\n%s" % (bad, out)); return 1
        if "headless: quit requested, exiting cleanly" not in out:
            print("FAIL no \"quit requested\" line (the run ended some other way)\n" + out); return 1
        print("PASS: quit during a slow call exits 0 (%.1f s after the signal)" % (time.monotonic() - t0))
        return 0
    finally:
        if p.poll() is None:
            p.kill()


if __name__ == "__main__":
    sys.exit(main())
