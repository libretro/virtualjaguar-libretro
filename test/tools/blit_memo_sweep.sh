#!/usr/bin/env bash
# Corpus soundness sweep for blit memoization (issue #411).
#
# Runs test/tools/blit_memo_verify over every cartridge in the private
# ROM tree.  Verify mode never skips a blit: it executes each would-be
# skip live and compares the write log and post-launch state against
# what the memo would have replayed, so a divergence means enabling the
# memo on that title would change emulation.
#
# The sweep answers one question -- "is the memo's skip condition sound
# across the corpus, or only on the title it was designed against" --
# and it is the gate for adding `virtualjaguar_blit_memo` to any row in
# src/core/titledb.c.
#
# Per-title verdicts (see blit_memo_verify.c for the exit codes):
#   clean    verifications ran and all agreed
#   DIVERGE  at least one would-be skip was unsound   <-- the finding
#   thin     no verdict: the title never repeated a blit stream here
#            (usually it sits in attract/menu and the generic input
#            below never reaches gameplay).  NOT a pass.
#   noload   the core refused the dump (prototype/alpha formats with no
#            ROM-database row) -- a corpus fact, unrelated to the memo
#   error    the harness itself failed
#
# Generic input: a title-agnostic attract-buster (pause/A/B plus d-pad)
# that reaches gameplay in many titles and keeps it live.  Titles that
# need a real route come out `thin` and want a fixture in
# test/fixtures/ -- thin is a coverage gap, not a clean bill.
#
# Env:
#   CORE       core .dylib/.so           (default ./virtualjaguar_libretro.dylib)
#   ROMS_DIR   corpus root               (default test/roms/private/ROMS)
#   FRAMES     frames per title          (default 3000)
#   MIN_RUNS   verifications for verdict (default 1000)
#   JOBS       parallel titles           (default cores/2)
#   OUT        result CSV                (default /tmp/blit_memo_sweep.csv)
#   FILTER     grep -i pattern to select titles
#   EXTRA_OPTS extra harness args applied to every title, e.g.
#              "--option virtualjaguar_internal_resolution=2x
#               --option virtualjaguar_true_color=enabled
#               --option virtualjaguar_pertitle_defaults=disabled"
#              Sweeping with the shadow surfaces ON matters: the memo
#              replays shadow stores for skipped blits, and a sweep with
#              them off never exercises that path at all.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CORE=${CORE:-"$ROOT/virtualjaguar_libretro.dylib"}
[ -f "$CORE" ] || CORE="$ROOT/virtualjaguar_libretro.so"
ROMS_DIR=${ROMS_DIR:-"$ROOT/test/roms/private/ROMS"}
FRAMES=${FRAMES:-3000}
MIN_RUNS=${MIN_RUNS:-1000}
JOBS=${JOBS:-$(( $(getconf _NPROCESSORS_ONLN) / 2 ))}
OUT=${OUT:-/tmp/blit_memo_sweep.csv}
FILTER=${FILTER:-}
EXTRA_OPTS=${EXTRA_OPTS:-}
VERIFY="$ROOT/test/tools/blit_memo_verify"

[ "$JOBS" -lt 1 ] && JOBS=1

if [ ! -x "$VERIFY" ]; then
  echo "blit_memo_sweep: build the verifier first:" >&2
  echo "  cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \\" >&2
  echo "     -o test/tools/blit_memo_verify test/tools/blit_memo_verify.c \\" >&2
  echo "     test/harness/harness.c -ldl -lm" >&2
  exit 2
fi
if [ ! -f "$CORE" ]; then
  echo "blit_memo_sweep: core not found ($CORE); build with TEST_EXPORTS=1" >&2
  exit 2
fi
if [ ! -d "$ROMS_DIR" ]; then
  echo "blit_memo_sweep: ROM tree not found ($ROMS_DIR)" >&2
  echo "  ln -sfn \"\${JAGUAR_ROMS_PRIVATE:?}\" test/roms/private" >&2
  exit 2
fi

# Title-agnostic attract-buster.  Frames are absolute from boot; the
# early presses clear title/attract screens, the later d-pad holds keep
# a reached gameplay scene live so streams repeat.
PRESSES=()
for f in 300 420 540 660 780 900 1020 1140; do
  PRESSES+=(--press "$f:pause:8" --press "$((f + 60)):a:8")
done
d=(up right down left)
i=0
for f in $(seq 1300 100 "$FRAMES"); do
  PRESSES+=(--press "$f:${d[$((i % 4))]}:90")
  i=$((i + 1))
