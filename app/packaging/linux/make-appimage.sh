#!/bin/bash
# Build an AppImage from an already-built app tree.
#
#   make-appimage.sh <build-dir> <out.AppImage> <x86_64|aarch64>
#
# linuxdeploy (pinned) gathers the shared libraries the executable needs; appimagetool
# (pinned) squashes the AppDir.  SDL3 is linked statically and dlopen()s the host's X11,
# Wayland, PulseAudio/ALSA and GL, which is what an AppImage must NOT bundle.
# Neither tool needs FUSE here: they run with APPIMAGE_EXTRACT_AND_RUN=1.
set -euo pipefail
build="$(cd "$1" && pwd)"; out="$2"; arch="$3"
id=io.github.libretro.VirtualJaguar

# Pins.  Digests are GitHub's own asset digests for the release tag.
LINUXDEPLOY_TAG=1-alpha-20251107-1
APPIMAGETOOL_TAG=1.9.1
case "$arch" in
  x86_64)
    LD_SHA=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
    AT_SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 ;;
  aarch64)
    LD_SHA=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
    AT_SHA=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158 ;;
  *) echo "unsupported arch $arch" >&2; exit 2 ;;
esac

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
fetch() {  # url sha dest
  curl -fsSL --retry 3 -o "$3" "$1"
  echo "$2  $3" | sha256sum -c - >/dev/null || { echo "digest mismatch for $1" >&2; exit 1; }
  chmod +x "$3"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$arch.AppImage" \
      "$LD_SHA" "$work/linuxdeploy"
fetch "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-$arch.AppImage" \
      "$AT_SHA" "$work/appimagetool"

export APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch"
appdir="$work/AppDir"
cmake --install "$build" --prefix "$appdir/usr"

"$work/linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/virtualjaguar" \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png"

# The AppImage's own app-data validation is advisory; the files are checked in CI.
"$work/appimagetool" --no-appstream "$appdir" "$out"
chmod +x "$out"
ls -la "$out"
