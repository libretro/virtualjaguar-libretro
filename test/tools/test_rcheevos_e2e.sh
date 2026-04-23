#!/usr/bin/env bash
# End-to-end check: load the core, capture SET_MEMORY_MAPS, run rcheevos rc_libretro
# memory initialization (same path RetroArch uses for Jaguar / console 17), then read
# bytes via rc_libretro_memory_read / rc_libretro_memory_find.
#
# Requires: bash, curl, cc, ar, libdl (Linux, for dlopen via -ldl)
# Usage: ./test/tools/test_rcheevos_e2e.sh path/to/virtualjaguar_libretro.{so,dylib}
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CORE="${1:?Usage: $0 path/to/core}"
export CC="${CC:-cc}"

"$ROOT/test/tools/build_rcheevos_static.sh"

DEST="${DEST:-$ROOT/build/rcheevos-static}"
LIBRC="$DEST/librcheevos.a"

if [[ "$(uname)" == Linux ]]; then
   LDFLAGS="-ldl"
else
   LDFLAGS=""
fi

$CC -O2 -Wall \
   -I"$ROOT/libretro-common/include" \
   -I"$DEST/rcheevos-src/include" \
   -I"$DEST/rcheevos-src/src" \
   -o "$ROOT/build/test_rcheevos_e2e" \
   "$ROOT/test/tools/test_rcheevos_e2e.c" \
   "$LIBRC" \
   $LDFLAGS

"$ROOT/build/test_rcheevos_e2e" "$CORE"
