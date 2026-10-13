#!/bin/bash
# Assemble "Virtual Jaguar.app" from an install tree (`cmake --install --prefix <dir>`).
#
#   make-app.sh <prefix> <out.app> <short-version> <build-version> <min-macos>
#
# SDL_GetBasePath() in a .app is Contents/Resources/, and the app looks for its bundled
# data beside that (cheats/, libretro-db/, shaders/), so:
#   Contents/MacOS/virtualjaguar          the executable
#   Contents/Resources/<bin/* data>       cheats, libretro-db, shaders, licence, notices
#   Contents/Frameworks/librashader.dylib the shader runtime (the app also probes
#                                         <base>/../Frameworks/)
#   Contents/Resources/AppIcon.icns
set -euo pipefail
prefix="$1"; app="$2"; shortv="$3"; buildv="$4"; minmac="$5"
here="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks"
cp "$prefix/bin/virtualjaguar" "$app/Contents/MacOS/virtualjaguar"
for f in "$prefix"/bin/*; do
  base="$(basename "$f")"
  case "$base" in
    virtualjaguar) ;;
    *.dylib) cp -a "$f" "$app/Contents/Frameworks/" ;;
    *) cp -a "$f" "$app/Contents/Resources/" ;;
  esac
done
cp "$prefix"/share/doc/virtualjaguar/LICENSE.txt "$prefix"/share/doc/virtualjaguar/THIRD-PARTY-NOTICES.txt \
   "$app/Contents/Resources/"
cp "$here/../icons/virtualjaguar.icns" "$app/Contents/Resources/AppIcon.icns"
sed -e "s|@SHORT_VERSION@|$shortv|" -e "s|@BUILD_VERSION@|$buildv|" -e "s|@MIN_MACOS@|$minmac|" \
    "$here/Info.plist.in" > "$app/Contents/Info.plist"
plutil -lint "$app/Contents/Info.plist"
echo "assembled $app"
