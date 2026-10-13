#!/bin/bash
# Merge two single-architecture install trees into one universal tree.
#
#   make-universal.sh <arm64-prefix> <x86_64-prefix> <out-prefix>
#
# Every Mach-O in the arm64 tree that also exists in the x86_64 tree is lipo'd (the
# executable, and librashader.dylib: VJShaders.cmake fetches the prebuilt runtime for ONE
# architecture per configure, so a single multi-arch configure would ship the arm64
# runtime only).  Data files come from the arm64 tree.  Every Mach-O in the result must
# carry BOTH architectures or this fails -- with ONE documented exception: files listed in
# ARM64_ONLY_OK (default: librashader.dylib).  As of librashader-v0.12.0 the upstream
# "x86_64-macos" release zip holds an arm64 dylib (verified with lipo), so there is no
# prebuilt Intel runtime to merge.  Such a file ships arm64-only, with a warning: on an
# Intel Mac the app reports the shader runtime as unavailable and runs without shaders.
# The real fix is building librashader for x86_64-apple-darwin in CI (a maintainer item).
set -euo pipefail
ARM64_ONLY_OK="${ARM64_ONLY_OK:-librashader.dylib}"
a="$1"; x="$2"; out="$3"
rm -rf "$out"; mkdir -p "$out"
cp -a "$a/." "$out/"
n=0
while IFS= read -r -d '' f; do
  rel="${f#"$a"/}"
  if file -b "$f" | grep -q 'Mach-O'; then
    [ -f "$x/$rel" ] || { echo "make-universal: $rel missing from the x86_64 tree" >&2; exit 1; }
    if [ "$(lipo -archs "$f")" = "$(lipo -archs "$x/$rel")" ]; then
      case " $ARM64_ONLY_OK " in
        *" $(basename "$rel") "*)
          echo "::warning::$rel has no x86_64 slice (upstream ships an arm64 binary in the x86_64 archive): shipping arm64-only"
          continue ;;
      esac
    fi
    lipo -create "$f" "$x/$rel" -output "$out/$rel"
    n=$((n+1))
  fi
done < <(find "$a" -type f -print0)
echo "make-universal: merged $n Mach-O file(s)"
