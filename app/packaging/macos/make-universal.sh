#!/bin/bash
# Merge two single-architecture install trees into one universal tree.
#
#   make-universal.sh <arm64-prefix> <x86_64-prefix> <out-prefix>
#
# Every Mach-O in the arm64 tree that also exists in the x86_64 tree is lipo'd (the
# executable, and librashader.dylib: VJShaders.cmake fetches the prebuilt runtime for ONE
# architecture per configure, so a single multi-arch configure would ship the arm64
# runtime only).  Data files come from the arm64 tree.  Every Mach-O in the result must
# carry BOTH architectures or this fails.
set -euo pipefail
a="$1"; x="$2"; out="$3"
rm -rf "$out"; mkdir -p "$out"
cp -a "$a/." "$out/"
n=0
while IFS= read -r -d '' f; do
  rel="${f#"$a"/}"
  if file -b "$f" | grep -q 'Mach-O'; then
    [ -f "$x/$rel" ] || { echo "make-universal: $rel missing from the x86_64 tree" >&2; exit 1; }
    lipo -create "$f" "$x/$rel" -output "$out/$rel"
    n=$((n+1))
  fi
done < <(find "$a" -type f -print0)
echo "make-universal: merged $n Mach-O file(s)"
