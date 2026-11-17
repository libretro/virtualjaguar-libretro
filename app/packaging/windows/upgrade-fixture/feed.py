"""Create or serve loopback-only WinSparkle qualification feeds."""

import argparse
import base64
import importlib.util
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import subprocess

ORIGIN = "https://127.0.0.1:8443"
ARCHITECTURES = ("x64", "arm64")


def create(root, key, openssl):
    module_path = Path(__file__).resolve().parent.parent / "build-appcast.py"
    spec = importlib.util.spec_from_file_location("appcast", module_path)
    appcast = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(appcast)
    for arch in ARCHITECTURES:
        directory = root / arch
        directory.mkdir()
        for build in (101, 102):
            name = f"VirtualJaguar-nightly-{build}-windows-{arch}-direct-setup.exe"
            (root / name).replace(directory / name)
        installer = directory / name
        signature = subprocess.check_output([
            openssl, "pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(installer),
        ])
        invalid = directory / f"tampered-{name}"
        invalid.write_bytes(installer.read_bytes() + b"WinSparkle qualification mutation")
        encoded = base64.b64encode(signature).decode("ascii")
        for case, asset in (("valid", installer), ("tampered", invalid)):
            feed = appcast.make_appcast(
                "example/update-fixture", "nightly", arch, "102", "3.12.0",
                f"{ORIGIN}/{asset.name}", asset.stat().st_size, encoded,
            )
            (directory / f"{case}.xml").write_bytes(feed)


def serve(root, cert, key):
    handler = partial(SimpleHTTPRequestHandler, directory=root)
    with ThreadingHTTPServer(("127.0.0.1", 8443), handler) as server:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("create", "serve"):
        subparser = commands.add_parser(command)
        subparser.add_argument("root", type=Path)
        subparser.add_argument("key", type=Path)
        if command == "create":
            subparser.add_argument("--openssl", required=True)
        else:
            subparser.add_argument("--cert", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "create":
        create(args.root, args.key, args.openssl)
    else:
        serve(args.root, args.cert, args.key)


if __name__ == "__main__":
    main()
