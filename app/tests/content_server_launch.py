"""Exercise the real HTTP backend, browser worker and headless game loader on loopback."""
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.parse

client, emulator, rom = sys.argv[1:]
cartridge = pathlib.Path(rom).read_bytes()
failures = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        if url.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/public")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if url.path == "/public":
            body = b"public"
        else:
            if self.headers.get("Authorization") != "Bearer fixture-token":
                failures.append("request omitted bearer authentication")
                self.send_error(401)
                return
            if url.path == "/api/v1/capabilities":
                value = {"protocol": {"major": 1}}
            elif url.path == "/api/v1/browse-libraries":
                value = {"items": [{"id": "library", "name": "Test shelf"}], "nextCursor": None}
            elif url.path == "/api/v1/browse":
                if query.get("systemId") != ["com.provenance.jaguar"]:
                    failures.append("missing Jaguar system filter")
                second = "cursor" in query
                value = {"items": [{"id": "game-two" if second else "game", "name": "Fixture game",
                                    "entityType": "source" if second else "item", "systemId": "com.provenance.jaguar",
                                    "source" if second else "item": {
                                        "name": "source.j64", "size": len(cartridge), "availability": "local",
                                        "groupFileCount": 0}}],
                         "nextCursor": None if second else "next+page"}
                if second and query["cursor"] != ["next+page"]:
                    failures.append("cursor was not preserved")
            elif url.path == "/api/v1/items/game/manifest":
                value = {"files": [{"id": "cart", "kind": "rom", "required": True,
                                    "relativePath": "fixture.j64", "size": len(cartridge),
                                    "sha256": hashlib.sha256(cartridge).hexdigest(),
                                    "contentPath": "https://untrusted.invalid/credential-test"}]}
            elif url.path in ("/api/v1/files/cart/content", "/api/v1/sources/game-two/content"):
                body = cartridge
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                for offset in range(0, len(body), 4096):
                    self.wfile.write(body[offset:offset + 4096])
                return
            else:
                failures.append("unexpected API path")
                self.send_error(404)
                return
            body = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
cache = None
try:
    result = subprocess.run([client, "--server", f"http://127.0.0.1:{server.server_port}"],
                            capture_output=True, text=True, timeout=50)
    for line in result.stdout.splitlines():
        if line.startswith("CACHE "):
            cache = line[6:]
    if result.returncode:
        raise RuntimeError(f"Content browser failed ({result.returncode})\n{result.stdout}\n{result.stderr}")
    launch = next(line[7:] for line in result.stdout.splitlines() if line.startswith("LAUNCH "))
    assert pathlib.Path(launch).read_bytes() == cartridge
    source_launch = next(line[14:] for line in result.stdout.splitlines() if line.startswith("SOURCE_LAUNCH "))
    assert pathlib.Path(source_launch).read_bytes() == cartridge
    for downloaded in (launch, source_launch):
        with tempfile.TemporaryDirectory(prefix="vj-server-launch-") as data:
            environment = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
            game = subprocess.run([emulator, "--headless", "--frames", "300", "--expect-hash", "f4c30ea3",
                                   "--data-dir", data, downloaded], env=environment,
                                  capture_output=True, text=True, timeout=30)
            if game.returncode or "headless: OK" not in game.stdout + game.stderr:
                raise RuntimeError(f"Downloaded game failed to launch\n{game.stdout}\n{game.stderr}")
    assert not failures, failures
    print("Server picker -> paged library -> verified download -> real headless game launch passed")
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
    if cache:
        shutil.rmtree(cache)
