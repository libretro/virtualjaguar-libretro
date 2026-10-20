#!/usr/bin/env python3
"""Opt-in integration tests using pinned, reviewed community sources, never executed."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.request

import convert


MANIFEST = Path(__file__).with_name("community-cases.json")


def contract(module, digest):
    results = []
    for entry in module.convert(digest):
        item = {"name": entry["name"]}
        if "code" in entry:
            item.update(writes=entry["writes"],
                        code_sha256=hashlib.sha256(entry["code"].encode("ascii")).hexdigest())
        else:
            item["unsupported"] = entry["unsupported"]
        results.append(item)
    return results


def checked_source(raw, case):
    if len(raw) > 1024*1024 or hashlib.sha256(raw).hexdigest() != case["source_sha256"]:
        raise ValueError("source checksum mismatch; use the pinned repository revision")
    return convert.Module(raw.decode("utf-8-sig"))


def check_case(raw, case, rom_dir=None):
    module = checked_source(raw, case)
    digest = int(case["rom_fnv1a64"], 16)
    # With no cartridge this is a conversion contract test against the source's
    # known hash, not a ROM identity or gameplay test. Local ROM mode adds the
    # real identity check; no private cartridge is downloaded or sent to CI.
    if rom_dir is not None:
        digest = convert.fnv1a64(convert.rom_bytes(rom_dir / case["rom_name"]))
        if digest != int(case["rom_fnv1a64"], 16):
            raise ValueError("local ROM hash does not match this source revision")
    if module.title != case["title"] or contract(module, digest) != case["expected"]:
        raise ValueError("conversion contract changed (names, values, grouping, or omissions)")
    wrong = module.convert(digest ^ 1)
    if any("code" in item for item in wrong):
        raise ValueError("wrong ROM hash activated a cheat")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--source-dir", type=Path, help="local checkout of the pinned community repository")
    inputs.add_argument("--fetch", action="store_true", help="download only the three reviewed source files to memory")
    parser.add_argument("--rom-dir", type=Path, help="also validate matching native cartridges in this local directory")
    args = parser.parse_args(argv)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if (manifest["format"] != 1 or not manifest["cases"]
            or not re.fullmatch(r"[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+", manifest["repository"])
            or not re.fullmatch(r"[0-9a-f]{40}", manifest["commit"])):
        parser.error("invalid or empty community test manifest")
    failures = 0
    for case in manifest["cases"]:
        name = case["source"]
        try:
            if not re.fullmatch(r"scripts/cheat_[a-z0-9_]+\.c", name):
                raise ValueError("only individually reviewed cheat C sources may be fetched")
            if args.fetch:
                url = f"https://raw.githubusercontent.com/{manifest['repository']}/{manifest['commit']}/{name}"
                with urllib.request.urlopen(url, timeout=30) as response:
                    raw = response.read(1024*1024 + 1)
            else:
                path = args.source_dir / name
                if path.stat().st_size > 1024*1024:
                    raise ValueError("source exceeds 1 MiB")
                raw = path.read_bytes()
            check_case(raw, case, args.rom_dir)
            print("PASS " + name + (" (local ROM hash verified)" if args.rom_dir else " (conversion contract only)"))
        except (ValueError, OSError, convert.Unsupported, convert.c_parser.ParseError) as error:
            failures += 1
            print("FAIL " + name + ": " + str(error), file=sys.stderr)
    print(f"Community sources: {len(manifest['cases'])-failures} passed, {failures} failed; no sources executed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
