#!/usr/bin/env sh
# Assert Package.swift's source lists still match Makefile.common.
#
# Makefile.common is the single source of truth for what makes up the core.
# Package.swift has to repeat it, because SPM needs the file list in the
# manifest and cannot read a Makefile.  A second hand-maintained copy of a list
# that changes every release is exactly the thing that rots: the downstream
# Provenance wrapper carried its own copy and drifted by 27 files over seven
# releases before anyone noticed, which is why this check exists rather than a
# comment asking people to remember.
#
# Fails loudly and names the offending files.  It never edits Package.swift --
# same posture as the docs/cd-boot-matrix.md drift guard and the
# version_fallback.h check: a human decides what the right list is.
#
# Usage: sh scripts/check-package-sources.sh
# POSIX sh -- no bashisms.

# -u as well as -e: these guard scripts are copied from, and an unset variable
# silently expanding to empty is exactly how a drift check starts passing on
# nothing.  `command rm` because this repo's shells alias rm to `rm -i`, which
# blocks forever with no tty.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

[ -f Makefile.common ] || { echo "check-package-sources: Makefile.common not found" >&2; exit 1; }
[ -f Package.swift ]   || { echo "check-package-sources: Package.swift not found" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'command rm -rf "$TMP"' EXIT INT TERM

# --- what Makefile.common says -------------------------------------------
# Every $(CORE_DIR)/....c and $(LIBRETRO_COMM_DIR)/....c mentioned anywhere in
# the file.  Deliberately includes the arch-conditional blitter_simd_*.c: SPM
# compiles all three variants and lets each guard itself (see
# src/tom/blitter_simd_arch.h), so the union is the correct comparison.
grep -oE '\$\(CORE_DIR\)/[a-zA-Z0-9_/.-]+\.c' Makefile.common \
  | sed 's|\$(CORE_DIR)/||' | sort -u > "$TMP/mk_core"
grep -oE '\$\(LIBRETRO_COMM_DIR\)/[a-zA-Z0-9_/.-]+\.c' Makefile.common \
  | sed 's|\$(LIBRETRO_COMM_DIR)/||' | sort -u > "$TMP/mk_lrc"

# --- what Package.swift says ---------------------------------------------
# Read each array by name so the two lists can never be confused for each other
# if the file is reordered.
extract_array() {
  # $1 = swift array variable name, $2 = output file
  sed -n "/^let $1: \[String\] = \[/,/^\]/p" Package.swift \
    | grep -oE '"[^"]+\.c"' | tr -d '"' | sort -u > "$2"
}
extract_array coreSources          "$TMP/pkg_core"
extract_array libretroCommonSources "$TMP/pkg_lrc"

for f in mk_core mk_lrc pkg_core pkg_lrc; do
  [ -s "$TMP/$f" ] || { echo "check-package-sources: '$f' came out empty -- the parser is broken, not the lists" >&2; exit 1; }
done

# --- compare ---------------------------------------------------------------
status=0
report() {
  # $1 = label, $2 = makefile list, $3 = package list
  missing=$(comm -23 "$2" "$3")
  extra=$(comm -13 "$2" "$3")
  if [ -n "$missing" ]; then
    status=1
    echo "ERROR: in Makefile.common but NOT in Package.swift ($1):" >&2
    echo "$missing" | sed 's/^/    /' >&2
  fi
  if [ -n "$extra" ]; then
    status=1
    echo "ERROR: in Package.swift but NOT in Makefile.common ($1):" >&2
    echo "$extra" | sed 's/^/    /' >&2
  fi
}

report "coreSources"           "$TMP/mk_core" "$TMP/pkg_core"
report "libretroCommonSources" "$TMP/mk_lrc"  "$TMP/pkg_lrc"

if [ "$status" -ne 0 ]; then
  echo "" >&2
  echo "Makefile.common is the source of truth.  Update the arrays in" >&2
  echo "Package.swift to match, then re-run this check." >&2
  exit 1
fi

echo "check-package-sources.sh: OK -- $(wc -l < "$TMP/mk_core" | tr -d ' ') core + $(wc -l < "$TMP/mk_lrc" | tr -d ' ') libretro-common sources agree"
