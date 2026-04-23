#!/usr/bin/env bash
# Builds a static librcheevos.a from the official RetroAchievements/rcheevos repo.
# Used by test_rcheevos_e2e.sh. Pin RCHEEVOS_REF to a tag or full commit SHA.
# Tarball URL: https://github.com/RetroAchievements/rcheevos/archive/${REF}.tar.gz
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${DEST:-$ROOT/build/rcheevos-static}"
REF="${RCHEEVOS_REF:-v12.3.0}"
CC="${CC:-cc}"

mkdir -p "$DEST"

CACHE_MARK="$DEST/.extracted_${REF//\//_}"

if [[ ! -f "$CACHE_MARK" ]]; then
   rm -rf "${DEST:?}/"* "${DEST:?}/".extracted_* 2>/dev/null || true
   TMP="$DEST/dl"
   mkdir -p "$TMP"
   echo "Downloading rcheevos ${REF}..."
   curl -fsSL -o "$TMP/rc.tgz" "https://github.com/RetroAchievements/rcheevos/archive/${REF}.tar.gz"
   tar -xzf "$TMP/rc.tgz" -C "$DEST"
   rm -rf "$TMP"
   SRC="$(echo "$DEST"/rcheevos-* | head -1)"
   mv "$SRC" "$DEST/rcheevos-src"
   touch "$CACHE_MARK"
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
