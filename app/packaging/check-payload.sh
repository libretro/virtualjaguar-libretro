#!/bin/bash
# Assert a PACKAGED payload carries everything the build tree staged beside the executable.
#
#   check-payload.sh <build-dir> <installed-exe-dir> [<exe-name>]
#
# For every directory the install sweep ships (VJ_PACKAGE_DATA_DIRS in
# app/cmake/VJPackage.cmake) that exists beside the executable in the BUILD tree, the same
# directory must exist, non-empty, beside the executable in the package.  Catches a
# packaging step that quietly drops cheats/, libretro-db/ or shaders/ (the app would just
# run without them).  Also prints what the package holds.
set -euo pipefail
build="$1"; dest="$2"; exe="${3:-virtualjaguar}"
dirs="cheats libretro-db shaders"
cfg="$build/CMakeCache.txt"
if [ -f "$cfg" ]; then
  v="$(sed -n 's/^VJ_PACKAGE_DATA_DIRS:[A-Z]*=//p' "$cfg")"
  [ -n "$v" ] && dirs="${v//;/ }"
fi
[ -f "$dest/$exe" ] || { echo "::error::$dest/$exe is missing"; exit 1; }
rc=0
for d in $dirs; do
  if [ -d "$build/$d" ]; then
    n_build="$(find "$build/$d" -type f | wc -l | tr -d ' ')"
    n_pkg=0; [ -d "$dest/$d" ] && n_pkg="$(find "$dest/$d" -type f | wc -l | tr -d ' ')"
    echo "payload $d/: build $n_build files, package $n_pkg files"
    if [ "$n_pkg" -eq 0 ] || [ "$n_pkg" -lt "$n_build" ]; then
      echo "::error::package is missing files from $d/ ($n_pkg of $n_build)"; rc=1
    fi
  else
    echo "payload $d/: not built in this configuration (skipped)"
  fi
done
exit "$rc"
