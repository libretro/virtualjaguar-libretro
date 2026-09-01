#!/usr/bin/env sh
# Assert the msvc-check job's hand-maintained source list still matches
# Makefile.common.
#
# Makefile.common is the single source of truth for what makes up the core.
# The MSVC job in .github/workflows/c-cpp.yml has to repeat it, because
# cl.exe is driven from a workflow step and cannot read a Makefile.  A second
# hand-maintained copy of a list that changes every release is exactly the
# thing that rots, and it rots INVISIBLY here: everything stays green except
# Windows, and the error is a missing header rather than anything naming the
# real cause.
#
# It has already cost real time three times (issue #679): PR #624 (missing
# -I libretro-common/include in the by-hand CI compiles), scripts/c89-lint.sh
# (missing -Isrc/debug), and PR #662, where a brand-new src/debug/ directory
# built everywhere except MSVC x64 and x86 while ~35 other checks passed.
#
# Same posture as scripts/check-package-sources.sh, the version_fallback.h
# check and the docs/cd-boot-matrix.md drift guard: fail loudly, name the
# offending files, never edit the workflow.  A human decides what the right
# list is -- which matters more here than in the SPM case, because the MSVC
# job legitimately omits things and "excluded on purpose" must stay
# distinguishable from "forgotten".  That is what EXCLUDED below is for, and
# why every entry carries a reason.
#
# Usage: sh scripts/check-msvc-sources.sh
# POSIX sh -- no bashisms.

# -u as well as -e: an unset variable silently expanding to empty is exactly
# how a drift check starts passing on nothing.  `command rm` because this
# repo's shells alias rm to `rm -i`, which blocks forever with no tty.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

WF=.github/workflows/c-cpp.yml

