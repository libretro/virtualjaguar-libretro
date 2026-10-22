#!/usr/bin/env python3
"""Build the committed cartridge identity table (cartridges.json): hashes, sizes and titles only.

Never writes ROM bytes or file paths. The community cheat pack generator maps a script's
FNV1a64 cartridge hash to the CRC32 the Virtual Jaguar app binds packs to using this table.
"""
import argparse
import importlib.util
import json
import re
import sys
import zlib
from pathlib import Path

_spec = importlib.util.spec_from_file_location("cheat_converter", Path(__file__).with_name("convert.py"))
convert = sys.modules.get(_spec.name) or importlib.util.module_from_spec(_spec)
if _spec.name not in sys.modules:
    sys.modules[_spec.name] = convert
    _spec.loader.exec_module(convert)

SUFFIXES = (".j64", ".jag", ".rom", ".bin")
FIELDS = {"fnv1a64", "crc32", "size", "title", "name"}


def title_of(stem):
    return re.sub(r"(\s*\[[^\]]*\])+\s*$", "", stem.split(" (", 1)[0]).strip()


def _hex(value, width, what):
    if not (isinstance(value, str) and len(value) == width and re.fullmatch(r"[0-9A-F]+", value)):
        raise ValueError(f"{what} must be {width} upper-case hex digits: {value!r}")


def load_cartridges(path):
    """fnv1a64 -> row. ValueError on a bad format, bad hex, bad row shape or duplicate fnv1a64/crc32."""
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(doc, dict) or doc.get("format") != 1 or not isinstance(doc.get("cartridges"), list):
        raise ValueError("cartridge table must be {\"format\": 1, \"cartridges\": [...]}")
    by_fnv, crcs = {}, set()
    for entry in doc["cartridges"]:
        if not isinstance(entry, dict) or set(entry) != FIELDS:
            raise ValueError(f"cartridge row must have exactly {sorted(FIELDS)}: {entry!r}")
        _hex(entry["fnv1a64"], 16, "fnv1a64")
        _hex(entry["crc32"], 8, "crc32")
        if type(entry["size"]) is not int or entry["size"] <= 0:
            raise ValueError(f"size must be a positive integer: {entry['size']!r}")
        if not all(isinstance(entry[key], str) and entry[key] for key in ("title", "name")):
            raise ValueError(f"title and name must be non-empty strings: {entry!r}")
        if entry["fnv1a64"] in by_fnv:
            raise ValueError("duplicate fnv1a64 " + entry["fnv1a64"])
        if entry["crc32"] in crcs:
            raise ValueError("duplicate crc32 " + entry["crc32"])
        by_fnv[entry["fnv1a64"]] = entry
        crcs.add(entry["crc32"])
    return by_fnv


def scan_dir(root, log=lambda message: None):
    """Rows for every cartridge under root, in sorted path order; identical bytes keep the first path."""
    files = sorted((p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in SUFFIXES
                    and not p.name.startswith(".")), key=lambda p: p.relative_to(root).as_posix())
    rows, seen = [], set()
    for path in files:
        try:
            data = convert.rom_bytes(path)
        except convert.Unsupported as error:
            log(f"skipped {path.relative_to(root).as_posix()}: {error}")
            continue
        fnv = f"{convert.fnv1a64(data):016X}"
        if fnv in seen:
            continue
        seen.add(fnv)
        rows.append({"fnv1a64": fnv, "crc32": f"{zlib.crc32(data):08X}", "size": len(data),
                     "title": title_of(path.stem), "name": path.stem})
    return rows


def merge_rows(existing, scanned):
    """Existing rows win (curated title/name); only unknown fnv1a64 rows are appended. Sorted by fnv1a64."""
    by_fnv = {row["fnv1a64"]: row for row in existing}
    crc_owner = {row["crc32"]: row["fnv1a64"] for row in existing}
    for row in scanned:
        if row["fnv1a64"] in by_fnv:
            continue
        if crc_owner.get(row["crc32"], row["fnv1a64"]) != row["fnv1a64"]:
            raise ValueError(f"crc32 {row['crc32']} collides: {crc_owner[row['crc32']]} vs {row['fnv1a64']}")
        by_fnv[row["fnv1a64"]] = row
        crc_owner[row["crc32"]] = row["fnv1a64"]
    return [by_fnv[key] for key in sorted(by_fnv)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom-dir", type=Path, required=True, help="directory scanned recursively (read-only)")
    parser.add_argument("--merge", type=Path, help="existing cartridges.json whose curated rows are kept")
    parser.add_argument("--output", type=Path, required=True, help="new JSON file; an existing file is refused")
    args = parser.parse_args(argv)
    try:
        if args.output.exists():
            raise ValueError(f"output already exists: {args.output}")
        if not args.rom_dir.is_dir():
            raise ValueError(f"not a directory: {args.rom_dir}")
        existing = list(load_cartridges(args.merge).values()) if args.merge else []
        rows = merge_rows(existing, scan_dir(args.rom_dir, lambda m: print(m, file=sys.stderr)))
        text = json.dumps({"format": 1, "cartridges": rows}, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
        with args.output.open("x", encoding="utf-8", newline="\n") as output_file:
            output_file.write(text)
        print(f"Wrote {len(rows)} cartridges: {args.output}")
        return 0
    except (ValueError, OSError) as error:
        print("Cannot scan: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
