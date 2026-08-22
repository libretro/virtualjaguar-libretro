#!/usr/bin/env bash
#
# simd_matrix_check.sh -- assert which blitter each platform actually selects.
#
# Issue #560.  Every defect it covers was invisible for the same reason: the
# build system silently picks a *slower but working* implementation, so a
# misconfigured target compiles clean, links clean, passes every test, and
# just runs slow on hardware nobody in CI owns.  There is no failure to
# notice.  This script is the thing that notices.
#
# What it caught when it was written:
#   * platform=unix with an aarch64- cross CC -- the shipped "Linux 64-bit
#     (ARM)" buildbot binary -- selected SCALAR on hardware where NEON is
#     architecturally mandatory.
#   * armv7-neon-hardfloat, a platform named after NEON, selected SCALAR.
#   * every rpi* name fell through to the Windows branch and built a .dll.
#
# ---------------------------------------------------------------------
# WHY `make -n`, when the repo rule says -n lies
#
# The standing warning (issue #560's own text, and CLAUDE.md) is "do NOT use
# make -n to verify" -- true for anything about *what got built*, because
# already-built objects make a dry run report nothing to do.
#
# This script asks a strictly different question: what does a variable expand
# to?  Variables are computed at parse time, before any recipe runs, so -n
# reports them exactly.  And -n is required here, not merely allowed:
# `platform` is in BUILD_AXES, so a non-dry run at each of these platforms
# would rewrite .build-config and `rm -f $(TARGET) $(OBJECTS)` every
# iteration -- this check would delete the build it is meant to guard.
#
# Usage: test/tools/simd_matrix_check.sh [-v]
# Exit:  0 all rows as expected, 1 a row regressed.

set -u

cd "$(dirname "$0")/../.." || exit 1

VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

PROBE="$(mktemp -t vjsimd).mk"
trap 'rm -f "$PROBE"' EXIT
cat > "$PROBE" <<'EOF'
vjsimd:
	@echo "SIMD=$(notdir $(BLITTER_SIMD_SRC)) TARGET=$(notdir $(TARGET))"
EOF

fail=0
checked=0

# dump <platform> [CC=...]
dump() {
   local plat="$1"; shift
   # -n prints the recipe with variables already expanded; grep the echo out
   # of it.  2>/dev/null because some platform blocks probe for SDKs that are
   # absent on the running host and warn about it -- that does not affect the
   # variable we are reading.
   # -n prints the *unexecuted* `echo "SIMD=... TARGET=..."` line, quotes and
   # all, so strip the quotes before matching or the first token anchors to a
   # `"` and silently never matches.
   make -n -f Makefile -f "$PROBE" vjsimd platform="$plat" "$@" 2>/dev/null \
      | tr -d '"' | tr ' ' '\n' | grep -E '^(SIMD|TARGET)=' | tr '\n' ' '
}

# expect <expected-simd> <platform> [CC=...] -- '*' to skip the SIMD check
expect() {
   local want="$1" plat="$2"; shift 2
   local got simd target label

   got="$(dump "$plat" "$@")"
   simd="$(printf '%s' "$got" | sed -n 's/.*SIMD=blitter_simd_\([a-z0-9]*\)\.c.*/\1/p')"
   target="$(printf '%s' "$got" | sed -n 's/.*TARGET=\([^ ]*\).*/\1/p')"
   label="$plat${1:+ $1}"
   checked=$((checked + 1))

   if [ -z "$simd" ]; then
      printf 'FAIL  %-42s could not read BLITTER_SIMD_SRC (got: %s)\n' "$label" "$got"
      fail=$((fail + 1))
      return
   fi

   if [ "$want" != '*' ] && [ "$simd" != "$want" ]; then
      printf 'FAIL  %-42s expected %s, got %s\n' "$label" "$want" "$simd"
      fail=$((fail + 1))
      return
   fi

   # A non-Windows platform that produced a .dll fell through the platform
   # if-chain into the Windows fallback -- how every rpi* name used to build.
   case "$plat" in
      win*|windows*|xbox*) ;;
      *)
         case "$target" in
            *.dll)
               printf 'FAIL  %-42s fell through to the Windows branch (TARGET=%s)\n' \
                  "$label" "$target"
               fail=$((fail + 1))
               return
               ;;
         esac
         ;;
   esac

   [ "$VERBOSE" = 1 ] && printf 'ok    %-42s %s (%s)\n' "$label" "$simd" "$target"
   return 0
}

echo "simd_matrix_check: blitter selection per platform (issue #560)"

# --- the regression that shipped: cross-compiled ARM64 Linux ---------------
# platform=unix + an aarch64 cross CC is .gitlab-ci.yml:44, "Linux 64-bit
# (ARM)" -- the binary an actual Raspberry Pi user installs.
expect neon   unix CC=aarch64-linux-gnu-gcc
expect neon   unix CC=aarch64-none-linux-gnu-gcc
# 32-bit armhf says nothing about NEON (VFP baseline, and ARMv6 parts exist),
# so scalar is the correct conservative answer here, not a bug.
expect scalar unix CC=arm-linux-gnueabihf-gcc

# --- Raspberry Pi names (libretro-super) -----------------------------------
expect neon   rpi5_64
expect neon   rpi4_64
expect neon   rpi3_64
expect neon   rpi5
expect neon   rpi4
expect neon   rpi3
expect neon   rpi2
# rpi1/rpi0 are ARMv6 (ARM1176) -- no NEON in the silicon.  Scalar is correct
# and this row exists to stop a well-meaning blanket "all rpi get NEON" fix.
expect scalar rpi1
expect scalar rpi0

# --- generic ARM names ------------------------------------------------------
expect neon   armv7-neon-hardfloat
expect neon   classic_armv7_a7
expect neon   arm64
expect neon   ios-arm64
expect neon   tvos-arm64

# --- x86 ---------------------------------------------------------------------
expect sse2   windows_msvc2015_x64
# Deliberately scalar: C89-only C mode plus the _MSC_VER < 1700 SSE2_SET64
# path, and the msvc-check CI job runs VS2022 and cannot verify it.
expect scalar windows_msvc2010_x64

# --- must NOT be given host SIMD ---------------------------------------------
# emscripten is the reason the native uname -m fallback keeps a platform
# allowlist rather than being opened up to everything: it has no cross-CC
# prefix, so on an x86_64 host a blanket fix would hand it the SSE2 blitter.
expect scalar emscripten
expect scalar vita

# --- native host build --------------------------------------------------------
case "$(uname -m 2>/dev/null)" in
   aarch64|arm64)        expect neon   unix ;;
   x86_64|i686|i386)     expect sse2   unix ;;
   *)                    expect '*'    unix ;;
esac

echo
if [ "$fail" -ne 0 ]; then
   echo "simd_matrix_check: FAILED ($fail of $checked rows)"
   exit 1
fi
echo "simd_matrix_check: OK ($checked rows)"
exit 0