[ -f Makefile.common ] || { echo "check-msvc-sources: Makefile.common not found" >&2; exit 1; }
[ -f "$WF" ]           || { echo "check-msvc-sources: $WF not found" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'command rm -rf "$TMP"' EXIT INT TERM

# --- sources the MSVC job deliberately does NOT compile --------------------
#
# Every entry needs a reason on its own line.  An exclusion without a reason
# is indistinguishable from an oversight, and telling those two apart is the
# entire value of this check.
cat > "$TMP/excluded" <<'EOF'
src/tom/blitter_simd_neon.c      arch-gated: exactly one BLITTER_SIMD_<ARCH> per cl.exe run; ARM only
src/tom/blitter_simd_sse2.c      arch-gated: compiled by the SECOND cl.exe step, on purpose
EOF

# --- sources the MSVC job does not compile YET -----------------------------
#
# This is DEBT, not design, and it is kept separate from the exclusions above
# so the two never blur together.  The job's step is named "Compile all
# sources" but has never compiled these; that gap predates this check and is
# tracked by issue #679.
#
# The rule for this list is: it may SHRINK, never grow.  A newly added source
# lands in neither list and therefore fails the check -- which is the whole
# point, and is exactly the #662 case (a brand-new src/debug/ directory that
# built everywhere except MSVC while ~35 other checks passed).
#
# Closing the gap means adding these to the first cl.exe step and letting
# Windows CI say whether they compile; that is separate work from installing
# the guard, and doing both at once would make a red MSVC job ambiguous.
cat > "$TMP/known_gap" <<'EOF'
src/cd/jagcd_bios.c
src/cd/jagcd_cart.c
src/cd/jagcd_hle.c
src/core/bus_arbiter.c
src/core/crash_detect.c
src/core/hookfile.c
src/core/jaggd.c
src/core/nvmbios.c
src/core/perf_counters.c
src/core/perf_iface.c
src/core/titledb.c
src/core/titlehook.c
src/core/vjtrace.c
src/jerry/axistune.c
src/jerry/inputdev.c
src/jerry/jlink.c
src/jerry/jlink_discover.c
src/jerry/jlink_netpacket.c
src/jerry/jlink_tcp.c
src/jerry/paddle.c
src/jerry/quadrature.c
src/jerry/uart.c
src/jerry/voicechat.c
src/m68000/cpudefs.c
src/m68000/cpuemu.c
src/m68000/cpuextra.c
src/m68000/cpustbl.c
src/m68000/readcpu.c
src/tom/blit_memo.c
src/tom/shadowfb.c
EOF

sed 's/[[:space:]].*//' "$TMP/excluded" | grep -v '^$' | sort -u > "$TMP/excl_only"
grep -v '^$' "$TMP/known_gap" | sort -u > "$TMP/gap"
cat "$TMP/excl_only" "$TMP/gap" | sort -u > "$TMP/excl"

# --- what Makefile.common says ---------------------------------------------
grep -oE '\$\(CORE_DIR\)/[a-zA-Z0-9_/.-]+\.c' Makefile.common \
  | sed 's|\$(CORE_DIR)/||' | sort -u > "$TMP/mk_all"
# libretro-common and deps are compiled through deps\libchdr\unity.c and are
# not listed file-by-file in the MSVC step, so they are out of scope here.
grep -v '^libretro-common/' "$TMP/mk_all" > "$TMP/mk_core"
comm -23 "$TMP/mk_core" "$TMP/excl" > "$TMP/mk_want"

# --- what the MSVC job says ------------------------------------------------
#
# ONLY the first cl.exe step.  The second compiles blitter.c +
# blitter_simd_sse2.c alone to prove the SSE2 shape, and is narrow on purpose
# -- flagging it would make this check cry wolf about a deliberate design.
STEP_START='Compile all sources with cl.exe'
STEP_END='Compile blitter with cl.exe'

# BOTH markers must exist, checked symmetrically.  A `sed` range whose END
# pattern never matches runs silently to EOF -- so a rename of the end marker
# alone would fold the SECOND cl.exe step into the parse and quietly break the
# "only the first step" invariant this check depends on, with no error.  That
# is benign today only by luck (blitter_simd_sse2.c is in Makefile.common and
# already excluded, so it trips neither the missing nor the stale branch), and
# luck is not an invariant.  Thanks to the Kimi review on PR #721 for catching
# that the guard below was asymmetric while its message said "names", plural.
for marker in "$STEP_START" "$STEP_END"; do
  grep -q "$marker" "$WF" || {
    echo "check-msvc-sources: step marker not found in $WF:" >&2
    echo "    $marker" >&2
    echo "  The step names this parser keys on were changed -- fix the parser," >&2
    echo "  do not delete the check." >&2
    exit 1
  }
done

sed -n "/$STEP_START/,/$STEP_END/p" "$WF" > "$TMP/step"

# Belt and braces: the range must have TERMINATED on the end marker, not run
# off the end of the file.
[ -s "$TMP/step" ] || {
  echo "check-msvc-sources: could not isolate the first cl.exe step in $WF." >&2
  exit 1
}
tail -n 1 "$TMP/step" | grep -q "$STEP_END" || {
  echo "check-msvc-sources: the first-step range did not terminate on" >&2
  echo "    $STEP_END" >&2
  echo "  so it ran to end-of-file and folded in the second cl.exe step." >&2
  echo "  Fix the parser, do not delete the check." >&2
  exit 1
}

grep -oE '(src\\[a-zA-Z0-9_\\.-]+\.c|(^|[[:space:]])libretro\.c)' "$TMP/step" \
  | tr -d ' ' | tr '\\' '/' | sort -u > "$TMP/msvc"

# --- include dirs ----------------------------------------------------------
grep -oE '/I[a-zA-Z0-9_\\.-]+' "$TMP/step" | sed 's|^/I||' | tr '\\' '/' \
  | sed 's|^\./||' | sort -u > "$TMP/msvc_inc"
# Makefile.common writes these as -I$(CORE_DIR)/src/... , so the variables
# have to be expanded before the two sides are comparable.  An earlier
# revision of this script matched a bare path and silently produced an EMPTY
# list, which made the include comparison below pass on nothing -- the same
# vacuous-check class this whole file exists to prevent.  Hence the
# emptiness guard on mk_inc.
# Only the unconditional `INCFLAGS :=` block.  Later `INCFLAGS +=` lines are
# platform-gated (msvc2003 gets a compat/msvc include the modern CI job
# correctly does not want), so folding them in would make this check demand
# a flag that must not be there.
sed -n '/^INCFLAGS :=/,/^$/p' Makefile.common \
  | grep -oE '\-I\$\([A-Z_]+\)[a-zA-Z0-9_/.-]*' \
  | sed 's|^-I||' \
  | sed 's|\$(LIBRETRO_COMM_DIR)|libretro-common|' \
  | sed 's|\$(LIBCHDR_DIR)|deps/libchdr|' \
  | sed 's|\$(CORE_DIR)/||; s|^\$(CORE_DIR)$|.|' \
  | grep -v '^$' | sort -u > "$TMP/mk_inc"

for f in mk_want msvc msvc_inc mk_inc; do
  [ -s "$TMP/$f" ] || {
    echo "check-msvc-sources: '$f' came out empty -- the parser is broken, not the lists." >&2
    exit 1
  }
done

# --- compare ---------------------------------------------------------------
status=0

missing=$(comm -23 "$TMP/mk_want" "$TMP/msvc")
if [ -n "$missing" ]; then
  status=1
  echo "ERROR: in Makefile.common but NOT compiled by the MSVC job:" >&2
  echo "$missing" | sed 's/^/    /' >&2
  echo "" >&2
  echo "  Add them to the first cl.exe step in $WF, or add an exclusion" >&2
  echo "  WITH A REASON to the EXCLUDED block in this script." >&2
fi

stale=$(comm -13 "$TMP/mk_core" "$TMP/msvc")
if [ -n "$stale" ]; then
  status=1
  echo "ERROR: compiled by the MSVC job but NOT in Makefile.common:" >&2
  echo "$stale" | sed 's/^/    /' >&2
fi

# An include dir the Makefile has and MSVC lacks is the #624 / #662 failure
# exactly: a new directory whose header cl.exe then cannot find.
inc_missing=$(comm -23 "$TMP/mk_inc" "$TMP/msvc_inc")
if [ -n "$inc_missing" ]; then
  status=1
  echo "ERROR: include dirs in Makefile.common but NOT in the MSVC job:" >&2
  echo "$inc_missing" | sed 's/^/    /' >&2
fi

if [ "$status" -ne 0 ]; then
  echo "" >&2
  echo "Makefile.common is the source of truth." >&2
  exit 1
fi

echo "check-msvc-sources.sh: OK -- $(wc -l < "$TMP/mk_want" | tr -d ' ') sources and $(wc -l < "$TMP/mk_inc" | tr -d ' ') include dirs agree"
echo "  $(grep -c . "$TMP/excl_only") deliberate exclusion(s); $(grep -c . "$TMP/gap") source(s) not yet compiled by the MSVC job (issue #679 -- this number may shrink, never grow)"
