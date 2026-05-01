#!/usr/bin/env bash
# Verify dist/info/virtualjaguar_libretro.info `display_version` matches
# the Makefile's CORE_BASE_VERSION.  Run on every CI build so a release
# can't ship with a stale .info field that would mislead RetroArch's
# "core version" UI.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
INFO="$ROOT/dist/info/virtualjaguar_libretro.info"
MAKEFILE="$ROOT/Makefile"

if [ ! -f "$INFO" ]; then
  echo "::error::missing $INFO"
  exit 1
fi

# Portable across BSD/GNU sed.
MAKE_VER=$(sed -n 's/^CORE_BASE_VERSION[[:space:]]*:*=[[:space:]]*\(.*\)/\1/p' "$MAKEFILE" | head -1)
INFO_VER=$(sed -n 's/^display_version[[:space:]]*=[[:space:]]*"\(.*\)"/\1/p' "$INFO" | head -1)

if [ -z "$MAKE_VER" ]; then
  echo "::error::could not parse CORE_BASE_VERSION from $MAKEFILE"
  exit 1
fi
if [ -z "$INFO_VER" ]; then
  echo "::error::could not parse display_version from $INFO"
  exit 1
fi

if [ "$MAKE_VER" != "$INFO_VER" ]; then
  echo "::error::version mismatch: Makefile CORE_BASE_VERSION=$MAKE_VER, .info display_version=$INFO_VER"
  echo "Update dist/info/virtualjaguar_libretro.info \`display_version\` to match before tagging."
  exit 1
fi

echo "OK: CORE_BASE_VERSION = display_version = $MAKE_VER"
