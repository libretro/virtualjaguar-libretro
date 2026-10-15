#!/bin/bash
# Fail unless EVERY Mach-O under <dir> (librashader.dylib included) contains arm64 and x86_64, and report each
# one's minimum macOS version (a prebuilt dylib with a higher minos than LSMinimumSystemVersion
# would make the app fail to launch on older systems).
#
#   check-universal.sh <dir> [<max-allowed-minos>]
set -euo pipefail
dir="$1"; maxmin="${2:-}"
bad=0; n=0
while IFS= read -r -d '' f; do
  file -b "$f" | grep -q 'Mach-O' || continue
  n=$((n+1))
  archs="$(lipo -archs "$f")"
  ok=0; if echo "$archs" | grep -qw arm64 && echo "$archs" | grep -qw x86_64; then ok=1; fi
  for arch in $archs; do
    minos="$(vtool -arch "$arch" -show-build "$f" 2>/dev/null | awk '/minos/ {print $2; exit}')"
    echo "  ${f#"$dir"/}: $arch minos=${minos:-?}"
    if [ -n "$maxmin" ] && [ -n "$minos" ] && \
       [ "$(printf '%s\n%s\n' "$minos" "$maxmin" | sort -V | tail -1)" != "$maxmin" ]; then
      echo "::error::${f#"$dir"/} ($arch) needs macOS $minos > $maxmin"; bad=1
    fi
  done
  if [ "$ok" != 1 ]; then
    echo "::error::${f#"$dir"/} is not universal (archs: $archs)"; bad=1
  fi
done < <(find "$dir" -type f -print0)
[ "$n" -gt 0 ] || { echo "::error::no Mach-O files under $dir"; exit 1; }
exit "$bad"
