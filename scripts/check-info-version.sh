#!/usr/bin/env bash
# Verify the three copies of the version agree: the Makefile's
# CORE_BASE_VERSION, dist/info/virtualjaguar_libretro.info `display_version`,
# and src/core/version_fallback.h's CORE_BASE_VERSION (used when the
# generated version.h is absent).  Run on every CI build so a release can't
# ship with a stale .info field (RetroArch's "core version" UI) or a stale
# fallback header (v3.6.1 nearly did, #734).

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
INFO="$ROOT/dist/info/virtualjaguar_libretro.info"
MAKEFILE="$ROOT/Makefile"
FALLBACK="$ROOT/src/core/version_fallback.h"

for f in "$MAKEFILE" "$INFO" "$FALLBACK"; do
  if [ ! -f "$f" ]; then
    echo "::error::missing $f"
    exit 1
  fi
done

# Portable across BSD/GNU sed.
MAKE_VER=$(sed -n 's/^CORE_BASE_VERSION[[:space:]]*:*=[[:space:]]*\(.*\)/\1/p' "$MAKEFILE" | head -1)
INFO_VER=$(sed -n 's/^display_version[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$INFO" | head -1)
FALLBACK_VER=$(sed -n 's/^#define[[:space:]]*CORE_BASE_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$FALLBACK" | head -1)

if [ -z "$MAKE_VER" ]; then
  echo "::error::could not parse CORE_BASE_VERSION from $MAKEFILE"
  exit 1
fi
if [ -z "$INFO_VER" ]; then
  echo "::error::could not parse display_version from $INFO"
  exit 1
fi

if [ -z "$FALLBACK_VER" ]; then
  echo "::error::could not parse CORE_BASE_VERSION from $FALLBACK"
  exit 1
fi

if [ "$MAKE_VER" != "$INFO_VER" ]; then
  echo "::error::version mismatch: Makefile CORE_BASE_VERSION=$MAKE_VER, .info display_version=$INFO_VER"
  echo "Update dist/info/virtualjaguar_libretro.info \`display_version\` to match before tagging."
  exit 1
fi

if [ "$MAKE_VER" != "$FALLBACK_VER" ]; then
  echo "::error::version mismatch: Makefile CORE_BASE_VERSION=$MAKE_VER, src/core/version_fallback.h CORE_BASE_VERSION=$FALLBACK_VER"
  echo "Update src/core/version_fallback.h to match before tagging."
  exit 1
fi

echo "OK: Makefile = .info display_version = version_fallback.h = $MAKE_VER"
