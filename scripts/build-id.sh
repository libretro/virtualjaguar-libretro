#!/usr/bin/env sh
# Prints the build identity embedded into the core by gen-version-h.sh:
# the short git rev, with a "-dirty" suffix when tracked files have
# uncommitted changes.  Test harnesses compare this against the version
# string reported by a loaded core (VJ_EXPECT_BUILD) so a stale or
# wrong-branch binary fails loudly instead of silently testing the
# wrong code.  POSIX sh compatible -- no bashisms.
#
# `--ignores` prints the ignore list below, one path per line, so a caller
# can check whether the file it is about to write is covered.

set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)

# Tracked files that are RESULTS of running the test tools, never inputs to
# the build.  Changes to these do not make the binary "dirty", because they
# cannot change a single byte of it.
#
# Why this list has to exist: a tool that records its results into a tracked
# file AND stamps those results with this build id cannot converge without
# it.  test/tools/cd_boot_matrix.sh computes the id once per invocation and
# writes rows stamped with it into docs/cd-boot-matrix.md; writing the first
# row would flip the tree to "-dirty", so the next chunked invocation would
# compute a different id, judge every row it had just written to be stale
# evidence, and re-run all of them -- forever.  Excluding the results file
# keeps the stamp meaningful (a real source edit still yields "-dirty")
# while letting a chunked sweep settle.
#
# Keep this minimal and results-only.  Anything that feeds a compile, a link
# or a code-generation step must NOT be listed here: the whole point of the
# stamp is that it changes when the emulator code under test changes.
#
# Repo-root-relative paths, ONE PER LINE -- the list is fed to `grep -vxF`,
# which reads a newline-separated pattern set.  Space-separating two entries
# makes a single literal pattern that matches no filename at all, silently
# restoring the non-convergence this list exists to prevent.
BUILD_ID_IGNORE='docs/cd-boot-matrix.md'

if [ "${1:-}" = "--ignores" ]; then
  printf '%s\n' "$BUILD_ID_IGNORE"
  exit 0
fi

REV=$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ "$REV" != "unknown" ]; then
  # Name filtering rather than an ':(exclude)' pathspec: it needs no
  # pathspec-magic support, and a malformed entry can only ever fail
  # towards "-dirty" (the safe direction) instead of silently matching
  # everything and disabling the guard.
  if (cd "$ROOT" && git diff-index --name-only HEAD -- 2>/dev/null) \
       | grep -vxF "$BUILD_ID_IGNORE" | grep -q .; then
    REV="${REV}-dirty"
  fi
fi
printf '%s\n' "$REV"
