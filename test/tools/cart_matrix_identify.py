#!/usr/bin/env python3
"""Backfill the `<!-- id: ... -->` identity stamp on cart-boot-matrix rows.

cart_boot_matrix.sh stamps every fresh row with the image's identity straight
from the probe (cart_boot_probe.c, issue #976): byte size, CRC32, byte 0 of
the image as the boot ROM sees it, and the src/core/filedb.c entry for that
CRC.  The site generator (scripts/build_site.py) reads the stamp to say WHY
the real boot ROM rejected a file ("altered or bad dump: a verified dump
boots" / "dump cannot be verified" / "unsigned image") instead of printing a
stage name, and to fold `.jag`/`.rom` twins with the same bytes into one row.

Rows written by an older sweep have no stamp.  Re-running the sweep needs the
wide-ABI core and ~45 minutes of corpus time; the identity does not, so this
tool computes it from the files alone and writes the same stamp format:

    python3 test/tools/cart_matrix_identify.py docs/cart-boot-matrix.md

Only rows without a stamp are touched (`--force` re-stamps all), the build
stamp and every cell stay byte-identical, and `--check` compares the stamps
already there against the files (exit 1 on a mismatch) so a probe-stamped
matrix can be audited against this independent computation.

What is replicated from the core, and where it can differ from the probe:
  * the prepended copier header rule (DetectPrependedHeaderSize in
    src/core/file.c): N MiB + 512 (or a sub-1-MiB image whose $400 marker
    sits 512 bytes in) is stripped before the CRC, as the core does;
  * byte 0 as mapped at $800000: the payload's first byte, except an
    Alpine-shaped image (size + 8192 is a whole MiB, no $400 marker) which
    the loader maps at $802000 behind $FF fill.  A RAM-loaded executable
    (ABS/COF/JAGR, BJL binaries) never runs the boot ROM, so its byte 0 is
    cosmetic here; the probe prints whatever the cart slot holds ($00).
The filedb table itself is parsed from src/core/filedb.c, never copied.

Title -> file: the sweep names a row after the file's stem, so twins
(`X.jag` + `X.rom`) are two rows with one title; rows sharing a title take
that stem's files in sorted order, which is the order the sweep wrote them.
"""
import argparse
import os
import re
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_ROMS = os.path.join(ROOT, "test", "roms", "private", "ROMS")
DEFAULT_FILEDB = os.path.join(ROOT, "src", "core", "filedb.c")

# The same file set cart_boot_matrix.sh sweeps.
ROM_EXTS = (".j64", ".jag", ".rom", ".abs", ".cof", ".bin", ".prg")

# src/core/filedb.h FileFlags.
FILE_FLAGS = {
    "FF_ROM": 0x01, "FF_ALPINE": 0x02, "FF_BIOS": 0x04, "FF_REQ_DSP": 0x08,
    "FF_REQ_BIOS": 0x10, "FF_NON_WORKING": 0x20, "FF_BAD_DUMP": 0x40,
    "FF_VERIFIED": 0x80, "FF_STARS_1": 0x000, "FF_STARS_2": 0x100,
    "FF_STARS_3": 0x200, "FF_STARS_4": 0x300, "FF_STARS_5": 0x400,
}
FILEDB_ROW_RE = re.compile(
    r'^\s*\{\s*0x([0-9A-Fa-f]{8})\s*,\s*"([^"]*)"\s*,\s*([A-Z_0-9|\s]*?)\s*\}', re.M)
FILEDB_END_CRC = 0xFFFFFFFF

# src/core/file.c constants.
CART_HEADER_SKIP_SIZE = 512
CART_UNIVERSAL_MARKER_OFFSET = 0x400
CART_UNIVERSAL_MARKER = b"\x04\x04\x04\x04"
MIB = 1048576

ID_STAMP_RE = re.compile(r"<!--\s*id:\s*(.*?)\s*-->")
BUILD_STAMP_RE = re.compile(r"<!--\s*build:[^>]*-->")
PIPE_SPLIT = re.compile(r"(?<!\\)\|")


