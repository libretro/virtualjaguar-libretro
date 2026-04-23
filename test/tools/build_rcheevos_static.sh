#!/usr/bin/env bash
# Builds a static librcherros.a from the official RetroAchievements/rcheevos repo.
# Used by test_rcheevos_e2e.sh. Pin RCHEEVOS_REF (tag or SHA) for reproducibility.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${DEST:-$ROOT/build/rcheevos-static}"
TAG="${RCHEEVOS_REF:-v12.3.0}"
CC="${CC:-cc}"

mkdir -p "$DEST"

if [[ ! -f "$DEST/.extracted_${TAG}" ]]; then
   rm -rf "${DEST:?}/"* "$DEST/.extracted_"*
   TMP="$DEST/dl"
   mkdir -p "$TMP"
   echo "Downloading rcheevos ${TAG}..."
   curl -fsSL -o "$TMP/rc.tgz" "https://github.com/RetroAchievements/rcheevos/archive/refs/tags/${TAG}.tar.gz"
   tar -xzf "$TMP/rc.tgz" -C "$DEST"
   rm -rf "$TMP"
   SRC="$(echo "$DEST"/rcheevos-* | head -1)"
   mv "$SRC" "$DEST/rcheevos-src"
   touch "$DEST/.extracted_${TAG}"
fi

SRCROOT="$DEST/rcheevos-src"
OBJDIR="$DEST/obj"
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"

CFLAGS="-O2 -I${SRCROOT}/include -I${SRCROOT}/src -I${ROOT}/libretro-common/include -DRC_DISABLE_LUA -DRC_CLIENT_SUPPORTS_HASH"

echo "Compiling rcheevos sources..."
while IFS= read -r -d '' f; do
   bn="$(basename "$f" .c)"
   $CC -c $CFLAGS -o "$OBJDIR/${bn}.o" "$f"
done < <(find "$SRCROOT/src" -name '*.c' -print0)

ar rcs "$DEST/librcheevos.a" "$OBJDIR"/*.o
echo "Built $DEST/librcheevos.a"
