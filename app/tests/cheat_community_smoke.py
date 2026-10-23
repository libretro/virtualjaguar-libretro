#!/usr/bin/env python3
"""Community cheat store end to end (#1071): the real app, headless, real HTTP over loopback.

usage: cheat_community_smoke.py VIRTUALJAGUAR SITE_CHEATS_DIR ROM CRC32 WORKDIR

1. Serves SITE_CHEATS_DIR on 127.0.0.1 and runs `virtualjaguar --headless --cheats-community-install ROM` with
   cheats.community.url pointing at it: the pack must land in <data>/cheats/community/packs/<CRC>.cht with its sidecar,
   byte for byte what the server holds.
2. Stops the server and runs it again on the same data directory: the installed pack must still be found (offline).
3. A fresh data directory with the server stopped must FAIL (nothing to install from).
"""
import functools
import http.server
import pathlib
import shutil
import subprocess
import sys
import threading


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def run(exe, data, rom, url):
    data.mkdir(parents=True, exist_ok=True)
    (data / "config.ini").write_text("cheats.community.url=%s\nlibrary.art=1\n" % url)
    p = subprocess.run([exe, "--headless", "--frames", "30", "--allow-blank", "--data-dir", str(data),
                        "--cheats-community-install", rom], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def main():
    exe, site, rom, crc, work = sys.argv[1:6]
    site, work = pathlib.Path(site), pathlib.Path(work)
    shutil.rmtree(work, ignore_errors=True)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(site)))
    url = "http://127.0.0.1:%d/" % srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    data = work / "data"
    rc, out = run(exe, data, rom, url)
    pack = data / "cheats/community/packs" / (crc + ".cht")
    if rc != 0 or "COMMUNITY pack %s installed" % crc not in out:
        print("FAIL install (rc=%d)\n%s" % (rc, out)); return 1
    if not pack.is_file() or not pack.with_suffix(".json").is_file():
        print("FAIL pack or sidecar missing under", data); return 1
    if pack.read_bytes() != (site / "packs" / (crc + ".cht")).read_bytes():
        print("FAIL installed bytes differ from the served pack"); return 1

    srv.shutdown(); srv.server_close()   # from here on nothing answers
    rc, out = run(exe, data, rom, url)
    if rc != 0 or "COMMUNITY pack %s installed" % crc not in out:
        print("FAIL offline reuse of the installed pack (rc=%d)\n%s" % (rc, out)); return 1

    rc, out = run(exe, work / "fresh", rom, url)
    if rc == 0 or (work / "fresh/cheats/community/packs" / (crc + ".cht")).exists():
        print("FAIL a fresh data dir with no server must not install (rc=%d)\n%s" % (rc, out)); return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
