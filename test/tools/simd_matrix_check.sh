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
#   * platform=unix with an aarch64- CROSS CC selected SCALAR on hardware
#     where NEON is architecturally mandatory.  (Not the official ARM64
#     Linux cores -- both build natively with plain gcc, so `uname -m`
#     fired and they always had NEON.  This hit cross-builds: distro
#     packagers, container cross-compiles.)
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
# Built with printf rather than a heredoc on purpose: the recipe line needs a
# leading TAB (make refuses anything else), and .editorconfig pins *.sh to
# indent_style = space -- a literal tab in this file fails the CI lint. Single
# quotes keep make's $(...) out of the shell's hands.
{
   printf 'vjsimd:\n'
   printf '\t@echo "SIMD=$(notdir $(BLITTER_SIMD_SRC)) TARGET=$(notdir $(TARGET)) CFLAGS=$(CFLAGS)"\n'
} > "$PROBE"

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
   # `env -u MAKEFLAGS` is load-bearing, not tidiness.  GNU make passes
   # command-line variables to sub-makes through MAKEFLAGS, and this script
   # runs from inside `make test` -- so under `make coverage`, which invokes
   # `$(MAKE) COVERAGE=1 TEST_EXPORTS=1 test`, the probe below would inherit
   # COVERAGE=1 and see `FLAGS += --coverage -O0 -g` appended after the
   # resolved level.  Every rpi row then reports "last -O is -O0", which is
   # the Makefile behaving CORRECTLY for a coverage build and this check
   # asking the wrong question.
   #
   # The question these assertions exist to answer is what a RELEASE build
   # resolves to, so the probe must be independent of whatever axes the
   # calling make happens to carry.  The explicit empty assignments pin the
   # rest of BUILD_AXES for the same reason -- a future `make test` variant
   # that sets DEBUG=1 would otherwise silently move the answer again.
   env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL \
      make -n -f Makefile -f "$PROBE" vjsimd platform="$plat" \
           COVERAGE= DEBUG= TEST_EXPORTS= BENCH_PROFILE= BLITTER_TRACE= \
           RELEASE_DEBUG_INFO= DEBUG_PRESENTATION= STATIC_LINKING= \
           "$@" 2>/dev/null \
      | tr -d '"' | sed -n 's/.*\(SIMD=.*\)/\1/p' | head -1
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

# expect_tune <platform> <expected-last-O> <expected-mcpu|-> <expect-32bit-fp:yes|no>
#
# Guards issue #516, which is a DIFFERENT failure from the SIMD one above and
# needs its own assertion: classic_armv7_a7 asked for -Ofast in its platform
# block for years and silently built at -O2, because the shared release branch
# appended -O2 afterwards and the LAST -O wins.  Nothing noticed, because a
# wrong -O level -- exactly like a wrong blitter -- compiles, links and passes
# every functional test.
#
# So this asserts the resolved level is the last -O in CFLAGS, not merely
# present, and that exactly one -O appears at all.
#
# It also asserts -mfpu/-mfloat-abi appear ONLY on 32-bit ARM: aarch64 gcc
# hard-errors on them ("unrecognized command-line option '-mfpu=neon-fp-armv8'"),
# so leaking one onto a *_64 row breaks that build outright.
expect_tune() {
   local plat="$1" want_o="$2" want_mcpu="$3" want_fp="$4"
   local got cflags last_o n_o mcpu has_fp

   checked=$((checked + 1))
   got="$(dump "$plat")"
   cflags="$(printf '%s' "$got" | sed -n 's/.*CFLAGS=//p')"

   if [ -z "$cflags" ]; then
      printf 'FAIL  %-42s could not read CFLAGS\n' "$plat"; fail=$((fail + 1)); return
   fi

   last_o="$(printf '%s' "$cflags" | tr ' ' '\n' | grep -E '^-O' | tail -1)"
   n_o="$(  printf '%s' "$cflags" | tr ' ' '\n' | grep -cE '^-O')"
   mcpu="$( printf '%s' "$cflags" | tr ' ' '\n' | grep -E '^-mcpu=' | tail -1)"
   has_fp="$(printf '%s' "$cflags" | tr ' ' '\n' | grep -cE '^-(mfpu=|mfloat-abi=)')"

   if [ "$last_o" != "$want_o" ]; then
      printf 'FAIL  %-42s last -O is %s, expected %s (issue #516)\n' "$plat" "${last_o:-none}" "$want_o"
      fail=$((fail + 1)); return
   fi
   if [ "$n_o" -ne 1 ]; then
      printf 'FAIL  %-42s %s -O flags in CFLAGS; expected exactly 1 (issue #516)\n' "$plat" "$n_o"
      fail=$((fail + 1)); return
   fi
   if [ "$want_mcpu" = "-" ]; then
      if [ -n "$mcpu" ]; then
         printf 'FAIL  %-42s unexpected %s\n' "$plat" "$mcpu"; fail=$((fail + 1)); return
      fi
   elif [ "$mcpu" != "-mcpu=$want_mcpu" ]; then
      printf 'FAIL  %-42s -mcpu is %s, expected -mcpu=%s\n' "$plat" "${mcpu:-none}" "$want_mcpu"
      fail=$((fail + 1)); return
   fi
   if [ "$want_fp" = yes ] && [ "$has_fp" -eq 0 ]; then
      printf 'FAIL  %-42s 32-bit ARM row is missing -mfpu/-mfloat-abi\n' "$plat"; fail=$((fail + 1)); return
   fi
   if [ "$want_fp" = no ] && [ "$has_fp" -ne 0 ]; then
      printf 'FAIL  %-42s 64-bit row carries -mfpu/-mfloat-abi; aarch64 gcc rejects those\n' "$plat"
      fail=$((fail + 1)); return
   fi

   [ "$VERBOSE" = 1 ] && printf 'ok    %-42s %s %s\n' "$plat" "$last_o" "${mcpu:-(no -mcpu)}"
   return 0
}

