#!/usr/bin/env bash
# Preserve the production binary before stripping/signing; never ship these intermediates in release assets.
set -euo pipefail
platform=${1:?platform required}
source_file=${2:?core binary required}
out=${3:?output directory required}
case "$platform" in
  macos-x86_64|emscripten-wasm|android-arm64-v8a|android-armeabi-v7a|android-x86_64|android-x86|ios-arm64|tvos-arm64|vita|switch) ;;
  *) echo "Unsupported shared core: $platform" >&2; exit 1 ;;
esac
[ "${TEST_EXPORTS:?explicit production ABI required}" = 0 ]
[ "${RELEASE_DEBUG_INFO:?explicit release flags required}" = 1 ]
[ -s "$source_file" ]
[ "$(git rev-parse HEAD)" = "${GITHUB_SHA:?exact workflow SHA required}" ]
mkdir -p "$out"
file=$(basename "$source_file")
command cp -f "$source_file" "$out/$file"
if command -v sha256sum >/dev/null 2>&1; then
  checksum=$(sha256sum "$out/$file" | cut -d' ' -f1)
else
  checksum=$(shasum -a 256 "$out/$file" | cut -d' ' -f1)
fi
printf '{"head_sha":"%s","platform":"%s","file":"%s","test_exports":0,"release_debug_info":1,"sha256":"%s"}\n' \
  "$GITHUB_SHA" "$platform" "$file" "$checksum" > "$out/BUILD.json"
