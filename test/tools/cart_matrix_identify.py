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
already there against the files, field by field, so a probe-stamped matrix
can be audited against this independent computation.  Both modes exit 1 when
any row's file is missing or empty, and `--check` also when it verified
nothing or found a mismatch: an audit that could not look at the files must
never read as a pass.

What is replicated from the core, and where it can differ from the probe:
  * the prepended copier header rule (DetectPrependedHeaderSize in
    src/core/file.c): N MiB + 512 (or a sub-1-MiB image whose $400 marker
    sits 512 bytes in) is stripped before the CRC, as the core does;
  * byte 0 as the loader maps it, following ParseFileType's order: an
    ABS/COF/JAGR/DRI header is a RAM-loaded program and the cart slot stays
    untouched ($00); an image with the $400 universal marker is a cartridge
    (its own byte 0); an Alpine image (size + 8192 a whole MiB, or the
    "$802000 header stub" shape) sits behind the loader's $FF fill.  Two
    shapes depend on the loader's raw-binary scorer, which is not ported:
    an exact-MiB image without the marker (cartridge, or Alpine when the
    scorer places it at $802000: Rayman Demo, #973) and a headerless image
    that is neither (RAM binary / BJL image, or the sub-1-MiB cartridge
    fallback).  For those the row's own BIOS column decides: a run in which
    the boot ROM ran (BIOS_REJECT, bios_trap, a handoff in the notes) was a
    cartridge mapping, one that scored without a boot phase (bios_ran=0)
    was not; when the column says neither, the field is left out and the
    site falls back to the stage-only wording.  `--check` skips a field the
    backfill left out.
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
MEMTRACK_SIZE = 131072

ID_STAMP_RE = re.compile(r"<!--\s*id:\s*(.*?)\s*-->")
ID_FIELD_RE = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)')
HTML_COMMENT_RE = re.compile(r"<!--.*?-->")
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


def has_marker(data):
    return (len(data) >= CART_UNIVERSAL_MARKER_OFFSET + 8
            and marker_at(data, CART_UNIVERSAL_MARKER_OFFSET))


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


def alpine_header_stub(data):
    """Port of LooksLikeAlpineHeaderStub: two or more MOVE.L/MOVEA.L abs.L
    reads of $802000..$8020FF in the first $40 bytes."""
    size = len(data)
    window = size if size < 0x100 else 0x100
    hits = 0
    for off in range(0, 0x40, 2):
        if off + 6 > size:
            break
        op = (data[off] << 8) | data[off + 1]
        if (op & 0xF1FF) != 0x2039 and (op & 0xF1FF) != 0x2079:
            continue
        target = int.from_bytes(data[off + 2:off + 6], "big")
        if 0x802000 <= target < 0x802000 + window:
            hits += 1
    return hits >= 2


def ram_program_header(data):
    """ParseFileType's header checks: ABS/COF type 1 (601B), type 2 (0150),
    and the 601A family (Jaguar Server, DRI, the legacy layout)."""
    if len(data) < 2:
        return False
    return ((data[0] == 0x60 and data[1] in (0x1B, 0x1A))
            or (data[0] == 0x01 and data[1] == 0x50))


def boot_rom_ran(bios_stage, bios_notes):
    """What the row's BIOS column says about the boot phase: True when the
    boot ROM ran (so the image was mapped as a cartridge), False when the
    run had none (bios_ran=0, cart_classify.sh), None when it says nothing
    (LOAD_FAIL, timeouts, infrastructure verdicts)."""
    s = bios_stage.strip()
    if s == "BIOS_REJECT" or s.startswith("? (bios_trap"):
        return True
    if "handoff" in bios_notes:
        return True
    if s == "GAME_CODE" or s.startswith("? (trap_loop"):
        return False
    return None


def mapped_byte0(data, ran):
    """Byte 0 at $800000 after the loader mapped `data` (the stripped
    payload), or None when the mapping cannot be told from the bytes and
    the row (see the module docstring)."""
    size = len(data)
    if ram_program_header(data):
        return 0x00
    marker = has_marker(data)
    if size % MIB == 0 or size == MEMTRACK_SIZE:
        if marker:
            return data[0]
        # JST_ROM, or JST_ALPINE ($FF fill) when the raw scorer places the
        # image at $802000 (Rayman Demo, #973).
        if data[0] == 0xFF:
            return 0xFF
        if ran is True:
            return data[0]
        if ran is False:
            return 0xFF
        return None
    if marker:
        return data[0]
    if (size + 8192) % MIB == 0 or alpine_header_stub(data):
        return 0xFF
    # Raw binary / BJL image (slot untouched) or the sub-1-MiB cartridge
    # fallback (its own byte 0).
    if ran is True:
        return data[0]
    if ran is False:
        return 0x00
    return None


