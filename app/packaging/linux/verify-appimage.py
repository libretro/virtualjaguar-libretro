#!/usr/bin/env python3
"""Verify the GPG signature embedded in an AppImage (issue #1151).

  verify-appimage.py <file.AppImage> [--pubkey KEY.asc] [--json]

`appimagetool --sign` signs the SHA-256 of the AppImage as it was before the
signature was embedded, i.e. with the `.sha256_sig` and `.sig_key` ELF sections
zero-filled, and stores the ASCII-armored detached signature of that hex digest
in `.sha256_sig` and the signer's public key in `.sig_key`.  appimagetool 1.9
ships no verifier, so this does what AppImageKit's `validate` did, with the
standard library plus `gpg`: parse the section headers, re-hash the file with
the two sections zeroed, and `gpg --verify` the signature over the digest.

With --pubkey the signature must come from a key in that file (the project's
committed release key), not merely from the key embedded in the AppImage.

Exit codes: 0 verified; 1 not signed; 2 bad signature; 3 signed by another key;
4 gpg missing or the file is not an AppImage.  Runs on Linux and macOS.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SIG_SECTION = ".sha256_sig"
KEY_SECTION = ".sig_key"


def elf_sections(path):
    """{name: (offset, size)} of every section header in an ELF32/64 file."""
    with open(path, "rb") as f:
        ident = f.read(16)
        if ident[:4] != b"\x7fELF":
            raise ValueError("not an ELF file")
        bits = ident[4]
        end = "<" if ident[5] == 1 else ">"
        if bits == 2:
            f.seek(0x28)
            shoff = struct.unpack(end + "Q", f.read(8))[0]
            f.seek(0x3A)
            shentsize, shnum, shstrndx = struct.unpack(end + "HHH", f.read(6))
            fmt = end + "IIQQQQIIQQ"
        elif bits == 1:
            f.seek(0x20)
            shoff = struct.unpack(end + "I", f.read(4))[0]
            f.seek(0x2E)
            shentsize, shnum, shstrndx = struct.unpack(end + "HHH", f.read(6))
            fmt = end + "IIIIIIIIII"
        else:
            raise ValueError("bad ELF class")
        if shoff == 0 or shnum == 0:
            raise ValueError("no section headers")
        headers = []
        for i in range(shnum):
            f.seek(shoff + i * shentsize)
            h = struct.unpack(fmt, f.read(struct.calcsize(fmt)))
            # (name, type, flags, addr, offset, size, ...)
            headers.append((h[0], h[4], h[5]))
        _n, stroff, strsize = headers[shstrndx]
        f.seek(stroff)
        strtab = f.read(strsize)
    out = {}
    for name_off, off, size in headers:
        endn = strtab.find(b"\0", name_off)
        out[strtab[name_off:endn].decode("ascii", "replace")] = (off, size)
    return out


def section_text(path, span):
    off, size = span
    with open(path, "rb") as f:
        f.seek(off)
        return f.read(size).rstrip(b"\0").decode("utf-8", "replace")


def digest_with_zeroed(path, spans):
    """SHA-256 hex of the file with every (offset, size) span read as zeros."""
    h = hashlib.sha256()
    spans = sorted(spans)
    pos = 0
    with open(path, "rb") as f:
        for off, size in spans:
            while pos < off:
                chunk = f.read(min(1 << 20, off - pos))
                if not chunk:
                    break
                h.update(chunk)
                pos += len(chunk)
            h.update(b"\0" * size)
            f.seek(off + size)
            pos = off + size
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def key_fingerprints(gpg, home, keyfile):
    r = subprocess.run([gpg, "--homedir", home, "--batch", "--with-colons",
                        "--import-options", "show-only", "--import", keyfile],
                       capture_output=True, text=True)
    return [l.split(":")[9] for l in r.stdout.splitlines() if l.startswith("fpr:")]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("appimage")
    ap.add_argument("--pubkey", help="armored public key the signature must come from")
    ap.add_argument("--json", action="store_true", help="machine-readable result")
    a = ap.parse_args()

    res = {"file": os.path.basename(a.appimage), "signed": False, "verified": False,
           "fingerprint": "", "matches_pubkey": None, "detail": ""}

    def finish(code, detail):
        res["detail"] = detail
        if a.json:
            print(json.dumps(res))
        else:
            print("%s: %s" % (res["file"], detail))
        return code

    try:
        secs = elf_sections(a.appimage)
    except (ValueError, struct.error, OSError) as e:
        return finish(4, "not an AppImage (%s)" % e)
    if SIG_SECTION not in secs or KEY_SECTION not in secs:
        return finish(1, "no signature sections (not built by appimagetool with --sign)")
    sig = section_text(a.appimage, secs[SIG_SECTION])
    if "BEGIN PGP SIGNATURE" not in sig:
        return finish(1, "not signed (empty %s section)" % SIG_SECTION)
    res["signed"] = True
    key = section_text(a.appimage, secs[KEY_SECTION])

    gpg = shutil.which("gpg2") or shutil.which("gpg")
    if not gpg:
        return finish(4, "signed, but gpg is not installed so the signature cannot be checked")

    digest = digest_with_zeroed(a.appimage, [secs[SIG_SECTION], secs[KEY_SECTION]])
    home = tempfile.mkdtemp(prefix="vj-appimage-verify.")
    os.chmod(home, 0o700)
    try:
        # Verify against every key we know of (the embedded one and the project
        # key), THEN check which one made the signature: a signature by the
        # wrong key must read "another key", not "bad".
        keyfile = os.path.join(home, "project.asc")
        embedded = os.path.join(home, "embedded.asc")
        if a.pubkey:
            shutil.copyfile(a.pubkey, keyfile)
            subprocess.run([gpg, "--homedir", home, "--batch", "--quiet", "--import", keyfile],
                           capture_output=True, text=True)
        if "BEGIN PGP PUBLIC KEY BLOCK" in key:
            with open(embedded, "w") as f:
                f.write(key)
            subprocess.run([gpg, "--homedir", home, "--batch", "--quiet", "--import", embedded],
                           capture_output=True, text=True)
        elif not a.pubkey:
            return finish(2, "signed, but no key embedded and no --pubkey given")
        sigfile = os.path.join(home, "sig.asc")
        datafile = os.path.join(home, "digest.txt")
        with open(sigfile, "w") as f:
            f.write(sig)
        with open(datafile, "w") as f:
            f.write(digest)          # the hex string, no newline: that is what was signed
        r = subprocess.run([gpg, "--homedir", home, "--batch", "--status-fd", "1",
                            "--verify", sigfile, datafile], capture_output=True, text=True)
        fpr = ""
        for line in r.stdout.splitlines():
            if line.startswith("[GNUPG:] VALIDSIG "):
                fpr = line.split()[2]
        if r.returncode != 0 or not fpr:
            return finish(2, "BAD signature over digest %s" % digest)
        res["verified"] = True
        res["fingerprint"] = fpr
        if a.pubkey:
            allowed = key_fingerprints(gpg, home, keyfile)
            res["matches_pubkey"] = fpr in allowed
            if not res["matches_pubkey"]:
                return finish(3, "good signature, but from %s, not the project key (%s)"
                              % (fpr, ", ".join(allowed) or "none"))
        return finish(0, "good signature from %s (digest %s)" % (fpr, digest))
    finally:
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
