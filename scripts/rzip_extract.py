#!/usr/bin/env python3
"""Extract a RetroArch rzip-compressed save state to a raw one.

RetroArch writes save states through rzip_stream when "Save State Compression"
is on, which our test harnesses cannot read -- retro_unserialize sees the
compressed bytes and rejects them on size.  This converts such a file into the
plain state blob the harness expects.

Layout (little-endian), confirmed against RetroArch's rzip_stream.c:

    0   6   magic "#RZIPv"
    6   1   format version
    7   1   '#' terminator
    8   4   chunk size (uncompressed bytes per chunk)
   12   8   total uncompressed size
   20   4   length of the first compressed chunk
   24   ..  zlib chunk, then [u32 length][zlib chunk] repeating

A zero length terminates the stream.  Files without the magic are already raw
and are copied through unchanged, so this is safe to run over a whole folder.

usage: rzip_extract.py <in.state> [out.state]
"""
import sys, os, zlib, struct

MAGIC = b'#RZIPv'


def extract(src, dst):
    with open(src, 'rb') as f:
        blob = f.read()

    if not blob.startswith(MAGIC):
        with open(dst, 'wb') as f:
            f.write(blob)
        return len(blob), 'already raw'

    chunk_size, total = struct.unpack_from('<IQ', blob, 8)
    out, pos = bytearray(), 20
    while pos + 4 <= len(blob):
        (clen,) = struct.unpack_from('<I', blob, pos)
        pos += 4
        if clen == 0:
            break
        out += zlib.decompress(blob[pos:pos + clen])
        pos += clen

    if len(out) != total:
        raise SystemExit('rzip_extract: got %d bytes, header declares %d '
                         '(chunk size %d) -- format mismatch, refusing to write'
                         % (len(out), total, chunk_size))

    with open(dst, 'wb') as f:
        f.write(out)
    return len(out), 'decompressed'


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    inp = sys.argv[1]
    outp = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(inp)[0] + '.raw.state'
    n, how = extract(inp, outp)
    print('%s: %s -> %s (%d bytes)' % (os.path.basename(inp), how, outp, n))
