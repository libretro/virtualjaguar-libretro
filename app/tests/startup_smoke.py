#!/usr/bin/env python3
"""Exercise startup with real GL; run manually on a desktop or under Xvfb."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    exe, rom = map(lambda value: str(Path(value).resolve()), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="vj-startup-") as root:
        for case in ("empty", "slow", "headless"):
            folder = Path(root) / case
            folder.mkdir()
            shot = folder / "frame.png"
            args = [exe, "--data-dir", str(folder), "--shader", "off", "--emu-thread", "on"]
            if case == "headless":
                args += ["--headless", "--frames", "2", rom]
            else:
                args += ["--screenshot", str(shot), "--screenshot-frame", "2"]
                if case == "slow":
                    args.append(rom)
            env = dict(os.environ)
            env.pop("VJ_TEST_SLOW_CALL_MS", None)
            if case == "slow":
                env["VJ_TEST_SLOW_CALL_MS"] = "1500"
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
            log = result.stdout + result.stderr
            if result.returncode:
                raise AssertionError(f"{case}: exit {result.returncode}\n{log}")
            if case == "headless":
                assert "[startup]" not in log, log
            else:
                qualified = log.index("[startup] renderer qualified:")
                started = log.index("[emu] emulation thread on;")
                ready = re.search(r"\[startup\] ready \(.*?, (\d+) frames\)", log)
                assert ready and qualified < started < ready.start(), log
                assert shot.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), log
                if case == "slow":
                    assert "[emu] busy:" in log, log
                    assert int(ready.group(1)) > 3, "No animation during slow load\n" + log
            print(f"PASS {case}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
