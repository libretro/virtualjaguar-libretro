#!/usr/bin/env bash
# test-skip.sh -- skip ledger for `make test`.
#
# WHY THIS EXISTS
# ---------------
# Several checks in the suite only run when an optional private ROM / disc
# image is present.  Historically each such site just printed "SKIP: ..." and
# fell through, so the suite still exited 0 -- a skip was indistinguishable
# from a pass.  That let the Skyhammer clipping sentinel sit inert for an
# unknown number of releases: the Makefile looked for a filename that does not
# exist in the corpus, printed one SKIP line among hundreds of lines of test
# output, and reported success.
#
# This is the same masked-failure class the repo was burned by in PR #170,
# where a "fix" silenced Iron Soldier 2 and the clipping test passed because
# silence has 0% saturation.  A skip that reads as a pass is that bug wearing
# a different hat.
#
# The ledger makes skips additive and visible: every skip site records one
# line, and the suite prints a consolidated summary at the end.  CI has none
# of the private ROMs, so skipping stays non-fatal by default -- but it is no
# longer silent, and `VJ_REQUIRE_ROMS=1` turns it into a hard failure for
# anyone who does have the corpus and wants the full suite enforced.
#
# Usage:
#   test-skip.sh reset                    truncate the ledger (call FIRST)
#   test-skip.sh record "<check>" "<why>" print + append one skip
#   test-skip.sh summary                  print the roll-up; exit 1 under
#                                         VJ_REQUIRE_ROMS=1 if any skips
#
# Ledger path defaults to test/.skipped-checks; override with VJ_SKIP_LOG.

set -u

log="${VJ_SKIP_LOG:-test/.skipped-checks}"
mode="${1:-}"

case "$mode" in
reset)
    # Truncate at the START of the run, never at the end.  A ledger left over
    # from a previous invocation would resurrect old rows as fresh skips --
    # the same stale-row failure mode documented for cd_boot_matrix.sh, where
    # a resumed sweep replayed an ancient Battle Morph result as a new one.
    mkdir -p "$(dirname "$log")" 2>/dev/null
    : > "$log"
    ;;

record)
    check="${2:-unnamed check}"
    reason="${3:-no reason given}"
    # Print inline too, so the skip is visible in-context as well as in the
    # roll-up.  Both come from this one call, so they cannot drift apart.
    printf '  SKIP: %s -- %s\n' "$check" "$reason"
    mkdir -p "$(dirname "$log")" 2>/dev/null
    printf '%s\t%s\n' "$check" "$reason" >> "$log"
    ;;

summary)
    echo ""
    if [ ! -s "$log" ]; then
        echo "=== Skipped checks: none -- every optional check ran ==="
        exit 0
    fi

    count=$(wc -l < "$log" | tr -d ' ')
    echo "=== Skipped checks ($count) ==="
    # Two columns: check name, then why.  awk (not column) so the output is
    # identical on macOS and Linux.
    awk -F'\t' '{ printf "  %-42s %s\n", $1, $2 }' "$log"
    echo ""
    echo "  These checks did NOT run.  They are not failures, but they are"
    echo "  not passes either -- most need optional private ROMs/discs that"
    echo "  CI does not have.  Re-run with VJ_REQUIRE_ROMS=1 to make any"
    echo "  skip a hard failure once you have the corpus in place."

    if [ "${VJ_REQUIRE_ROMS:-0}" = "1" ]; then
        echo ""
        echo "ERROR: VJ_REQUIRE_ROMS=1 and $count check(s) were skipped." >&2
        exit 1
    fi
    ;;

*)
    echo "usage: test-skip.sh {reset|record <check> <reason>|summary}" >&2
    exit 2
    ;;
esac
