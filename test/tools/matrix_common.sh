#!/usr/bin/env bash
# test/tools/matrix_common.sh -- shared preflight, core-error classification and
# row-cache identity for the boot-matrix sweeps (cart_boot_matrix.sh and
# cd_boot_matrix.sh).
#
# WHY THIS FILE EXISTS
#   Both sweeps independently reimplemented core discovery, the build-identity
#   preflight and result classification.  That duplication is why the same
#   three faults existed in two places: `7e6975f` fixed them in the cart script
#   only, and the CD script still blamed titles for a core that would not load.
#   One implementation means a fix lands in both sweeps at once.
#
# WHAT STAYS PER-SCRIPT
#   Stage-classification ladders.  Cart and CD boot stages genuinely differ and
#   must not be merged behind a mode switch.  Only the CORE-LEVEL verdicts --
#   "the core did not load", "the core is not the build we asked for" -- are
#   shared, because those are never a statement about the title.
#
# Source it from a sweep script:
#   . "$(dirname "${BASH_SOURCE[0]}")/matrix_common.sh"

# ---------------------------------------------------------------------------
# Core discovery + preflight
# ---------------------------------------------------------------------------

MATRIX_CORE=""

matrix_find_core() {
    # usage: matrix_find_core || exit 1     then use "$MATRIX_CORE"
    #
    # Sets the global MATRIX_CORE rather than echoing, DELIBERATELY: called as
    # core="$(matrix_find_core)" the function runs in a subshell, where `exit 1`
    # kills only that subshell and hands the caller an EMPTY STRING -- which is
    # the very fault this function exists to prevent.  The regression test
    # (matrix_common_test.sh) pins this.
    #
    # The old cart version fell through to a nonexistent .so when the dylib was
    # missing (an ABI-mode switch deletes it, and a stray iOS-built .o makes the
    # relink fail); every worker then dlopen-failed and the classifier wrote 123
    # false LOAD_FAIL rows.  The old CD version looped over the same candidates
    # but never re-checked after its rebuild, so it could continue with an EMPTY
    # core path.  Both are fatal here.
    local candidate found=""
    MATRIX_CORE=""
    for candidate in virtualjaguar_libretro.dylib virtualjaguar_libretro.so; do
        if [ -f "$candidate" ]; then found="$candidate"; break; fi
    done
    if [ -z "$found" ]; then
        echo "no built core found -- building with TEST_EXPORTS=1..." >&2
        make TEST_EXPORTS=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >&2 \
            || { echo "FATAL: build failed" >&2; exit 1; }
        for candidate in virtualjaguar_libretro.dylib virtualjaguar_libretro.so; do
            if [ -f "$candidate" ]; then found="$candidate"; break; fi
        done
    fi
    if [ -z "$found" ]; then
        echo "FATAL: no core at ./virtualjaguar_libretro.{dylib,so} after a build" >&2
        echo "  build one by hand with:  make TEST_EXPORTS=1" >&2
        return 1
    fi
    MATRIX_CORE="$found"
}

matrix_require_test_exports() {
    # usage: matrix_require_test_exports <core> <symbol>
    # A plain `make` relinks against the slim production export list and drops
    # the dlsym-able internals; every dlsym-based assertion then reports SKIP,
    # which reads as a pass.  Rebuild rather than sweep a half-blind core.
    local core="$1" symbol="$2"
    command -v nm >/dev/null 2>&1 || return 0
    if ! nm -gU "$core" 2>/dev/null | grep -q "$symbol"; then
        echo "$core is missing test exports ($symbol) -- rebuilding..." >&2
        command rm -f "$core"
        make TEST_EXPORTS=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >&2 \
            || { echo "FATAL: build failed" >&2; exit 1; }
    fi
}

matrix_build_id() {
    # The conservative, git-rev-based identity used for VJ_EXPECT_BUILD.  This
    # guards against testing a STALE BINARY and is deliberately broad: it is
    # cheap to rebuild, expensive to publish a matrix from the wrong core.
    bash scripts/build-id.sh 2>/dev/null || true
}