done

# Word-split EXTRA_OPTS deliberately (it is a flag list, not a path).
for o in $EXTRA_OPTS; do
  PRESSES+=("$o")
done

export CORE VERIFY FRAMES MIN_RUNS
export VJ_EXPECT_BUILD="${VJ_EXPECT_BUILD:-}"

run_one() {
  local rom="$1"; shift
  local base out rc runs fails misses dirty through
  base=$(basename "$rom")
  out=$("$VERIFY" "$CORE" "$rom" --frames "$FRAMES" --min-runs "$MIN_RUNS" \
        --quiet "$@" 2>&1)
  rc=$?
  runs=$(sed -n 's/.*BMVERIFY runs=\([0-9]*\).*/\1/p' <<<"$out" | tail -1)
  fails=$(sed -n 's/.*BMVERIFY.*fails=\([0-9]*\).*/\1/p' <<<"$out" | tail -1)
  misses=$(sed -n 's/.*BMVERIFY.*misses=\([0-9]*\).*/\1/p' <<<"$out" | tail -1)
  dirty=$(sed -n 's/.*BMVERIFY.*dirty=\([0-9]*\).*/\1/p' <<<"$out" | tail -1)
  through=$(sed -n 's/.*BMVERIFY.*through=\([0-9]*\).*/\1/p' <<<"$out" | tail -1)
  : "${runs:=0}" "${fails:=0}" "${misses:=0}" "${dirty:=0}" "${through:=0}"
  case $rc in
    0) verdict=clean ;;
    1) verdict=DIVERGE ;;
    3) verdict=thin ;;
    *)
      # The core refusing the dump (prototype/alpha formats with no ROM
      # database row) is a corpus fact, not a memo result -- keep it out
      # of the error bucket so a real harness failure stays visible.
      if grep -q 'retro_load_game failed\|unsupported or invalid content' <<<"$out"; then
        verdict=noload
      else
        verdict=error
      fi
      ;;
  esac
  # Title last and quoted: Jaguar dump names contain commas
  # ("Assassin Demo, The ..."), which would otherwise split the field.
  printf '%s,%s,%s,%s,%s,%s,"%s"\n' \
    "$verdict" "$runs" "$fails" "$misses" "$dirty" "$through" "$base"
}
export -f run_one

echo "verdict,verify_runs,verify_fails,misses,dirty,exec_through,title" > "$OUT"

# Plain read loop rather than mapfile: macOS ships bash 3.2 as /bin/bash
# and mapfile is a bash 4 builtin, so this script has to stay 3.2-clean or
# it breaks on a default macOS setup (it only worked here because
# /usr/bin/env bash found a Homebrew bash 5).
ROMS=()
while IFS= read -r rom; do
  [ -n "$rom" ] || continue
  if [ -n "$FILTER" ]; then
    printf '%s\n' "$rom" | grep -qi -- "$FILTER" || continue
  fi
  ROMS+=("$rom")
done < <(find -L "$ROMS_DIR" -maxdepth 1 -type f \
  \( -iname '*.j64' -o -iname '*.jag' -o -iname '*.rom' \) | sort)

if [ ${#ROMS[@]} -eq 0 ]; then
  echo "blit_memo_sweep: no ROMs matched in $ROMS_DIR${FILTER:+ (filter: $FILTER)}" >&2
  exit 2
fi

echo "blit_memo_sweep: ${#ROMS[@]} titles, ${FRAMES} frames, ${JOBS} jobs -> $OUT"

printf '%s\0' "${ROMS[@]}" | \
  xargs -0 -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {} "${PRESSES[@]}" \
  >> "$OUT"

echo
echo "=== blit_memo_sweep summary ==="
awk -F, 'NR>1 {n[$1]++} END {for (v in n) printf "  %-8s %d\n", v, n[v]}' "$OUT" | sort
echo
if awk -F, 'NR>1 && $1=="DIVERGE" {found=1} END {exit !found}' "$OUT"; then
  echo "DIVERGENCES (these titles must NOT be tagged):"
  awk -F, 'NR>1 && $1=="DIVERGE" {
             t=$0; sub(/^([^,]*,){6}/, "", t);
             printf "  %s  (fails=%s of %s checks)\n", t, $3, $2 }' "$OUT"
  echo
  echo "Full results: $OUT"
  exit 1
fi
echo "No divergences. Full results: $OUT"
exit 0
