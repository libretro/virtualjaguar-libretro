#!/bin/sh
# C89 compliance lint — catches mid-block variable declarations
# that break MSVC builds. Run before committing.
#
# Usage: scripts/c89-lint.sh [file ...]
#   No args = check all source files
#   With args = check only the listed files

set -e

# libretro.c includes the generated src/core/version.h.  Make sure it
# exists before we run -fsyntax-only -- this script is invoked from CI
# and pre-commit hooks where `make` may not have run yet.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/src/core/version.h" ] || bash "$ROOT/scripts/gen-version-h.sh"

CC="${CC:-gcc}"
CFLAGS="-fsyntax-only -std=gnu89 -Werror=declaration-after-statement"
INCLUDES="-I. -Isrc -Isrc/core -Isrc/tom -Isrc/jerry -Isrc/cd -Isrc/bios -Isrc/m68000 -Ilibretro-common/include"
DEFINES='-D__LIBRETRO__ -DINLINE=inline'

skip_file() {
    case "$1" in
        src/m68000/cpu*.c|src/m68000/read*.c) return 0 ;;
        src/bios/jag*bios*.c|src/bios/jagstub*bios.c) return 0 ;;
        src/tom/blitter_simd_neon.c|src/tom/blitter_simd_sse2.c) return 0 ;;
        # Depends on rcheevos headers fetched at runtime by the e2e shell wrapper.
        test/tools/test_rcheevos_e2e.c) return 0 ;;
        # Diagnostic tools — not part of the libretro core build.
        test/tools/flicker_detect.c) return 0 ;;
    esac
    return 1
}

FAILED=0

if [ $# -gt 0 ]; then
    FILES="$@"
else
    FILES="libretro.c $(find src -name '*.c')"
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
