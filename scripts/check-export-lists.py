#!/usr/bin/env python3
"""Assert the two wide-test-ABI symbol lists stay mirrors of each other.

The same test ABI is spelled twice, because the two linkers disagree about
how to express it:

  link-test.T        GNU ld (Linux, Windows/MSYS2, ARM, ...) via
                     --version-script.  Bare, semicolon terminated:
                         TitleDB*;
  exports-test.list  Mach-O ld64 (macOS, iOS, tvOS) via
                     -exported_symbols_list.  Carries the leading
                     underscore the Mach-O ABI adds:
                         _TitleDB*

A symbol added to one file and not the other links and passes CI on the
platform that got it and is hidden on the other, where harness_dlsym then
returns NULL.  That is how test_pertitle_db --case 6 reached develop
dlsym'ing a TitleDBTitleName no platform exported: nothing compared the two
files, so nothing said so.

Usage: python3 scripts/check-export-lists.py   (non-zero exit on divergence)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GNU = os.path.join(ROOT, "link-test.T")
MACHO = os.path.join(ROOT, "exports-test.list")


def gnu_symbols(path):
    """Symbols in the global: .. local: body of a GNU version script."""
    text = open(path, errors="replace").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)   # strip /* comments */
    m = re.search(r"global:(.*?)local:", text, flags=re.S)
    if not m:
        sys.stderr.write("check-export-lists: no global:/local: block in %s\n" % path)
        sys.exit(1)
    return {s.strip() for s in m.group(1).split(";") if s.strip()}


def macho_symbols(path):
    """Symbols in a Mach-O -exported_symbols_list, minus the ABI underscore."""
    out = set()
    for line in open(path, errors="replace"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.add(line[1:] if line.startswith("_") else line)
    return out


def main():
    for path in (GNU, MACHO):
        if not os.path.isfile(path):
            sys.stderr.write("check-export-lists: missing %s\n" % path)
            return 1

    gnu = gnu_symbols(GNU)
    macho = macho_symbols(MACHO)

    only_macho = sorted(macho - gnu)
    only_gnu = sorted(gnu - macho)

    if not only_macho and not only_gnu:
        print("export lists in sync (%d symbols)" % len(gnu))
        return 0

    sys.stderr.write("ERROR: the wide test ABI lists have diverged.\n")
    if only_macho:
        sys.stderr.write(
            "  in exports-test.list (Mach-O) but NOT link-test.T (GNU ld):\n")
        for s in only_macho:
            sys.stderr.write("    _%s\n" % s)
        sys.stderr.write(
            "  -> hidden on Linux/Windows; harness dlsym returns NULL there.\n")
    if only_gnu:
        sys.stderr.write(
            "  in link-test.T (GNU ld) but NOT exports-test.list (Mach-O):\n")
        for s in only_gnu:
            sys.stderr.write("    %s\n" % s)
        sys.stderr.write(
            "  -> hidden on macOS/iOS/tvOS; harness dlsym returns NULL there.\n")
    sys.stderr.write(
        "  Add the symbol to both files (Mach-O entries take a leading '_').\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
