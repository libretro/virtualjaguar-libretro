#!/bin/bash
# Build a compressed DMG holding the app and an /Applications shortcut.
#   make-dmg.sh <Virtual Jaguar.app> <out.dmg>
set -euo pipefail
app="$1"; out="$2"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$work/dmg"
ditto "$app" "$work/dmg/$(basename "$app")"   # ditto keeps the signature, xattrs and stapled ticket
ln -s /Applications "$work/dmg/Applications"
rm -f "$out"
hdiutil create -volname "Virtual Jaguar" -srcfolder "$work/dmg" -fs HFS+ -format UDZO -ov "$out"
