#!/bin/bash
# Regenerates the committed raster icons from virtualjaguar.svg.
# Needs rsvg-convert, magick (ImageMagick) and, for the .icns, macOS iconutil.
set -euo pipefail
cd "$(dirname "$0")"
svg=virtualjaguar.svg
mkdir -p png
for s in 16 32 48 64 128 256 512; do
  rsvg-convert -w "$s" -h "$s" "$svg" -o "png/virtualjaguar-$s.png"
done
magick png/virtualjaguar-16.png png/virtualjaguar-32.png png/virtualjaguar-48.png \
       png/virtualjaguar-64.png png/virtualjaguar-128.png png/virtualjaguar-256.png virtualjaguar.ico
mkdir -p ../windows/msix/Assets
for entry in Square44x44Logo:44 Square150x150Logo:150 StoreLogo:50; do
  rsvg-convert -w "${entry#*:}" -h "${entry#*:}" "$svg" -o "../windows/msix/Assets/${entry%:*}.png"
done
if command -v iconutil >/dev/null; then
  set_dir=$(mktemp -d)/virtualjaguar.iconset
  mkdir -p "$set_dir"
  for s in 16 32 128 256 512; do
    rsvg-convert -w "$s" -h "$s" "$svg" -o "$set_dir/icon_${s}x${s}.png"
    rsvg-convert -w "$((s*2))" -h "$((s*2))" "$svg" -o "$set_dir/icon_${s}x${s}@2x.png"
  done
  iconutil -c icns "$set_dir" -o virtualjaguar.icns
fi
