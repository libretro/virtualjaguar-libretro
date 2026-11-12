#!/usr/bin/env python3
"""Validate a public direct Windows installer and write one local appcast entry."""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
MANAGED_REPOSITORY = "libretro/virtualjaguar-libretro"
ED25519_SPKI = bytes.fromhex("302a300506032b6570032100")


def download(url, output):
    with urllib.request.urlopen(url, timeout=60) as response:
        if not response.url.startswith("https://"):
            raise ValueError("download redirected outside HTTPS")
        shutil.copyfileobj(response, output)


def validate_public_installer(repository, tag, name, destination):
    api = f"https://api.github.com/repos/{repository}/releases/tags/{tag}"
    with urllib.request.urlopen(api, timeout=30) as response:
        release = json.load(response)
    if release.get("immutable") is not True or release.get("draft"):
        raise ValueError("installer must come from a public immutable GitHub release")
    if release.get("tag_name") != tag:
        raise ValueError("release tag does not match the requested build")

    url = f"https://github.com/{repository}/releases/download/{tag}/{name}"
    assets = [asset for asset in release["assets"] if asset["name"] == name]
    if len(assets) != 1 or assets[0]["browser_download_url"] != url:
        raise ValueError(f"immutable release does not contain {name}")

    with destination.open("wb") as output:
        download(url, output)
    length = destination.stat().st_size
    with destination.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    if length != assets[0]["size"] or assets[0].get("digest") != f"sha256:{digest}":
        raise ValueError("public installer differs from immutable release metadata")
    return url, length


def verify_signatures(installer, public_key, signature, openssl, powershell):
    key_file = installer.with_suffix(".pub")
    signature_file = installer.with_suffix(".sig")
    key_file.write_bytes(ED25519_SPKI + public_key)
    signature_file.write_bytes(signature)
    subprocess.run(
        [openssl, "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey",
         str(key_file), "-rawin", "-in", str(installer), "-sigfile", str(signature_file)],
        check=True,
    )
    subprocess.run(
        [powershell, "-NoProfile", "-NonInteractive", "-Command",
         "$ErrorActionPreference = 'Stop'; "
         "$signature = Get-AuthenticodeSignature -LiteralPath $env:VJ_APPCAST_INSTALLER; "
         "if ($signature.Status -ne 'Valid') { throw $signature.StatusMessage }"],
        env={**os.environ, "VJ_APPCAST_INSTALLER": str(installer)},
        check=True,
    )


def version_tuple(channel, value):
    pattern = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    if channel == "nightly":
        pattern = r"[1-9][0-9]*"
    if not re.fullmatch(pattern, value):
        raise ValueError(f"invalid {channel} build version: {value}")
    parts = tuple(map(int, value.split(".")))
    if max(parts) > 2147483647:
        raise ValueError("version component exceeds WinSparkle's signed 32-bit range")
    return parts


def check_previous(previous, channel, arch, version):
    if previous is None:
        return
    root = ET.parse(previous).getroot()
    if root.findtext("./channel/title") != f"Virtual Jaguar Windows {channel} {arch}":
        raise ValueError("previous appcast is for a different channel or architecture")
    enclosures = root.findall("./channel/item/enclosure")
    if not enclosures:
        raise ValueError("previous appcast has no enclosure")
    for enclosure in enclosures:
        if enclosure.attrib.get(f"{{{SPARKLE}}}os") != f"windows-{arch}":
            raise ValueError("previous appcast contains a different architecture")
        previous_version = enclosure.attrib[f"{{{SPARKLE}}}version"]
        if version_tuple(channel, version) <= version_tuple(channel, previous_version):
            raise ValueError(f"version must increase from {previous_version}")


def make_appcast(repository, channel, arch, version, display_version, url, length, signature):
    ET.register_namespace("sparkle", SPARKLE)
    rss = ET.Element("rss", {"version": "2.0"})
    feed = ET.SubElement(rss, "channel")
    ET.SubElement(feed, "title").text = f"Virtual Jaguar Windows {channel} {arch}"
    ET.SubElement(feed, "link").text = f"https://github.com/{repository}/releases"
    ET.SubElement(feed, "description").text = "Virtual Jaguar direct installer updates"
    item = ET.SubElement(feed, "item")
    ET.SubElement(item, "title").text = f"Virtual Jaguar {display_version}"
    ET.SubElement(item, "enclosure", {
        "url": url,
        "length": str(length),
        "type": "application/octet-stream",
        f"{{{SPARKLE}}}version": version,
        f"{{{SPARKLE}}}shortVersionString": display_version,
        f"{{{SPARKLE}}}os": f"windows-{arch}",
        f"{{{SPARKLE}}}edSignature": signature,
    })
    ET.indent(rss)
    return ET.tostring(rss, encoding="utf-8", xml_declaration=True) + b"\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("channel", choices=["stable", "nightly"])
    parser.add_argument("arch", choices=["x64", "arm64"])
    parser.add_argument("version")
    parser.add_argument("display_version")
    parser.add_argument("--asset-repository", required=True, help="Dedicated GitHub release repository (owner/repo)")
    parser.add_argument("--public-key", required=True)
    parser.add_argument("--signature", required=True)
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--powershell", default="powershell.exe")
    parser.add_argument("--output", type=Path, required=True)
    history = parser.add_mutually_exclusive_group(required=True)
    history.add_argument("--previous", type=Path)
    history.add_argument("--first-entry", action="store_true")
    args = parser.parse_args()

    repository = args.asset_repository
    if (not re.fullmatch(r"[A-Za-z0-9-]+/[A-Za-z0-9_.-]+", repository)
            or repository.split("/")[1] in (".", "..")
            or repository.lower() == MANAGED_REPOSITORY):
        parser.error("--asset-repository must name the dedicated update-release repository")
    version_tuple(args.channel, args.version)
    version_tuple("stable", args.display_version)
    if args.channel == "stable" and args.version != args.display_version:
        raise ValueError("stable build and display versions must match")
    check_previous(args.previous, args.channel, args.arch, args.version)
    key = base64.b64decode(args.public_key, validate=True)
    signature = base64.b64decode(args.signature, validate=True)
    if len(key) != 32 or len(signature) != 64:
        raise ValueError("expected an Ed25519 public key and signature")

    tag = f"windows-direct-{args.channel}-{args.version}"
    name = f"VirtualJaguar-{args.channel}-{args.version}-windows-{args.arch}-direct-setup.exe"
    with tempfile.TemporaryDirectory() as temporary:
        installer = Path(temporary) / name
        url, length = validate_public_installer(repository, tag, name, installer)
        verify_signatures(installer, key, signature, args.openssl, args.powershell)

    feed = make_appcast(repository, args.channel, args.arch, args.version, args.display_version,
                        url, length, args.signature)
    with args.output.open("xb") as output:
        output.write(feed)


if __name__ == "__main__":
    main()
