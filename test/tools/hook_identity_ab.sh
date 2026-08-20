#!/usr/bin/env bash
#
# hook_identity_ab.sh -- stock-path identity gate for per-title enhancement
# hooks (issue #370, epic #338's non-negotiable guardrail: "everything
# default off; the stock path stays bit-identical").
#
# For each in-repo public ROM, emits a frame_hash_ab CSV for four arms and
# requires all four to be BYTE-IDENTICAL:
#
#   base      no --option at all: the gate at its registered default
#   base2     the same arm again -- the determinism control.  If base and
#             base2 differ, the run is noise and every other comparison in
#             this script is uninterpretable, so that is reported as a
#             separate, louder failure than a real divergence.
#   off       virtualjaguar_enhancement_hooks=disabled, set explicitly
#   on        virtualjaguar_enhancement_hooks=enabled
#
# The `on` arm matters as much as the `off` one: no shipped table row
# carries a hook, so turning the gate ON must also change nothing.
#
# WHAT THE `on` ARM DOES **NOT** PROVE.  It does not prove the applier is
# inert -- it proves the applier is unREACHED, which is a weaker claim, and
# the header used to overstate it.  TitleHookApplyToBuffer() returns at its
# first guard for every ROM this script can run:
#
#     hooks == NULL          unlisted content (both in-repo ROMs)
#     hooks[0].kind == NONE  every row the shipped table does carry
#
# So the applier's body is dead code in every configuration shipping today.
# Replace that body with `memset(rom, 0, romSize)` and this script still
# prints OK on all eight arms.  The only sabotage it catches is one where
# merely REGISTERING the option perturbs rendering -- which is a real thing
# to guard (an option that changes defaults, or reorders a struct read at
# init, would show up here), just not the thing the old wording claimed.
#
# The applier's own behaviour is covered by test_titlehook (refusal paths,
# bounds, wrap safety) and test_hook_gate (gate on/off/mismatch against a
# synthetic row).  Those are the tests that go red if the applier breaks;
# this one is the stock-path identity guard.  Do not credit it with more.
#
# NOTE ON SCOPE.  This is a self-comparison: it proves the OPTION cannot
# move the picture.  It is NOT the same claim as "byte-identical to the
# pre-change build" -- that one needs a baseline core built from a pristine
# checkout of the parent commit and is done by hand at review time (see the
# PR body).  Do not let this script stand in for that.
#
# Env:
#   VJ_HOOK_AB_FRAMES   frames per arm (default 1800)
#   VJ_HOOK_AB_ROMS     space-separated ROM list (default: the in-repo pair)
#   VJ_HOOK_AB_OUT      scratch dir for the CSVs (default: a mktemp dir)
#
# Usage: hook_identity_ab.sh <core.so|.dylib>

set -u

CORE="${1:-./virtualjaguar_libretro.dylib}"
FRAMES="${VJ_HOOK_AB_FRAMES:-1800}"
ROMS="${VJ_HOOK_AB_ROMS:-test/roms/yarc.j64 test/roms/jagniccc.j64}"
TOOL=test/tools/frame_hash_ab

if [ ! -f "$CORE" ]; then
    echo "hook_identity_ab: core '$CORE' not found" >&2
    exit 1
fi

# frame_hash_ab is not part of the default `make`; build it on first use,
# the same way vjtrace_selftest.sh builds its analyzers.
if [ ! -x "$TOOL" ]; then
    echo "hook_identity_ab: building $TOOL"
    ${CC:-cc} -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
        -o "$TOOL" test/tools/frame_hash_ab.c test/harness/harness.c \
        $( [ "$(uname -s)" = "Linux" ] && echo -ldl ) -lm || {
        echo "hook_identity_ab: failed to build $TOOL" >&2
        exit 1
    }
fi

OUT="${VJ_HOOK_AB_OUT:-}"
CLEANUP=0
if [ -z "$OUT" ]; then
    OUT=$(mktemp -d "${TMPDIR:-/tmp}/vj_hook_ab.XXXXXX") || exit 1
    CLEANUP=1
fi

rc=0
compared=0

run_arm() {
    # run_arm <rom-tag> <arm-name> [extra args...]
    local tag="$1"; shift
    local arm="$1"; shift
    "$TOOL" "$CORE" "$ROM" --frames "$FRAMES" \
        --csv "$OUT/${tag}_${arm}.csv" --quiet "$@" >/dev/null 2>&1
    if [ ! -s "$OUT/${tag}_${arm}.csv" ]; then
        echo "  FAIL $tag/$arm: no CSV produced" >&2
        rc=1
        return 1
    fi
    return 0
}

for ROM in $ROMS; do
    if [ ! -f "$ROM" ]; then
        # NOT a skip-as-pass.  These ROMs are committed to the repo, so a
        # missing one means the checkout is wrong, not that the check is
        # optional -- and the `compared` tally below turns "every ROM was
        # missing" into a failure rather than a green run of nothing.
        echo "hook_identity_ab: SKIP (missing $ROM)" >&2
        continue
    fi
    TAG=$(basename "$ROM" .j64)
    echo "hook_identity_ab: $TAG ($FRAMES frames/arm)"

    run_arm "$TAG" base  || continue
    run_arm "$TAG" base2 || continue
    run_arm "$TAG" off --option virtualjaguar_enhancement_hooks=disabled || continue
    run_arm "$TAG" on  --option virtualjaguar_enhancement_hooks=enabled  || continue

    if ! cmp -s "$OUT/${TAG}_base.csv" "$OUT/${TAG}_base2.csv"; then
        echo "  FAIL $TAG: the two BASELINE runs differ -- this run is" >&2
        echo "       nondeterministic and no arm comparison below is" >&2
        echo "       interpretable.  Fix that before reading anything else." >&2
        rc=1
        continue
    fi
    echo "  PASS $TAG: baseline is deterministic (base == base2)"

    for arm in off on; do
        if cmp -s "$OUT/${TAG}_base.csv" "$OUT/${TAG}_${arm}.csv"; then
            echo "  PASS $TAG: gate=$arm is byte-identical to the default arm"
        else
            echo "  FAIL $TAG: gate=$arm DIVERGES from the default arm" >&2
            diff "$OUT/${TAG}_base.csv" "$OUT/${TAG}_${arm}.csv" | head -5 >&2
            rc=1
        fi
    done
    compared=$((compared + 1))
done

[ "$CLEANUP" = "1" ] && rm -rf "$OUT"

# A run that compared nothing must not report OK.  Without this the script
# exits 0 having skipped every ROM -- the skip-as-pass failure mode this
# repo has shipped before, and the reason the Makefile's claim that "none
# of them can skip" needed something other than a comment to enforce it.
if [ "$compared" = "0" ]; then
    echo "hook_identity_ab: FAILED -- 0 ROMs compared (none of: $ROMS)" >&2
    echo "       These are committed to the repo; a missing one means the" >&2
    echo "       checkout is wrong, not that this check is optional." >&2
    exit 1
fi

if [ "$rc" = "0" ]; then
    echo "hook_identity_ab: OK ($compared ROM(s)) -- the enhancement-hooks option cannot move the picture"
else
    echo "hook_identity_ab: FAILED" >&2
fi
exit $rc
