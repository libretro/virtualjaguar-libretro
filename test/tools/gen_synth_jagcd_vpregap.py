#!/usr/bin/env python3
"""Generate the CUE/BIN source of test/roms/synth_jagcd_vpregap.chd (issue #774).

Layout (one BIN per track, the shape redump uses for Myst (USA)):

  session 1  track 01  4 silent sectors
  session 2  track 02  4 sectors, Atari boot header + bra.s * stub in sector 0
             track 03  INDEX 00 = 2 silent sectors, INDEX 01 = 4 tagged sectors
             track 04  INDEX 00 = 3 silent sectors, INDEX 01 = 4 tagged sectors
             track 05  INDEX 00 = 5 silent sectors, INDEX 01 = 4 tagged sectors

The pinned chdman records each INDEX 00 run as a VAUDIO (virtual) pregap
counted inside the track's FRAMES -- the case #755 fixed in ParseCHD.
Pregaps differ per track so an error that accumulates (the #754 shift)
lands on a wrong but non-zero tag, not just on silence.

Each tagged sector is filled with a repeating 16-byte pattern naming its
track and sector, e.g. b"T03S01 vpregap  ", so a read from the wrong place
is identifiable from the bytes alone.

Usage:
  python3 test/tools/gen_synth_jagcd_vpregap.py OUTDIR
  <pinned chdman> createcd -i OUTDIR/disc.cue \\
      -o test/roms/synth_jagcd_vpregap.chd -c none -f

Use the chdman from the jagcd-tools-*.zip release asset (pin in
tools/jagcd/CHDMAN_PIN). Homebrew/distro chdman writes no CHSE and the core
refuses the result. Keep -c none (see test/roms/README-synth-chd.txt).
"""
import os
import sys

SECTOR = 2352
STUB_LOAD_ADDR = 0x004000
STUB_LENGTH = 0x400
HDR_MAGIC_OFF = 0x42
HDR_LOAD_OFF = 0x62
HDR_LEN_OFF = 0x66
HDR_PAYLOAD = 0x6A
BOOT_MAGIC = b"ATARI APPROVED DATA HEADER ATRI "

# (track number, pregap sectors, data sectors)
TAGGED = [(3, 2, 4), (4, 3, 4), (5, 5, 4)]


def swap16(b):
    out = bytearray(len(b))
    out[0::2] = b[1::2]
    out[1::2] = b[0::2]
    return bytes(out)


def boot_track():
    sw = bytearray(4 * SECTOR)
    sw[HDR_MAGIC_OFF:HDR_MAGIC_OFF + 32] = BOOT_MAGIC
    sw[HDR_LOAD_OFF:HDR_LOAD_OFF + 4] = STUB_LOAD_ADDR.to_bytes(4, "big")
    sw[HDR_LEN_OFF:HDR_LEN_OFF + 4] = STUB_LENGTH.to_bytes(4, "big")
    for i in range(HDR_PAYLOAD, HDR_PAYLOAD + STUB_LENGTH, 2):
        sw[i] = 0x60
        sw[i + 1] = 0xFE
    # On-disc bytes are Jaguar I2S order; the extractor un-swaps them.
    return swap16(bytes(sw))


def tag_sector(track, sector):
    tag = ("T%02dS%02d vpregap  " % (track, sector)).encode("ascii")
    assert len(tag) == 16
    return tag * (SECTOR // 16)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen_synth_jagcd_vpregap.py OUTDIR")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    with open(os.path.join(out, "track01.bin"), "wb") as f:
        f.write(bytes(4 * SECTOR))
    with open(os.path.join(out, "track02.bin"), "wb") as f:
        f.write(boot_track())

    cue = [
        "REM SESSION 01",
        'FILE "track01.bin" BINARY',
        "  TRACK 01 AUDIO",
        "    INDEX 01 00:00:00",
        "REM SESSION 02",
        'FILE "track02.bin" BINARY',
        "  TRACK 02 AUDIO",
        "    INDEX 01 00:00:00",
    ]
    for track, pregap, data in TAGGED:
        name = "track%02d.bin" % track
        with open(os.path.join(out, name), "wb") as f:
            f.write(bytes(pregap * SECTOR))
            for s in range(data):
                f.write(tag_sector(track, s))
        cue += [
            'FILE "%s" BINARY' % name,
            "  TRACK %02d AUDIO" % track,
            "    INDEX 00 00:00:00",
            "    INDEX 01 00:00:%02d" % pregap,
        ]
    with open(os.path.join(out, "disc.cue"), "w") as f:
        f.write("\n".join(cue) + "\n")


if __name__ == "__main__":
    main()