matrix_verify_core_build() {
    # usage: matrix_verify_core_build <core> <build-id>
    # Rebuild if the binary does not embed the expected id.
    local core="$1" want="$2"
    [ -n "$want" ] || return 0
    if ! strings "$core" 2>/dev/null | grep -Eq "$want( |\$)"; then
        echo "$core does not embed build id $want -- rebuilding..." >&2
        command rm -f "$core"
        make TEST_EXPORTS=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >&2 \
            || { echo "FATAL: build failed" >&2; exit 1; }
    fi
}

# ---------------------------------------------------------------------------
# Row-cache identity  (issue #440)
# ---------------------------------------------------------------------------

# Paths whose content can actually change a matrix row: the emulated machine,
# the harnesses that drive it, and the sweep scripts that classify the result.
# Everything else -- docs, the site, .gitignore, release notes, and the matrix
# output itself -- cannot, and must not invalidate the cache.
MATRIX_INPUT_PATHS="src libretro.c libretro_core_options.h libretro-common \
Makefile Makefile.common test/harness test/cd_assertions.h \
test/tools/cart_boot_probe.c test/tools/cart_boot_matrix.sh \
test/tools/cd_boot_matrix.sh test/tools/matrix_common.sh \
test/test_cd_hle_boot.c test/test_cd_bios_boot.c"

matrix_cache_id() {
    # A narrow identity for the ROW CACHE, distinct from matrix_build_id.
    #
    # Rows used to be keyed on the git rev, so ANY commit invalidated every
    # cached row.  A sweep once re-measured all 154 cart titles (~45 min) after
    # two commits that touched only .gitignore and the matrix's own output --
    # zero core code -- and produced byte-identical results.  Key on content
    # that can change a verdict instead.
    #
    # Committed tree state for those paths, plus any uncommitted diff to them,
    # so a dirty working tree is still distinguished.  Falls back to the broad
    # build id outside a git checkout.
    local trees diff
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        matrix_build_id
        return
    fi
    trees="$(for p in $MATRIX_INPUT_PATHS; do
                 git rev-parse "HEAD:$p" 2>/dev/null || printf 'absent:%s\n' "$p"
             done)"
    diff="$(git diff HEAD -- $MATRIX_INPUT_PATHS 2>/dev/null)"
    printf '%s\n%s\n' "$trees" "$diff" \
        | { shasum 2>/dev/null || sha1sum; } \
        | cut -c1-12
}

# ---------------------------------------------------------------------------
# Core-level classification  (the never-blame-the-title rule)
# ---------------------------------------------------------------------------

matrix_core_error() {
    # usage: reason="$(matrix_core_error <logfile>)"
    #        [ -n "$reason" ] && { record "? (core_error)|$reason"; return; }
    #
    # MUST be consulted before any title verdict.  A core that fails to dlopen,
    # or that is not the build we asked for, says nothing about the ROM -- and
    # because rows are cached, one such row is reused by every later invocation.
    # harness.c prints "harness: dlopen(<path>): <dlerror>" on a failed load;
    # the build guard prints "FATAL build mismatch".
    local log="$1"
    [ -f "$log" ] || { printf 'log %s missing\n' "$log"; return; }
    if grep -aq 'dlopen(' "$log" 2>/dev/null; then
        printf 'core failed to load -- row invalid, not a title result\n'
        return
    fi
    if grep -aq 'FATAL build mismatch' "$log" 2>/dev/null; then
        printf 'core does not match VJ_EXPECT_BUILD -- row invalid\n'
        return
    fi
}

# ---------------------------------------------------------------------------
# Bounded execution
# ---------------------------------------------------------------------------

MATRIX_TIMEOUT_BIN="$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)"

matrix_run_bounded() {
    # usage: matrix_run_bounded <seconds> <logfile> <cmd...>
    # Returns 124 on timeout, matching GNU timeout, whichever path is taken.
    local secs="$1" logfile="$2"
    shift 2
    if [ -n "$MATRIX_TIMEOUT_BIN" ]; then
        "$MATRIX_TIMEOUT_BIN" "$secs" "$@" >"$logfile" 2>&1
        return $?
    fi
    "$@" >"$logfile" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        waited=$((waited + 1))
        if [ "$waited" -ge "$secs" ]; then
            kill -TERM "$pid" 2>/dev/null
            sleep 2
            kill -KILL "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            return 124
        fi
    done
    wait "$pid"
    return $?
}
