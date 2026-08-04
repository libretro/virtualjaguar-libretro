#!/usr/bin/env python3
"""Classify the A/B difference between two fb_row_digest dumps.

Written for the AvP "brown bar" sweep (#178).  The fix under test must only
ever change rows the core previously left unwritten, and must only ever
change them to opaque black.  This checks that mechanically so a 46-title
sweep does not depend on reading 46 screenshots.

VJFBDIG3 adds a per-frame overscan column-band digest (columns x >= 320).
Both inputs must be VJFBDIG3; VJFBDIG2 dumps exit 2 with a clear error.

Checks, in order of severity:
  1. Geometry must match frame for frame.  Any difference is a hard failure.
  2. Per-frame audio digests must match.  A video-window change cannot alter
     audio; any difference is a hard failure.
  3. Every row whose hash differs must be >= the patched build's written-row
     extent for that frame, and its patched hash must equal the all-opaque-
     black hash for that width.  Anything else is a hard failure.
  4. Overscan band_hash must match frame for frame (unless --ignore-band).

Usage:  fb_row_diff.py <stock.bin> <patched.bin> [--label NAME] [--ignore-band]
Exit:   0 = identical or only expected differences, 1 = unexpected difference,
        2 = usage/format error.
"""

import struct
import sys

MAGIC = b"VJFBDIG3"
HDR = struct.Struct("<4I")          # width, height, frame_hash, written_extent
BAND = struct.Struct("<6I")         # x0, width, hash, nonblack, first_x, first_y
NO_EXTENT = 0xFFFFFFFF


def fnv1a(data, h=2166136261):
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


_black_cache = {}


def black_row_hash(width):
    """Hash of a row of `width` opaque-black XRGB8888 pixels, in the same
    little-endian byte order fb_row_digest hashes the framebuffer in."""
    if width not in _black_cache:
        _black_cache[width] = fnv1a(b"\x00\x00\x00\xff" * width)
    return _black_cache[width]


def load(path):
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:8] != MAGIC:
        raise ValueError(f"{path}: bad magic {blob[:8]!r} (need VJFBDIG3)")
    nvideo, naudio = struct.unpack_from("<2I", blob, 8)
    frames = []
    off = 16
    for _ in range(nvideo):
        w, h, fh, extent = HDR.unpack_from(blob, off)
        off += HDR.size
        rows = struct.unpack_from(f"<{h}I", blob, off)
        off += 4 * h
        band = BAND.unpack_from(blob, off)
        off += BAND.size
        frames.append((w, h, fh, extent, rows, band))
    audio = struct.unpack_from(f"<{naudio}I", blob, off) if naudio else ()
    return frames, list(audio)


def main():
    argv = sys.argv[1:]
    label = "run"
    ignore_band = False
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--label" and i + 1 < len(argv):
            label = argv[i + 1]
            i += 2
        elif argv[i] == "--ignore-band":
            ignore_band = True
            i += 1
        elif argv[i].startswith("--"):
            i += 1
        else:
            args.append(argv[i])
            i += 1
    if len(args) != 2:
        print(__doc__)
        return 2

    try:
        frames_a, audio_a = load(args[0])
        frames_b, audio_b = load(args[1])
    except ValueError as e:
        print(f"FAIL {label}: {e}", file=sys.stderr)
        return 2

    unexpected = []
    changed_frames = 0
    changed_rows = set()
    first_changed_frame = None
    band_changed_frames = 0
    max_band_nonblack = 0
    any_band_nonblack = False

    gap_frames = sum(1 for (_w, h, _fh, ext, _r, _b) in frames_b
                     if ext != NO_EXTENT and ext < h)
    extents = [ext for (_w, _h, _fh, ext, _r, _b) in frames_b if ext != NO_EXTENT]
    extent_changes = sum(1 for a_, b_ in zip(extents, extents[1:]) if a_ != b_)

    if len(frames_a) != len(frames_b):
        unexpected.append((-1, -1,
                           f"frame count {len(frames_a)} != {len(frames_b)}"))
    if audio_a != audio_b:
        n = sum(1 for x, y in zip(audio_a, audio_b) if x != y)
        first = next((k for k, (x, y) in enumerate(zip(audio_a, audio_b))
                      if x != y), None)
        unexpected.append((-1, -1, f"audio differs in {n} frames, "
                                   f"first at audio frame {first}"))

    for n, (fa, fb_) in enumerate(zip(frames_a, frames_b)):
        wa, ha, fha, _exta, rowsa, banda = fa
        wb, hb, fhb, extb, rowsb, bandb = fb_

        if bandb[3] > max_band_nonblack:
            max_band_nonblack = bandb[3]
        if bandb[3] > 0:
            any_band_nonblack = True

        if (wa, ha) != (wb, hb):
            unexpected.append((n, -1, f"geometry {wa}x{ha} -> {wb}x{hb}"))
            continue

        if banda[2] != bandb[2]:
            band_changed_frames += 1
            if not ignore_band:
                unexpected.append((n, -1, f"overscan band_hash differs "
                                          f"({banda[2]:08X} -> {bandb[2]:08X})"))

        if fha == fhb:
            continue

        changed_frames += 1
        if first_changed_frame is None:
            first_changed_frame = n

        blk = black_row_hash(wb)
        for r in range(ha):
            if rowsa[r] == rowsb[r]:
                continue
            changed_rows.add(r)
            if extb != NO_EXTENT and r < extb:
                unexpected.append((n, r, f"row < written extent {extb}"))
            elif rowsb[r] != blk:
                unexpected.append((n, r, "patched row is not opaque black"))

    ok = not unexpected
    rows_desc = (f"{min(changed_rows)}-{max(changed_rows)}"
                 if changed_rows else "none")
    print(f"{'OK  ' if ok else 'FAIL'} {label}: frames={len(frames_a)} "
          f"gap_frames={gap_frames} extent_changes={extent_changes} "
          f"changed_frames={changed_frames} changed_rows={rows_desc} "
          f"first_changed_frame={first_changed_frame} "
          f"band_changed_frames={band_changed_frames} "
          f"max_band_nonblack={max_band_nonblack} "
          f"band_nonblack_any={int(any_band_nonblack)} "
          f"unexpected={len(unexpected)}")
    for frame, row, why in unexpected[:10]:
        print(f"       frame {frame} row {row}: {why}")
    if len(unexpected) > 10:
        print(f"       ... {len(unexpected) - 10} more")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
