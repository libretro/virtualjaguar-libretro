#!/bin/bash
# Build a .deb from an already-built app tree.
#
#   make-deb.sh <build-dir> <out.deb> <deb-arch> <deb-version>
#
# Layout (the app finds its data beside its executable, so the executable and the data
# live together and /usr/bin gets a symlink; SDL_GetBasePath() resolves /proc/self/exe):
#   /usr/lib/virtualjaguar/{virtualjaguar,cheats/,libretro-db/,shaders/,...}
#   /usr/bin/virtualjaguar -> ../lib/virtualjaguar/virtualjaguar
#   /usr/share/{applications,icons,metainfo,doc}
# Depends comes from dpkg-shlibdeps (libc, libcurl, ...).  SDL dlopen()s the display and
# audio stacks, which shlibdeps cannot see, so those are Recommends.
set -euo pipefail
build="$(cd "$1" && pwd)"; out="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
arch="$3"; version="$4"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
pkg="$work/pkg"; inst="$work/inst"
cmake --install "$build" --prefix "$inst"

mkdir -p "$pkg/usr/lib/virtualjaguar" "$pkg/usr/bin" "$pkg/usr/share" "$pkg/DEBIAN"
cp -a "$inst/bin/." "$pkg/usr/lib/virtualjaguar/"
ln -s ../lib/virtualjaguar/virtualjaguar "$pkg/usr/bin/virtualjaguar"
for d in applications icons metainfo; do cp -a "$inst/share/$d" "$pkg/usr/share/"; done
docdir="$pkg/usr/share/doc/virtualjaguar"
mkdir -p "$docdir"
cp -a "$inst/share/doc/virtualjaguar/." "$docdir/"
cat > "$docdir/copyright" <<'COPYRIGHT'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Virtual Jaguar
Source: https://github.com/libretro/virtualjaguar-libretro

Files: *
Copyright: Virtual Jaguar contributors
License: GPL-3
 On Debian systems the full text is in /usr/share/common-licenses/GPL-3.
 Bundled data and libraries carry their own licences; see THIRD-PARTY-NOTICES.txt
 in this directory.
COPYRIGHT
rm -f "$docdir/LICENSE.txt"   # /usr/share/common-licenses/GPL-3 covers it

# Depends from the shared libraries the executable links.
mkdir -p "$work/debian"; : > "$work/debian/control"
depends="$(cd "$work" && dpkg-shlibdeps -O -e"$pkg/usr/lib/virtualjaguar/virtualjaguar" 2>/dev/null \
           | sed -n 's/^shlibs:Depends=//p')"
[ -n "$depends" ] || { echo "dpkg-shlibdeps produced no Depends" >&2; exit 1; }

size_kb="$(du -sk "$pkg" | cut -f1)"
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: virtualjaguar
Version: $version
Section: games
Priority: optional
Architecture: $arch
Maintainer: Virtual Jaguar contributors <noreply@github.com>
Installed-Size: $size_kb
Depends: $depends, libgl1
Recommends: libasound2t64 | libasound2, libpulse0, libudev1, libxkbcommon0, libwayland-client0, libx11-6, libxext6, libxcursor1, libxi6, libxrandr2, libdecor-0-0
Homepage: https://jaguar.provenance-emu.com
Description: Atari Jaguar and Jaguar CD emulator
 Virtual Jaguar emulates the Atari Jaguar console and its CD add-on. This
 desktop app runs the same core as the RetroArch core and adds save states,
 shaders, cheats, controller remapping and a game library.
 .
 Games are not included.
CONTROL

find "$pkg" -type d -exec chmod 755 {} +
find "$pkg" -type f -exec chmod 644 {} +
chmod 755 "$pkg/usr/lib/virtualjaguar/virtualjaguar"
dpkg-deb --root-owner-group -Zxz --build "$pkg" "$out"
dpkg-deb --info "$out"