def identify(path, db, bios_cell=("", "")):
    """-> dict(size, crc32, hdr_byte0 (int or None), entry, sibling), or
    None for an empty file."""
    with open(path, "rb") as fh:
        data = fh.read()
    data = data[prepended_header_size(data):]
    if not data:
        return None
    crc = zlib.crc32(data) & 0xFFFFFFFF
    entry = db.get(crc)
    return {"size": len(data), "crc32": crc,
            "hdr_byte0": mapped_byte0(data, boot_rom_ran(*bios_cell)),
            "entry": entry,
            "sibling": verified_sibling(db, crc) if entry else 0}


def format_fields(ident):
    """The probe's identity tail, byte for byte; hdr_byte0 left out when
    the backfill could not tell the mapping."""
    s = " size=%d crc32=$%08X" % (ident["size"], ident["crc32"])
    if ident["hdr_byte0"] is not None:
        s += " hdr_byte0=$%02X" % ident["hdr_byte0"]
    s += " filedb=%d" % (1 if ident["entry"] else 0)
    if ident["entry"]:
        name, flags = ident["entry"]
        s += ' filedb_flags=$%03X filedb_name="%s" filedb_verified_sibling=$%08X' % (
            flags, name, ident["sibling"])
    return s


def format_stamp(ident):
    return "<!-- id:%s -->" % format_fields(ident)


def parse_fields(text):
    return dict(ID_FIELD_RE.findall(text))


def compare_stamp(row_stamp, computed_fields):
    """-> list of 'key: row=... file=...' differences.  Every field the
    backfill computed must match; a field it left out (hdr_byte0 for an
    undecidable mapping) is skipped."""
    have = parse_fields(row_stamp)
    want = parse_fields(computed_fields)
    diffs = []
    for key, val in want.items():
        if have.get(key) != val:
            diffs.append("%s: row=%s file=%s" % (key, have.get(key, "(absent)"), val))
    for key in have:
        if key not in want and key != "hdr_byte0":
            diffs.append("%s: row=%s file=(absent)" % (key, have[key]))
    return diffs


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


def row_cells(line):
    body = HTML_COMMENT_RE.sub("", line).strip()
    if not (body.startswith("|") and body.endswith("|")):
        return None
    return [c.strip() for c in PIPE_SPLIT.split(body[1:-1])]


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
                         "exit 1 on a mismatch, a missing file, or nothing checked")
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
    stamped = checked = mismatched = missing = undecided = 0
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
        cells = row_cells(line)
        title = cells[0] if cells else ""
        bios_cell = (cells[3], cells[4]) if cells and len(cells) >= 5 else ("", "")
        files = stems.get(title, [])
        idx = taken.get(title, 0)
        taken[title] = idx + 1
        if idx >= len(files):
            sys.stderr.write("no file for row %r (file %d of %d)\n"
                             % (title, idx + 1, len(files)))
            missing += 1
            out.append(line)
            continue
        ident = identify(files[idx], db, bios_cell)
        if ident is None:
            sys.stderr.write("empty file for row %r\n" % title)
            missing += 1
            out.append(line)
            continue
        if ident["hdr_byte0"] is None:
            undecided += 1
        fields = format_fields(ident)
        have = ID_STAMP_RE.search(line)
        if args.check:
            if have:
                checked += 1
                diffs = compare_stamp(have.group(1), fields)
                if diffs:
                    mismatched += 1
                    print("MISMATCH %s\n  %s" % (title, "\n  ".join(diffs)))
            out.append(line)
            continue
        if have and not args.force:
            out.append(line)
            continue
        line = ID_STAMP_RE.sub("", line).rstrip() + "<!-- id:%s -->" % fields
        stamped += 1
        out.append(line)

    if args.check:
        print("checked %d stamped rows, %d mismatched, %d rows without a file, "
              "%d with hdr_byte0 left to the probe"
              % (checked, mismatched, missing, undecided))
        if checked == 0:
            print("FAIL: nothing was checked")
        return 1 if (mismatched or missing or checked == 0) else 0
    with open(args.matrix, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out))
    print("stamped %d rows in %s (%d rows without a file left alone, "
          "%d with hdr_byte0 left to the probe)"
          % (stamped, args.matrix, missing, undecided))
    if missing:
        print("FAIL: %d rows could not be stamped" % missing)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
