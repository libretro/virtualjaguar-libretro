#!/usr/bin/env sh
# Prints the build identity embedded into the core by gen-version-h.sh:
# the short git rev, with a "-dirty" suffix when tracked files have
# uncommitted changes.  Test harnesses compare this against the version
# string reported by a loaded core (VJ_EXPECT_BUILD) so a stale or
# wrong-branch binary fails loudly instead of silently testing the
# wrong code.  POSIX sh compatible -- no bashisms.

set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)

REV=$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ "$REV" != "unknown" ]; then
  if ! (cd "$ROOT" && git diff-index --quiet HEAD -- 2>/dev/null); then
    REV="${REV}-dirty"
  fi
fi
printf '%s\n' "$REV"
