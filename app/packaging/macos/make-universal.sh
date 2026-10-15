#!/bin/bash
# Merge two single-architecture install trees into one universal tree.
#
#   make-universal.sh <arm64-prefix> <x86_64-prefix> <out-prefix>
#
# Every Mach-O in the arm64 tree that also exists in the x86_64 tree is lipo'd: the executable
# and librashader.dylib.  Both trees come from single-architecture configures (VJShaders.cmake
# is handed a single-architecture librashader per configure, -DVJ_LIBRASHADER_LIB=..., built
# from the same pinned source by .github/workflows/librashader.yml), so one multi-arch
# configure would not do.  Data files come from the arm64 tree.  Every Mach-O must exist in
# both trees with ONE architecture each, and the merge must contain both -- no exceptions:
# the old arm64-only librashader.dylib (upstream's "x86_64" zip held an arm64 binary, #950)
# is gone.
set -euo pipefail
a="$1"; x="$2"; out="$3"
rm -rf "$out"; mkdir -p "$out"
cp -a "$a/." "$out/"
n=0
while IFS= read -r -d '' f; do
  rel="${f#"$a"/}"
  if file -b "$f" | grep -q 'Mach-O'; then
    [ -f "$x/$rel" ] || { echo "make-universal: $rel missing from the x86_64 tree" >&2; exit 1; }
    [ "$(lipo -archs "$f")" = arm64 ]    || { echo "make-universal: $rel in the arm64 tree is '$(lipo -archs "$f")', not arm64" >&2; exit 1; }
    [ "$(lipo -archs "$x/$rel")" = x86_64 ] || { echo "make-universal: $rel in the x86_64 tree is '$(lipo -archs "$x/$rel")', not x86_64" >&2; exit 1; }
    lipo -create "$f" "$x/$rel" -output "$out/$rel"
    n=$((n+1))
  fi
done < <(find "$a" -type f -print0)
echo "make-universal: merged $n Mach-O file(s)"
