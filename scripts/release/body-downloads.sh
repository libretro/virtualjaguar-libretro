#!/usr/bin/env bash
# Print a quick download table from the assets staged for upload.
# Usage: bash scripts/release/body-downloads.sh <release-dir> <download-url> <tag>
set -euo pipefail
rel=${1:?release dir}
download_url=${2:?download URL}
tag=${3:?release tag}
# Match the asset label in app-package.yml (e.g. v3.11.0 -> 3.11.0).
asset_label=${tag#v}
app=VirtualJaguar-$asset_label
core=virtualjaguar_libretro
shopt -s nullglob
app_files=("$rel/$app"-* "$rel/virtualjaguar_${asset_label}_"*.deb)

links() {
  local separator='' label file
  while [ "$#" -gt 0 ]; do
    label=$1; file=$2; shift 2
    [ -f "$rel/$file" ] || continue
    printf '%s[%s](%s/%s)' "$separator" "$label" "$download_url" "$file"
    separator=' · '
  done
}

row() {
  local platform=$1 standalone=$2 retroarch=$3
  if [ -n "$standalone$retroarch" ]; then
    if [ "${#app_files[@]}" -gt 0 ]; then
      printf '| %s | %s | %s |\n' "$platform" "${standalone:-—}" "${retroarch:-—}"
    else
      printf '| %s | %s |\n' "$platform" "$retroarch"
    fi
  fi
}

echo '## Quick downloads'
echo
if [ "${#app_files[@]}" -gt 0 ]; then
  echo '| Platform | Standalone app | Libretro core |'
  echo '|---|---|---|'
else
  echo '| Platform | Libretro core |'
  echo '|---|---|'
fi
row 'macOS' "$(links 'Universal app (.zip)' "$app-macos-universal.zip")" \
  "$(links 'Apple Silicon' "$core-macos-arm64.dylib" 'Intel' "$core-macos-x86_64.dylib")"
row 'Windows x64 / x86' "$(links 'x64 installer (.exe)' "$app-windows-x64-setup.exe" 'x64 portable (.zip)' "$app-windows-x64.zip")" \
  "$(links '64-bit' "$core-windows-x86_64.dll" '32-bit' "$core-windows-i686.dll")"
row 'Windows ARM64' "$(links 'Installer (.exe)' "$app-windows-arm64-setup.exe" 'Portable (.zip)' "$app-windows-arm64.zip")" ''
row 'Linux x86-64 / x86' "$(links 'AppImage (64-bit)' "$app-linux-x86_64.AppImage" '.deb (64-bit)' "virtualjaguar_${asset_label}_amd64.deb" 'Flatpak (64-bit)' "$app-linux-x86_64.flatpak")" \
  "$(links '64-bit' "$core-linux-x86_64.so" '32-bit' "$core-linux-i686.so")"
row 'Linux ARM64' "$(links 'AppImage' "$app-linux-aarch64.AppImage" '.deb' "virtualjaguar_${asset_label}_arm64.deb" 'Flatpak' "$app-linux-aarch64.flatpak")" \
  "$(links 'ARM64' "$core-linux-aarch64.so")"
row 'Android' '' "$(links 'ARM64' "$core-android-arm64-v8a.so" 'ARMv7' "$core-android-armeabi-v7a.so" \
  'x86-64' "$core-android-x86_64.so" 'x86' "$core-android-x86.so")"
row 'iOS / tvOS' '' "$(links 'iOS' "$core-ios-arm64.dylib" 'tvOS' "$core-tvos-arm64.dylib")"
row 'Raspberry Pi' '' "$(links 'Pi 1' "$core-linux-rpi1-armv6.so" 'Pi 2' "$core-linux-rpi2-cortex-a7.so" \
  'Pi 3 (32-bit)' "$core-linux-rpi3-cortex-a53.so" 'Pi 3 (64-bit)' "$core-linux-rpi3_64-cortex-a53.so" \
  'Pi 4 (32-bit)' "$core-linux-rpi4-cortex-a72.so" 'Pi 4 (64-bit)' "$core-linux-rpi4_64-cortex-a72.so" \
  'Pi 5 (32-bit)' "$core-linux-rpi5-cortex-a76.so" 'Pi 5 (64-bit)' "$core-linux-rpi5_64-cortex-a76.so")"
row 'Switch / PS Vita' '' "$(links 'Switch' "$core-switch.a" 'PS Vita' "$core-vita.a")"
row 'Web (Emscripten)' '' "$(links 'WASM build (.bc)' "$core-emscripten-wasm.bc")"
echo
if [ -f "$rel/$core.info" ]; then
  printf 'For RetroArch, download the %s alongside the core.\n\n' "$(links 'core info file' "$core.info")"
fi

if [ -f "$rel/$app-macos-universal.zip" ]; then
  case "$tag" in
    v*)
      cat <<'EOF'
### Homebrew (macOS)

Install the stable standalone app from the [project tap](https://github.com/Provenance-Emu/homebrew-virtualjaguar):

```sh
brew tap Provenance-Emu/virtualjaguar
brew trust --cask Provenance-Emu/virtualjaguar/virtualjaguar
brew install --cask Provenance-Emu/virtualjaguar/virtualjaguar
```

EOF
      ;;
    nightly|prerelease)
      printf 'The [Homebrew tap](https://github.com/Provenance-Emu/homebrew-virtualjaguar) installs the stable macOS app. For this build, use the %s above.\n\n' \
        "$(links 'universal macOS ZIP' "$app-macos-universal.zip")"
      ;;
  esac
fi
