#!/usr/bin/env python3
"""Stage one channel's three platform-verified appcasts for a single Git commit."""

import argparse
import base64
from pathlib import Path
import re
import xml.etree.ElementTree as ET

SPARKLE = "{http://www.andymatuschak.org/xml-namespaces/sparkle}"


def entry_version(xml, channel, arch, repository):
    root = ET.fromstring(xml)
    if root.tag != "rss":
        raise ValueError("expected an RSS appcast")
    items = root.findall("./channel/item")
    if len(items) != 1:
        raise ValueError("expected exactly one release entry")
    item = items[0]
    enclosures = item.findall("enclosure")
    if len(enclosures) != 1:
        raise ValueError("expected exactly one download")
    enclosure = enclosures[0]
    if arch:
        if enclosure.get(SPARKLE + "os") != f"windows-{arch}":
            raise ValueError(f"expected Windows {arch}")
        version = enclosure.get(SPARKLE + "version", "")
    else:
        version = item.findtext(SPARKLE + "version", "")
    pattern = r"(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*)){2}"
    if channel == "nightly":
        pattern = r"[1-9][0-9]*"
    if not re.fullmatch(pattern, version):
        raise ValueError(f"invalid {channel} version: {version!r}")
    numbers = tuple(map(int, version.split(".")))
    if max(numbers) > 2147483647:
        raise ValueError("version component exceeds signed 32-bit range")
    if arch:
        tag = f"windows-direct-{channel}-{version}"
        asset = f"VirtualJaguar-{channel}-{version}-windows-{arch}-direct-setup.exe"
    else:
        tag = f"v{version}" if channel == "stable" else f"macos-nightly-{version}"
        asset = f"VirtualJaguar-{tag}-macos-universal-direct.zip"
    expected = f"https://github.com/{repository}/releases/download/{tag}/{asset}"
    if enclosure.get("url") != expected:
        raise ValueError(f"expected build-specific download {expected}")
    if len(base64.b64decode(enclosure.get(SPARKLE + "edSignature", ""), validate=True)) != 64:
        raise ValueError("expected an Ed25519 signature")
    if not re.fullmatch(r"[1-9][0-9]*", enclosure.get("length", "")):
        raise ValueError("expected a positive asset length")
    return numbers


def stage_feeds(channel, repository, candidates, destination):
    targets = (
        ("macos.xml", f"updates/macos/{channel}/appcast.xml", None),
        ("windows-x64.xml", f"updates/windows/{channel}-x64.xml", "x64"),
        ("windows-arm64.xml", f"updates/windows/{channel}-arm64.xml", "arm64"),
    )
    staged = []
    versions = set()
    for filename, path, arch in targets:
        source = (candidates / filename).read_bytes()
        version = entry_version(source, channel, arch, repository)
        versions.add(version)
        target = destination / path
        if target.is_file():
            previous = target.read_bytes()
            if previous == source:
                continue
            if entry_version(previous, channel, arch, repository) >= version:
                raise ValueError(f"{path}: refusing a stale or changed published version")
        staged.append((target, source))
    if len(versions) != 1:
        raise ValueError("all three platform feeds must have the same build version")
    for target, source in staged:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source)
        print(target.relative_to(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("channel", choices=("stable", "nightly"))
    parser.add_argument("repository", help="Dedicated immutable asset repository, owner/name")
    parser.add_argument("candidates", type=Path, help="Directory with three platform-verified appcasts")
    parser.add_argument("destination", type=Path, help="Fresh checkout of the feed publication branch")
    args = parser.parse_args()
    if (not re.fullmatch(r"[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+", args.repository)
            or args.repository.split("/")[1] in (".", "..")
            or args.repository.lower() == "libretro/virtualjaguar-libretro"):
        parser.error("use a dedicated asset repository")
    try:
        stage_feeds(args.channel, args.repository, args.candidates, args.destination)
    except (OSError, ValueError, ET.ParseError) as error:
        parser.exit(1, f"Update feeds were not staged: {error}\n")


if __name__ == "__main__":
    main()