echo "simd_matrix_check: blitter selection per platform (issue #560)"

# --- cross-compiled ARM64 Linux --------------------------------------------
# Not what the official runners do (they are native), but what a distro
# packager or a container cross-build does.
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

# --- per-SoC tuning and optimisation level (issues #560, #516) --------------
# The Pi platform names identify the silicon exactly, so -mcpu is a fact here
# rather than a guess. rpi0/rpi1 are ARMv6 (ARM1176, no NEON); the rest are
# A7/A53/A72/A76.
echo
echo "simd_matrix_check: -O level and per-SoC tuning (issues #516, #560)"
expect_tune rpi0     -O3 arm1176jzf-s yes
expect_tune rpi1     -O3 arm1176jzf-s yes
expect_tune rpi2     -O3 cortex-a7    yes
expect_tune rpi3     -O3 cortex-a53   yes
expect_tune rpi3_64  -O3 cortex-a53   no
expect_tune rpi4     -O3 cortex-a72   yes
expect_tune rpi4_64  -O3 cortex-a72   no
expect_tune rpi5     -O3 cortex-a76   yes
expect_tune rpi5_64  -O3 cortex-a76   no
# The target #516 was actually about: -Ofast must survive to the end.
expect_tune classic_armv7_a7 -Ofast - yes
# Non-Pi ARM must NOT acquire -mcpu -- we do not know their silicon.
expect_tune tvos-arm64 -O3 - no
expect_tune vita       -O2 - no

echo

# --- ELF visibility / interposition (issue #569) ----------------------------
# -fno-semantic-interposition and -fvisibility=hidden must reach every
# GC_STYLE=gnu release CFLAGS; -fvisibility=hidden must vanish under
# TEST_EXPORTS=1 (the white-box harnesses dlsym() internal symbols), while
# -fno-semantic-interposition -- codegen-only, no symbol-table effect -- stays
# on regardless. Mach-O (osx) must get neither: ld64's two-level namespace
# never had the interposition assumption to begin with.
#
# expect_vis <platform> <expect-hidden:yes|no> <expect-interposition:yes|no> [extra make vars...]
expect_vis() {
   local plat="$1" want_hidden="$2" want_interp="$3"; shift 3
   local got cflags has_hidden has_interp label

   checked=$((checked + 1))
   label="$plat${1:+ $1}"
   got="$(dump "$plat" "$@")"
   cflags="$(printf '%s' "$got" | sed -n 's/.*CFLAGS=//p')"

   if [ -z "$cflags" ]; then
      printf 'FAIL  %-42s could not read CFLAGS\n' "$label"; fail=$((fail + 1)); return
   fi

   has_hidden="$(printf '%s' "$cflags" | tr ' ' '\n' | grep -cE '^-fvisibility=hidden$')"
   has_interp="$(printf '%s' "$cflags" | tr ' ' '\n' | grep -cE '^-fno-semantic-interposition$')"

   if [ "$want_hidden" = yes ] && [ "$has_hidden" -eq 0 ]; then
      printf 'FAIL  %-42s missing -fvisibility=hidden\n' "$label"; fail=$((fail + 1)); return
   fi
   if [ "$want_hidden" = no ] && [ "$has_hidden" -ne 0 ]; then
      printf 'FAIL  %-42s unexpectedly has -fvisibility=hidden (breaks TEST_EXPORTS dlsym)\n' "$label"
      fail=$((fail + 1)); return
   fi
   if [ "$want_interp" = yes ] && [ "$has_interp" -eq 0 ]; then
      printf 'FAIL  %-42s missing -fno-semantic-interposition\n' "$label"; fail=$((fail + 1)); return
   fi
   if [ "$want_interp" = no ] && [ "$has_interp" -ne 0 ]; then
      printf 'FAIL  %-42s unexpectedly has -fno-semantic-interposition\n' "$label"
      fail=$((fail + 1)); return
   fi

   [ "$VERBOSE" = 1 ] && printf 'ok    %-42s hidden=%s interp=%s\n' "$label" "$want_hidden" "$want_interp"
   return 0
}

echo "simd_matrix_check: ELF visibility / interposition (issue #569)"
expect_vis unix     yes yes
expect_vis rpi4_64  yes yes
expect_vis rpi1     yes yes
expect_vis arm64    yes yes
# TEST_EXPORTS=1: -fvisibility=hidden must disappear, interposition stays.
expect_vis unix     no  yes TEST_EXPORTS=1
expect_vis rpi4_64  no  yes TEST_EXPORTS=1
# Mach-O is GC_STYLE=macho, not gnu -- neither flag belongs there.
expect_vis osx      no  no
# win (native MinGW) is GC_STYLE=gnu too, but that is a --version-script
# convenience, not an ELF signal: TARGET is a PE/COFF .dll and RETRO_API
# there expands to __attribute__((__dllexport__)), not the visibility
# attribute, so it is explicitly excluded from both flags in the Makefile.
expect_vis win       no  no

echo
if [ "$fail" -ne 0 ]; then
   echo "simd_matrix_check: FAILED ($fail of $checked rows)"
   exit 1
fi
echo "simd_matrix_check: OK ($checked rows)"
exit 0
