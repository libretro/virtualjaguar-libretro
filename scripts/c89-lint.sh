#!/bin/sh
# C89 compliance lint — catches mid-block variable declarations
# that break MSVC builds. Run before committing.
#
# Usage: scripts/c89-lint.sh [file ...]
#   No args = check all source files
#   With args = check only the listed files

set -e

CC="${CC:-gcc}"
CFLAGS="-fsyntax-only -std=gnu89 -Werror=declaration-after-statement"
INCLUDES="-I. -Isrc -Isrc/m68000 -Ilibretro-common/include"
DEFINES='-D__LIBRETRO__ -DINLINE=inline'

skip_file() {
    case "$1" in
        src/m68000/cpu*.c|src/m68000/read*.c) return 0 ;;
        src/jag*bios*.c|src/jagstub*bios.c) return 0 ;;
        src/blitter_simd_neon.c|src/blitter_simd_sse2.c) return 0 ;;
    esac
    return 1
}

FAILED=0

if [ $# -gt 0 ]; then
    FILES="$@"
else
    FILES="libretro.c $(find src -maxdepth 1 -name '*.c') src/m68000/m68kinterface.c"
fi

for f in $FILES; do
    [ -f "$f" ] || continue
    case "$f" in *.c) ;; *) continue ;; esac
    if skip_file "$f"; then continue; fi

    if ! $CC $CFLAGS $INCLUDES $DEFINES "$f" 2>&1; then
        FAILED=1
    fi
done

if [ "$FAILED" = "1" ]; then
    echo "C89 lint FAILED — fix mid-block declarations"
    exit 1
fi
echo "C89 lint passed"
