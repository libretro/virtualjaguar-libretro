#!/usr/bin/env python3
"""Build a minimal ELF64 file shaped like a signed AppImage, for the tests of
app/packaging/linux/verify-appimage.py.

  fake_appimage.py <out> [--sig sig.asc] [--key pub.asc] [--payload-byte N]

Sections: .sha256_sig (1 KiB) and .sig_key (8 KiB), zero-filled unless --sig /
--key give their contents, exactly as appimagetool reserves and fills them.
`--digest-only` prints the SHA-256 the signature must cover (the file with
both sections zeroed) for an unsigned image built with the same arguments.
"""
import argparse
import hashlib
import struct
import sys

SIG_SIZE = 1024
KEY_SIZE = 8192


def build(sig=b"", key=b"", payload_byte=0x41):
    body = bytes([payload_byte]) * 4096          # stands in for runtime + squashfs
    names = b"\0.sha256_sig\0.sig_key\0.shstrtab\0"
    off_body = 64
    off_sig = off_body + len(body)
    off_key = off_sig + SIG_SIZE
    off_str = off_key + KEY_SIZE
    off_sh = off_str + len(names)
    off_sh += (-off_sh) % 8
    data = bytearray()
    data += body
    data += sig.ljust(SIG_SIZE, b"\0")
    data += key.ljust(KEY_SIZE, b"\0")
    data += names
    data += b"\0" * (off_sh - off_str - len(names))

    def sh(name, typ, off, size):
        return struct.pack("<IIQQQQIIQQ", name, typ, 0, 0, off, size, 0, 0, 1, 0)
    shdrs = sh(0, 0, 0, 0)
    shdrs += sh(1, 1, off_sig, SIG_SIZE)
    shdrs += sh(13, 1, off_key, KEY_SIZE)
    shdrs += sh(22, 3, off_str, len(names))
    ehdr = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\0" * 8
    ehdr += struct.pack("<HHIQQQIHHHHHH", 2, 62, 1, 0, 0, off_sh, 0, 64, 56, 0, 64, 4, 3)
    assert len(ehdr) == 64
    return bytes(ehdr) + bytes(data) + shdrs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--sig")
    ap.add_argument("--key")
    ap.add_argument("--payload-byte", type=int, default=0x41)
    ap.add_argument("--digest-only", action="store_true")
    a = ap.parse_args()
    if a.digest_only:
        print(hashlib.sha256(build(payload_byte=a.payload_byte)).hexdigest())
        return 0
    sig = open(a.sig, "rb").read() if a.sig else b""
    key = open(a.key, "rb").read() if a.key else b""
    with open(a.out, "wb") as f:
        f.write(build(sig, key, a.payload_byte))
    return 0


if __name__ == "__main__":
    sys.exit(main())