def load_filedb(path):
    """CRC -> (name, flags) from src/core/filedb.c, the sentinel excluded."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    db = {}
    for m in FILEDB_ROW_RE.finditer(text):
        crc = int(m.group(1), 16)
        if crc == FILEDB_END_CRC:
            continue
        flags = 0
        for tok in m.group(3).split("|"):
            tok = tok.strip()
            if not tok:
                continue
            if tok not in FILE_FLAGS:
                raise SystemExit("%s: unknown flag %r on CRC %08X"
                                 % (path, tok, crc))
            flags |= FILE_FLAGS[tok]
        db.setdefault(crc, (m.group(2), flags))
    if not db:
        raise SystemExit("%s: no romList rows found" % path)
    return db


def verified_sibling(db, crc):
    """FindVerifiedRomVariant: a VERIFIED entry with the same name, other CRC."""
    name = db[crc][0]
    for other, (oname, oflags) in db.items():
        if other != crc and oname == name and oflags & FILE_FLAGS["FF_VERIFIED"]:
            return other
    return 0


def marker_at(data, off):
    return data[off:off + 4] == CART_UNIVERSAL_MARKER


def prepended_header_size(data):
    """Port of DetectPrependedHeaderSize (src/core/file.c)."""
    size = len(data)
    if size < CART_HEADER_SKIP_SIZE + CART_UNIVERSAL_MARKER_OFFSET + 4:
        return 0
    if size % MIB == 0:
        return 0
    payload = size - CART_HEADER_SKIP_SIZE
    if not marker_at(data, CART_HEADER_SKIP_SIZE + CART_UNIVERSAL_MARKER_OFFSET):
        return 0
    if payload % MIB == 0:
        return CART_HEADER_SKIP_SIZE
    if payload < MIB and not marker_at(data, CART_UNIVERSAL_MARKER_OFFSET):
        return CART_HEADER_SKIP_SIZE
    return 0


def alpine_shaped(data):
    """The loader fills $800000-$801FFF with $FF and maps the image at
    $802000 for these (JST_ALPINE); the boot ROM then sees byte 0 = $FF."""
    size = len(data)
    has_marker = size >= CART_UNIVERSAL_MARKER_OFFSET + 8 and \
        marker_at(data, CART_UNIVERSAL_MARKER_OFFSET)
    if has_marker:
        return False
    return (size + 8192) % MIB == 0


def identify(path, db):
    """-> dict(size, crc32, hdr_byte0, entry=(name, flags)|None, sibling)."""
    with open(path, "rb") as fh:
        data = fh.read()
    data = data[prepended_header_size(data):]
    if not data:
        return None
    crc = zlib.crc32(data) & 0xFFFFFFFF
    byte0 = 0xFF if alpine_shaped(data) else data[0]
    entry = db.get(crc)
    return {"size": len(data), "crc32": crc, "hdr_byte0": byte0,
            "entry": entry,
            "sibling": verified_sibling(db, crc) if entry else 0}


def format_stamp(ident):
    """Byte-for-byte the probe's identity tail inside the comment."""
    s = " size=%d crc32=$%08X hdr_byte0=$%02X filedb=%d" % (
        ident["size"], ident["crc32"], ident["hdr_byte0"],
        1 if ident["entry"] else 0)
    if ident["entry"]:
        name, flags = ident["entry"]
        s += ' filedb_flags=$%03X filedb_name="%s" filedb_verified_sibling=$%08X' % (
            flags, name, ident["sibling"])
    return "<!-- id:%s -->" % s


def rom_files_by_stem(roms_root):
    stems = {}
    for name in sorted(os.listdir(roms_root)):
        p = os.path.join(roms_root, name)
        if not os.path.isfile(p) or name.startswith("[BIOS]"):
            continue
        if not name.lower().endswith(ROM_EXTS):
            continue
        stems.setdefault(os.path.splitext(name)[0], []).append(p)
    return stems


def row_title(line):
    body = BUILD_STAMP_RE.sub("", ID_STAMP_RE.sub("", line)).strip()
    if not (body.startswith("|") and body.endswith("|")):
        return None
    cells = [c.strip() for c in PIPE_SPLIT.split(body[1:-1])]
    return cells[0] if cells else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("matrix", help="docs/cart-boot-matrix.md (rewritten in place)")
    ap.add_argument("--roms", default=DEFAULT_ROMS,
                    help="ROM directory the sweep ran over (default: %(default)s)")
    ap.add_argument("--filedb", default=DEFAULT_FILEDB,
                    help="src/core/filedb.c to read the table from")
    ap.add_argument("--force", action="store_true",
                    help="re-stamp rows that already carry an id stamp")
    ap.add_argument("--check", action="store_true",
                    help="compare existing stamps with the files; write nothing; "
                         "exit 1 on any mismatch")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.roms):
        raise SystemExit("ROM directory %s not found" % args.roms)
    db = load_filedb(args.filedb)
    stems = rom_files_by_stem(args.roms)
    with open(args.matrix, "r", encoding="utf-8") as fh:
        lines = fh.read().split("\n")

    # Rows of the first table only; the header and separator pass through.
    in_table = False
    seen_header = False
    taken = {}
    stamped = checked = mismatched = missing = 0
    out = []
    for line in lines:
        if not line.startswith("|"):
            if in_table:
                in_table = False  # end of the first table: copy the rest
            out.append(line)
            continue
        if not seen_header:
            seen_header = in_table = True
            out.append(line)
            continue
        if not in_table or re.fullmatch(r"\|[\s:|-]+\|", line.strip()):
            out.append(line)
            continue
        title = row_title(line)
        files = stems.get(title, [])
        idx = taken.get(title, 0)
        taken[title] = idx + 1
        if idx >= len(files):
            sys.stderr.write("no file for row %r (file %d of %d)\n"
                             % (title, idx + 1, len(files)))
            missing += 1
            out.append(line)
            continue
        ident = identify(files[idx], db)
        if ident is None:
            sys.stderr.write("empty file for row %r\n" % title)
            missing += 1
            out.append(line)
            continue
        stamp = format_stamp(ident)
        have = ID_STAMP_RE.search(line)
        if args.check:
            if have:
                checked += 1
                if have.group(0) != stamp:
                    mismatched += 1
                    print("MISMATCH %s\n  row:  %s\n  file: %s"
                          % (title, have.group(0), stamp))
            out.append(line)
            continue
        if have and not args.force:
            out.append(line)
            continue
        line = ID_STAMP_RE.sub("", line).rstrip() + stamp
        stamped += 1
        out.append(line)

    if args.check:
        print("checked %d stamped rows, %d mismatched, %d rows without a file"
              % (checked, mismatched, missing))
        return 1 if mismatched else 0
    with open(args.matrix, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out))
    print("stamped %d rows in %s (%d rows without a file left alone)"
          % (stamped, args.matrix, missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
