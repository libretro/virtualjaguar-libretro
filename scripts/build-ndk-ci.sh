#!/usr/bin/env bash
set -euo pipefail
# ndk-build uses jni/Android.mk (not the project Makefile),
# so the parse-time version.h regen in Makefile doesn't run.
bash scripts/gen-version-h.sh

NDK=${1:?NDK path required}
ABI=${2:?ABI required}
SIMD=${3:?SIMD required}
UPPER=$(echo "$SIMD" | tr '[:lower:]' '[:upper:]')

# Dry run first, while nothing is built, so make actually prints
# the compile lines. blitter.c MUST carry -DBLITTER_SIMD_<ARCH>:
# without it blitter.c inlines the scalar ops and still links
# happily against the neon/sse2 vtable, so a missing define is a
# silent slow path that no build failure would reveal.
"$NDK/ndk-build" APP_ABI="$ABI" -n > ndk-dry.log 2>&1
BLIT_LINES=$(grep -c '[/ ]blitter\.c' ndk-dry.log || true)
if [ "$BLIT_LINES" = "0" ]; then
  echo "::error::ndk-build -n printed no compile line for blitter.c; this check needs rework"
  exit 1
fi
if ! grep '[/ ]blitter\.c' ndk-dry.log | grep -q -- "-DBLITTER_SIMD_${UPPER}"; then
  echo "::error::blitter.c is not compiled with -DBLITTER_SIMD_${UPPER} on ${ABI}"
  grep '[/ ]blitter\.c' ndk-dry.log | head -3
  exit 1
fi
echo "==> ${ABI}: blitter.c carries -DBLITTER_SIMD_${UPPER}"

"$NDK/ndk-build" APP_ABI="$ABI" -j4

# And the arch .c that define has to agree with.
FOUND=$(find "obj/local/$ABI" -name 'blitter_simd_*.o' -exec basename {} \; | sort | tr '\n' ' ')
if [ "$FOUND" != "blitter_simd_${SIMD}.o " ]; then
  echo "::error::expected blitter_simd_${SIMD}.o on ${ABI}, found: ${FOUND:-<none>}"
  exit 1
fi
echo "==> ${ABI}: built ${FOUND}"
