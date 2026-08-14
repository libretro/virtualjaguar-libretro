#!/usr/bin/env bash
# test/tools/matrix_common_test.sh -- regression gate for matrix_common.sh.
#
# The matrix sweeps take 45+ minutes and need the private ROM corpus, so they
# cannot be a per-change gate.  These checks exercise the shared logic directly
# with synthetic logs and run in under a second.
#
# Covers the three faults that produced 123 false LOAD_FAIL rows (7e6975f, #430)
# and the row-cache churn that re-measured a whole corpus for nothing (#440).
#
# Usage: bash test/tools/matrix_common_test.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT" || exit 1

. "$SCRIPT_DIR/matrix_common.sh"

PASS=0
FAIL=0
TMP="$(mktemp -d "${TMPDIR:-/tmp}/matrix_common_test.XXXXXX")"
cleanup() { command rm -rf "$TMP"; }
trap cleanup EXIT

ok()   { PASS=$((PASS+1)); printf '  ok   %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL %s\n     %s\n' "$1" "${2:-}"; }

# ---------------------------------------------------------------------------
echo "matrix_core_error -- a core fault must never be blamed on the title"
# ---------------------------------------------------------------------------

printf 'harness: dlopen(./virtualjaguar_libretro.so): image not found\n' > "$TMP/dlopen.log"
r="$(matrix_core_error "$TMP/dlopen.log")"
[ -n "$r" ] && ok "dlopen failure is reported as a core error" \
             || bad "dlopen failure is reported as a core error" "got empty verdict"

printf '[HARNESS] FATAL build mismatch: core is abc1234, expected def5678\n' > "$TMP/mismatch.log"
r="$(matrix_core_error "$TMP/mismatch.log")"
[ -n "$r" ] && ok "build mismatch is reported as a core error" \
             || bad "build mismatch is reported as a core error" "got empty verdict"

# The critical negative: a title that genuinely fails to load must NOT be
# reclassified as a core error, or every real LOAD_FAIL disappears from the
# matrix and the sweep looks cleaner than it is.
printf '[FAIL] Some Title -- load failed\nCARTPROBE load_fail=1 frames=0\n' > "$TMP/loadfail.log"
r="$(matrix_core_error "$TMP/loadfail.log")"
[ -z "$r" ] && ok "a genuine ROM load failure is NOT a core error" \
             || bad "a genuine ROM load failure is NOT a core error" "got: $r"

printf 'CARTPROBE frames=600 pc_valid=1 pc=$001234 lit_frames=600\n' > "$TMP/good.log"
r="$(matrix_core_error "$TMP/good.log")"
[ -z "$r" ] && ok "a healthy run is not a core error" \
             || bad "a healthy run is not a core error" "got: $r"

r="$(matrix_core_error "$TMP/does-not-exist.log")"
[ -n "$r" ] && ok "a missing log is a core error, not a silent pass" \
             || bad "a missing log is a core error, not a silent pass" "got empty verdict"

# ---------------------------------------------------------------------------
echo "matrix_cache_id -- scoped to inputs that can change a verdict (#440)"
# ---------------------------------------------------------------------------

if git rev-parse --git-dir >/dev/null 2>&1; then
    base="$(matrix_cache_id)"
    [ -n "$base" ] && ok "cache id is non-empty" || bad "cache id is non-empty"

    [ "$(matrix_cache_id)" = "$base" ] && ok "cache id is stable across calls" \
        || bad "cache id is stable across calls"

    # A docs-only edit -- the exact case that cost a 45-minute re-sweep.
    DOC=docs/cart-boot-matrix.md
    if [ -f "$DOC" ]; then
        cp "$DOC" "$TMP/doc.bak"
        printf '\n<!-- test scratch -->\n' >> "$DOC"
        after="$(matrix_cache_id)"
        command cp -f "$TMP/doc.bak" "$DOC"
        [ "$after" = "$base" ] && ok "a docs edit does NOT invalidate the row cache" \
            || bad "a docs edit does NOT invalidate the row cache" "id moved $base -> $after"
    fi

    # A core-source edit MUST invalidate it, or a real regression is served
    # stale rows and looks clean.
    SRC=src/tom/blitter_mmio.c
    if [ -f "$SRC" ]; then
        cp "$SRC" "$TMP/src.bak"
        printf '\n/* test scratch */\n' >> "$SRC"
        after="$(matrix_cache_id)"
        command cp -f "$TMP/src.bak" "$SRC"
        [ "$after" != "$base" ] && ok "a core-source edit DOES invalidate the row cache" \
            || bad "a core-source edit DOES invalidate the row cache" "id unchanged at $base"
    fi
else
    echo "  skip (not a git checkout)"
fi

# ---------------------------------------------------------------------------
echo "matrix_run_bounded -- timeout convention"
# ---------------------------------------------------------------------------

matrix_run_bounded 5 "$TMP/rb.log" true
[ $? -eq 0 ] && ok "propagates success" || bad "propagates success"

matrix_run_bounded 5 "$TMP/rb.log" sh -c 'exit 3'
[ $? -eq 3 ] && ok "propagates a non-zero exit code" || bad "propagates a non-zero exit code"

matrix_run_bounded 1 "$TMP/rb.log" sh -c 'sleep 5'
[ $? -eq 124 ] && ok "returns 124 on timeout" || bad "returns 124 on timeout"

# ---------------------------------------------------------------------------
echo "matrix_find_core -- refuses to continue without a core"
# ---------------------------------------------------------------------------

# Run in an empty directory with a `make` that produces nothing: the function
# must exit non-zero rather than return an empty path (the CD script's old
# behaviour, which then ran every harness against "").
mkdir -p "$TMP/empty/bin"
printf '#!/bin/sh\nexit 0\n' > "$TMP/empty/bin/make"
chmod +x "$TMP/empty/bin/make"
out="$( cd "$TMP/empty" && PATH="$TMP/empty/bin:$PATH" bash -c \
        ". '$SCRIPT_DIR/matrix_common.sh'; matrix_find_core || exit 1; echo \"CORE=[\$MATRIX_CORE]\"" 2>/dev/null )"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "the documented caller pattern aborts when no core exists" "exited 0, output: $out"
elif printf '%s' "$out" | grep -q 'CORE='; then
    bad "the documented caller pattern aborts when no core exists" "kept going: $out"
else
    ok "the documented caller pattern aborts when no core exists"
fi

# And the trap that made this necessary: in a command substitution, a failing
# matrix_find_core must not silently yield "".
out="$( cd "$TMP/empty" && PATH="$TMP/empty/bin:$PATH" bash -c \
        ". '$SCRIPT_DIR/matrix_common.sh'; core=\"\$(matrix_find_core)\" || exit 1; echo \"CORE=[\$core]\"" 2>/dev/null )"
[ -z "$out" ] && ok "command-substitution callers still get a non-zero status" \
              || bad "command-substitution callers still get a non-zero status" "got: $out"

# ---------------------------------------------------------------------------
printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
