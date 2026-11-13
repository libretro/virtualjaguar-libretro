"""Exercise the real HTTP backend, browser worker and headless game loader on loopback."""
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import urllib.parse
import zlib

client, emulator, rom = sys.argv[1:]
cartridge = pathlib.Path(rom).read_bytes()
failures = []
patch = b"PATCHEOF"
patch_id = hashlib.sha256(patch).hexdigest()
companion_list = "/api/v1/sources/game-two/companions"
patch_path = f"{companion_list}/{patch_id}/content"



def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))


cover = (b"\x89PNG\r\n\x1a\n" +
         png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
         png_chunk(b"IDAT", zlib.compress(b"\x00\x20\x40\x60\xff")) +
         png_chunk(b"IEND", b""))


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
        if url.path == patch_path and self.headers.get("Authorization") == "Bearer deny-companion":
            self.send_error(403)
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
                value = {"items": [{"id": "game-two" if second else "42", "name": "Fixture game",
                                    "entityType": "source" if second else "item", "systemId": "com.provenance.jaguar",
                                    "source" if second else "item": {
                                        "name": "source.j64", "size": len(cartridge), "availability": "local",
                                        "groupFileCount": 0}}],
                         "nextCursor": None if second else "next+page"}
                if not second:
                    value["items"][0]["previewAsset"] = {"id": "cover"}
                if second and query["cursor"] != ["next+page"]:
                    failures.append("cursor was not preserved")
            elif url.path == "/api/v1/items/42/manifest":
                value = {"files": [{"id": "cart", "kind": "rom", "required": True,
                                    "relativePath": "fixture.j64", "size": len(cartridge),
                                    "sha256": hashlib.sha256(cartridge).hexdigest(),
                                    "contentPath": "https://untrusted.invalid/credential-test"}]}
            elif url.path == companion_list:
                value = {"supportedKinds": ["patch"], "items": [{
                    "id": patch_id, "kind": "patch", "format": "ips", "filename": "Fixture.ips",
                    "sizeBytes": len(patch), "sha256": patch_id, "contentPath": patch_path,
                }]}
            elif url.path == patch_path:
                self.send_response(200)
                self.send_header("Content-Length", str(len(patch)))
                self.end_headers()
                self.wfile.write(patch)
                return
            elif url.path in ("/api/v1/files/cart/content", "/api/v1/sources/game-two/content",
                              "/api/v1/files/cover/content"):
                body = cover if url.path == "/api/v1/files/cover/content" else cartridge
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


class RomMHandler(Handler):
    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        if self.headers.get("Authorization") != "Bearer romm-fixture-token":
            failures.append("RomM request used the wrong credentials")
            self.send_error(401)
            return
        cue = b'FILE "Track.bin" BINARY\n  TRACK 01 MODE1/2352\n'
        files = {
            101: ("Fixture.j64", cartridge, "roms/jaguar/Fixture.j64"),
            102: ("Fixture.cue", cue, "roms/jaguarcd/Fixture/Fixture.cue"),
            103: ("Track.bin", b"track", "roms/jaguarcd/Fixture/Track.bin"),
            104: ("Fixture.ips", patch, "roms/jaguar/Fixture.j64/patch/Fixture.ips"),
            105: ("Fixture.ips", b"mod-data", "roms/jaguar/Fixture.j64/mod/Fixture.ips"),
        }
        if url.path == "/api/platforms":
            value = [{"id": 1, "slug": "snes", "rom_count": 80000},
                     {"id": 15, "slug": "jaguar"}, {"id": 21, "slug": "jaguarcd"}]
        elif url.path == "/api/roms":
            if (query.get("platform_ids") not in (["15"], ["21"]) or
                    query.get("search_term") != ["Jag + CD"] or query.get("limit") != ["100"] or
                    any(query.get(key) != ["false"] for key in
                        ("with_rom_id_index", "with_filter_values", "with_char_index", "with_files"))):
                failures.append("RomM did not send a bounded Jaguar query")
            platform = int(query["platform_ids"][0])
            offset = int(query["offset"][0])
            row = {"id": 44 if platform == 21 else 42 + offset, "name": "Fixture game", "platform_id": platform}
            if offset:
                row["path_cover_small"] = "/assets/romm/resources/missing/small.png?ts=2026-11-11 00:00"
            if platform == 21:
                row["path_cover_small"] = "/assets/romm/resources/fixture/small.webp?ts=2026-11-11 00:00"
            value = {"items": [row], "total": 2 if platform == 15 else 1}
        elif url.path in ("/api/roms/42/simple", "/api/roms/44/simple"):
            # RomM permits IDs to overlap ECS. The second cartridge row is used only for artwork.
            disc = "/44/" in url.path
            ids = [102, 103] if disc else [101, 104, 105]
            value = {"id": 44 if disc else 42, "fs_name": "Fixture" if disc else "Fixture.j64",
                     "fs_path": "roms/jaguarcd" if disc else "roms/jaguar",
                     "files": [{"id": key, "file_name": files[key][0], "file_size_bytes": len(files[key][1]),
                                "full_path": files[key][2],
                                "category": {104: "patch", 105: "mod"}.get(key, "game")} for key in ids]}
        elif url.path.startswith(("/api/roms/42/content/", "/api/roms/44/content/")):
            key = int(query["file_ids"][0])
            name, body, _ = files[key]
            purpose = "download" if key in (104, 105) else "play"
            if url.path.rsplit("/", 1)[-1] != name or query.get("purpose") != [purpose]:
                failures.append("incorrect RomM file selector")
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        elif url.path == "/assets/romm/resources/missing/small.png" and not query:
            self.send_error(404)
            return
        elif url.path == "/assets/romm/resources/fixture/small.png" and not query:
            self.send_response(200)
            self.send_header("Content-Length", str(len(cover)))
            self.end_headers()
            self.wfile.write(cover)
            return
        else:
            failures.append("unexpected RomM API path")
            self.send_error(404)
            return
        body = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


servers = [http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) for handler in (Handler, RomMHandler)]
threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
for thread in threads:
    thread.start()
# Reserve an unused address without listening to simulate a connection refusal.
offline = socket.socket()
offline.bind(("127.0.0.1", 0))
cache = None
try:
    origins = [f"http://127.0.0.1:{server.server_port}" for server in servers]
    origins.append(f"http://127.0.0.1:{offline.getsockname()[1]}")
    result = subprocess.run([client, "--servers", *origins],
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
    patch_file = next(line[13:] for line in result.stdout.splitlines() if line.startswith("SERVER_PATCH "))
    assert pathlib.Path(patch_file).read_bytes() == patch
    romm_launch = next(line[12:] for line in result.stdout.splitlines() if line.startswith("ROMM_LAUNCH "))
    assert pathlib.Path(romm_launch).read_bytes() == cartridge
    for downloaded in (launch, source_launch, romm_launch):
        with tempfile.TemporaryDirectory(prefix="vj-server-launch-") as data:
            environment = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
            game = subprocess.run([emulator, "--headless", "--frames", "300", "--expect-hash", "e33e2c61",
                                   "--data-dir", data, downloaded], env=environment,
                                  capture_output=True, text=True, timeout=30)
            if game.returncode or "headless: OK" not in game.stdout + game.stderr:
                raise RuntimeError(f"Downloaded game failed to launch\n{game.stdout}\n{game.stderr}")
    assert not failures, failures
    print("Server picker -> paged library -> verified download -> real headless game launch passed")
finally:
    offline.close()
    for server, thread in zip(servers, threads):
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    if cache:
        shutil.rmtree(cache)
