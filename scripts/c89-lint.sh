#!/bin/sh
# C89 compliance lint — catches mid-block variable declarations
# that break MSVC builds. Run before committing.
#
# Usage: scripts/c89-lint.sh [file ...]
#   No args = check all source files
#   With args = check only the listed files

set -e

# Generate src/core/version.h if it is missing -- this script is invoked
# from CI and pre-commit hooks where `make` may not have run yet.
#
# libretro.c no longer *needs* it (it falls back to the committed
# src/core/version_fallback.h when the generated header is absent), so this
# is now a deliberate choice rather than a workaround: the Makefile build is
# the one this lint is meant to model, and that build always has the
# generated header.  Dropping these two lines would silently switch the lint
# to the fallback path instead.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/src/core/version.h" ] || sh "$ROOT/scripts/gen-version-h.sh"

CC="${CC:-gcc}"
CFLAGS="-fsyntax-only -std=gnu89 -Werror=declaration-after-statement"
INCLUDES="-I. -Isrc -Isrc/core -Isrc/tom -Isrc/jerry -Isrc/cd -Isrc/bios -Isrc/m68000 -Isrc/debug -Ilibretro-common/include -Ideps/libchdr/include"
DEFINES='-D__LIBRETRO__ -DINLINE=inline'

skip_file() {
    case "$1" in
        src/m68000/cpu*.c|src/m68000/read*.c) return 0 ;;
        src/bios/jag*bios*.c) return 0 ;;
        # Skipped only by the default pass, which has no BLITTER_SIMD_<ARCH>
        # define and so cannot compile them (blitter_simd.h would inline the
        # scalar ops and the arch header would redefine every one). They are
        # checked, with the right define and target, by check_simd_arch below.
        src/tom/blitter_simd_neon.c|src/tom/blitter_simd_sse2.c) return 0 ;;
        src/tom/op_simd_neon.h) return 0 ;;
        src/tom/op_simd_sse2.h) return 0 ;;
        src/tom/tom_scan_simd_neon.h) return 0 ;;
        src/tom/tom_scan_simd_sse2.h) return 0 ;;
        src/tom/shadowfb_simd_neon.h) return 0 ;;
        src/jerry/voicechat_simd_neon.h) return 0 ;;
        # Depends on rcheevos headers fetched at runtime by the e2e shell wrapper.
        test/tools/test_rcheevos_e2e.c) return 0 ;;
        # Diagnostic tools — not part of the libretro core build.
        test/tools/flicker_detect.c) return 0 ;;
        deps/libchdr/*) return 0 ;;
        tools/jagcd/*) return 0 ;;
        # Builds against test/harness/, which is outside $INCLUDES; C99 harness.
        test/tools/audio_wav_dump.c) return 0 ;;
        test/tools/fmv_seek_probe.c) return 0 ;;
        test/tools/frame_hash_ab.c) return 0 ;;
        test/tools/hires_box_check.c) return 0 ;;
        test/tools/hires_state_digest.c) return 0 ;;
        test/tools/gdb_determinism_probe.c) return 0 ;;
        test/tools/hires_shot.c) return 0 ;;
        test/tools/blit_memo_verify.c) return 0 ;;
        test/tools/op_list_dump.c) return 0 ;;
    esac
    return 1
}

FAILED=0

if [ $# -gt 0 ]; then
    FILES="$@"
else
    FILES="libretro.c $(find src -name '*.c')"
fi

CHECK_SIMD=0

for f in $FILES; do
    [ -f "$f" ] || continue
    case "$f" in *.c) ;; *) continue ;; esac
    case "$f" in src/tom/blitter.c|src/tom/blitter_simd_*.c|src/tom/op.c|src/tom/tom.c) CHECK_SIMD=1 ;; esac
    if skip_file "$f"; then continue; fi

    if ! $CC $CFLAGS $INCLUDES $DEFINES "$f" 2>&1; then
        FAILED=1
    fi
done

# --------------------------------------------------------------------
# Per-arch SIMD pass.
#
# blitter.c includes blitter_simd.h, which inlines whichever
# blitter_simd_<arch>.h the build selected.  The loop above runs with no
# BLITTER_SIMD_<ARCH> define, so it only ever sees the scalar header --
# yet the buildbot compiles the sse2 one inside blitter.c on every MSVC
# and x86 target, and the neon one on every ARM target.  A C99-ism in
# either arch header would sail past the lint and break there instead.
#
# Re-check blitter.c (and the arch's own .c) once per arch, cross-
# targeting when the host can't do it natively.  A host that can't
# cross-target says so out loud rather than passing silently.
#
# op.c and tom.c ride the same pass: they include the guard-selected
# op_simd_<arch>.h / tom_scan_simd_<arch>.h fragments, which the default
# loop only ever sees with the host's own capability macros.  Compiling
# them per-arch is the only host-side syntax check the non-native
# fragment header gets.
check_simd_arch() {
    arch="$1"      # sse2 | neon
    define="$2"    # BLITTER_SIMD_SSE2 | BLITTER_SIMD_NEON
    target="$3"    # extra flags, may be empty
    probe="$4"     # intrinsics header to probe for

    cc="$CC"
    case "$target" in
        *--target=*) command -v clang >/dev/null 2>&1 && cc=clang ;;
    esac

    probe_c="${TMPDIR:-/tmp}/c89lint_probe_$$.c"
    echo "#include <$probe>" > "$probe_c"
    echo "int main(void) { return 0; }" >> "$probe_c"
    if ! $cc $CFLAGS $target "$probe_c" >/dev/null 2>&1; then
        rm -f "$probe_c"
        echo "C89 lint: SKIP $arch — $cc cannot target it here ($probe unavailable)"
        echo "C89 lint:      the $arch header is NOT checked on this host; CI covers it."
        return 0
    fi
    rm -f "$probe_c"

    arch_failed=0
    for f in src/tom/blitter.c "src/tom/blitter_simd_$arch.c" \
             src/tom/op.c src/tom/tom.c; do
        [ -f "$f" ] || continue
        if ! $cc $CFLAGS $target $INCLUDES $DEFINES "-D$define" "$f" 2>&1; then
            echo "C89 lint: $arch pass failed on $f"
            arch_failed=1
            FAILED=1
        fi
    done
    [ "$arch_failed" = "0" ] && echo "C89 lint: $arch pass OK"
    return 0
}

# Only worth running when blitter.c is actually in scope.
case "$CHECK_SIMD" in
    1)
        case "$(uname -m)" in
            x86_64|amd64|i686|i386) SSE2_TARGET="-msse2" ;;
            *) case "$(uname -s)" in
                   Darwin) SSE2_TARGET="--target=x86_64-apple-macos11 -msse2" ;;
                   *)      SSE2_TARGET="--target=x86_64-linux-gnu -msse2" ;;
               esac ;;
        esac
        case "$(uname -m)" in
            aarch64|arm64) NEON_TARGET="" ;;
            *) case "$(uname -s)" in
                   Darwin) NEON_TARGET="--target=arm64-apple-macos11" ;;
                   *)      NEON_TARGET="--target=aarch64-linux-gnu" ;;
               esac ;;
        esac
        check_simd_arch sse2 BLITTER_SIMD_SSE2 "$SSE2_TARGET" emmintrin.h
        check_simd_arch neon BLITTER_SIMD_NEON "$NEON_TARGET" arm_neon.h
        ;;
esac

if [ "$FAILED" = "1" ]; then
    echo "C89 lint FAILED — fix mid-block declarations"
    exit 1
fi
echo "C89 lint passed"
