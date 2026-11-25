"""Wrap this sample's flat image in a Jaguar ABS header with its text symbols."""

import struct
import subprocess
import sys
from pathlib import Path


def main():
    nm, elf, image_path, output = sys.argv[1:]
    wanted = {"_start", "drawmark"}
    addresses = {}
    for line in subprocess.check_output([nm, "-P", elf], text=True).splitlines():
        name, kind, address, *_ = line.split()
        if name in wanted and kind == "T":
            addresses[name] = int(address, 16)
    if addresses.keys() != wanted:
        raise SystemExit("Expected _start and drawmark text symbols in the sample ELF")

    image = Path(image_path).read_bytes()
    load_address = addresses["_start"]
    if not load_address <= addresses["drawmark"] < load_address + len(image):
        raise SystemExit("drawmark lies outside the image")

    # DRI absolute executable, with the Jaguar 36-byte header and 14-byte
    # symbol records. 0x8200 denotes a defined text symbol.
    symbols = b"".join(
        struct.pack(">8sHI", name.encode("ascii"), 0x8200, addresses[name])
        for name in sorted(wanted)
    )
    header = bytearray(36)
    struct.pack_into(">HIIII", header, 0, 0x601B, len(image), 0, 0, len(symbols))
    struct.pack_into(">I", header, 0x16, load_address)
    Path(output).write_bytes(header + image + symbols)


if __name__ == "__main__":
    main()
