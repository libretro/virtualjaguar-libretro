#!/bin/bash
# Fill the package-manager templates with a version and the release's SHA-256 values.
#
#   render.sh --version 4.0.0 --sums SHA256SUMS.txt --out <dir> [--commit SHA]
#             [--source-sha HEX | --fetch-source] [--date YYYY-MM-DD]
#
#   --sums          the release's SHA256SUMS.txt (or any file in that format listing the
#                   packages; CI builds one from the freshly built packages to validate)
#   --fetch-source  download https://github.com/libretro/virtualjaguar-libretro/archive/refs/tags/v<V>.tar.gz
#                   and hash it (the Homebrew formula's source sha); needs the tag to exist
#   --source-sha    or give it directly; neither: a zero placeholder (validation runs)
#
# Output (nothing is submitted anywhere; see app/packaging/README.md):
#   <out>/homebrew/virtualjaguar.rb (cask), virtualjaguar-formula.rb
#   <out>/winget/Libretro.VirtualJaguar{,.installer,.locale.en-US}.yaml
#   <out>/scoop/virtualjaguar.json
#   <out>/flatpak/io.github.libretro.VirtualJaguar.yml
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
version=""; sums=""; out=""; commit=""; srcsha=""; fetch=0; date_="$(date -u +%Y-%m-%d)"
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --sums) sums="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --commit) commit="$2"; shift 2 ;;
    --source-sha) srcsha="$2"; shift 2 ;;
    --fetch-source) fetch=1; shift ;;
    --date) date_="$2"; shift 2 ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
done
[ -n "$version" ] && [ -n "$sums" ] && [ -n "$out" ] || { sed -n '2,16p' "$0"; exit 2; }

sha_of() {   # asset file name -> hex digest from the sums file
  local h
  h="$(awk -v f="$1" '{n=$2; sub(/^\*/,"",n)} n==f {print $1; exit}' "$sums")"
  [ -n "$h" ] || { echo "render.sh: $1 not listed in $sums" >&2; exit 1; }
  echo "$h"
}
sha_mac_zip="$(sha_of "VirtualJaguar-${version}-macos-universal.zip")"
sha_win_zip="$(sha_of "VirtualJaguar-${version}-windows-x64.zip")"
sha_win_setup="$(sha_of "VirtualJaguar-${version}-windows-x64-setup.exe")"
if [ "$fetch" = 1 ]; then
  srcsha="$(curl -fsSL "https://github.com/libretro/virtualjaguar-libretro/archive/refs/tags/v${version}.tar.gz" | sha256sum | cut -d' ' -f1)"
fi
[ -n "$srcsha" ] || srcsha="$(printf '0%.0s' $(seq 64))"
[ -n "$commit" ] || commit="$(git -C "$here" rev-parse HEAD 2>/dev/null || printf '0%.0s' $(seq 40))"

fill() {  # template dest
  mkdir -p "$(dirname "$2")"
  sed -e "s|@VERSION@|${version}|g" \
      -e "s|@DATE@|${date_}|g" \
      -e "s|@COMMIT@|${commit}|g" \
      -e "s|@SHA_MAC_ZIP@|${sha_mac_zip}|g" \
      -e "s|@SHA_WIN_ZIP@|${sha_win_zip}|g" \
      -e "s|@SHA_WIN_SETUP_UPPER@|$(echo "$sha_win_setup" | tr a-f A-F)|g" \
      -e "s|@SHA_SOURCE@|${srcsha}|g" "$1" > "$2"
}
fill "$here/homebrew/virtualjaguar.rb.cask.in"            "$out/homebrew/virtualjaguar.rb"
fill "$here/homebrew/virtualjaguar.rb.formula.in"         "$out/homebrew/virtualjaguar-formula.rb"
fill "$here/winget/Libretro.VirtualJaguar.yaml.in"        "$out/winget/Libretro.VirtualJaguar.yaml"
fill "$here/winget/Libretro.VirtualJaguar.installer.yaml.in" "$out/winget/Libretro.VirtualJaguar.installer.yaml"
fill "$here/winget/Libretro.VirtualJaguar.locale.en-US.yaml.in" "$out/winget/Libretro.VirtualJaguar.locale.en-US.yaml"
fill "$here/scoop/virtualjaguar.json.in"                  "$out/scoop/virtualjaguar.json"
# Flatpak: the CI manifest (app/packaging/flatpak, a `dir` source) with its source block swapped
# for the release tag's git source -- one manifest, two source forms, no drift.
mkdir -p "$out/flatpak"
awk -v ver="$version" -v commit="$commit" '
  /# BEGIN-LOCAL-SOURCE/ { skipping = 1
    print "      - type: git"
    print "        url: https://github.com/libretro/virtualjaguar-libretro.git"
    print "        tag: v" ver
    print "        commit: " commit
    next }
  /# END-LOCAL-SOURCE/ { skipping = 0; next }
  !skipping { print }' "$here/../flatpak/io.github.libretro.VirtualJaguar.yml" > "$out/flatpak/io.github.libretro.VirtualJaguar.yml"
# The manifest's librashader module lists ./cargo-sources.json (relative), so it travels with it.
cp "$here/../flatpak/cargo-sources.json" "$out/flatpak/cargo-sources.json"
grep -q 'type: git' "$out/flatpak/io.github.libretro.VirtualJaguar.yml" || { echo "render.sh: flatpak source block not replaced" >&2; exit 1; }
if grep -q '^      - type: dir' "$out/flatpak/io.github.libretro.VirtualJaguar.yml"; then echo "render.sh: flatpak manifest still has a dir source" >&2; exit 1; fi
if grep -rn '@[A-Z_]*@' "$out" >/dev/null; then
  echo "render.sh: unfilled placeholders:" >&2; grep -rn '@[A-Z_]*@' "$out" >&2; exit 1
fi
find "$out" -type f | sort
