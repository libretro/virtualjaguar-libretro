#!/bin/bash
# corpus_ab_sweep.sh -- A/B two cores across a ROM corpus and report every
# title whose framebuffer stream differs.
#
#   test/tools/corpus_ab_sweep.sh <base-core> <new-core> [rom-dir]
#
# Env: FRAMES (default 300)   frames per run
#      TMO    (default 60)    per-run timeout, seconds
#
# WHY THE TIMEOUT IS NOT OPTIONAL
#
# At least one ROM in the private corpus (Chroma-Luma Color Pick, issue #659)
# makes retro_run never return -- an unbounded blit, invisible to the crash
# watchdog because every watchdog signature is checked between frames. The
# first version of this sweep had no timeout, wedged on it, and sat there for
# 2h23m emitting nothing. The regression in #641 reached develop because
# "still running" was read as progress rather than as a hang, so the sweep
# that would have caught it never reported.
#
# Hence: every run is bounded, a hang is one visible SKIP line, and progress
# is printed as it goes so an empty tail is distinguishable from a stall.
#
# Compares the FULL hash stream, not a summary. Two cores can agree on the
# transition count and still render different pixels -- that is exactly the
# #641/#632 signature (Super Burnout: 143 transitions on both, different
# frames), so a count comparison would have called that "no change".
set -u

die() { printf 'corpus_ab_sweep: %s\n' "$*" >&2; exit 2; }

BASE=${1:-}; NEW=${2:-}
ROMDIR=${3:-${VJ_ROMS:-test/roms/private}}
[ -n "$BASE" ] && [ -n "$NEW" ] || die "usage: corpus_ab_sweep.sh <base-core> <new-core> [rom-dir]"
[ -f "$BASE" ] || die "base core not found: $BASE"
[ -f "$NEW"  ] || die "new core not found: $NEW"
[ -d "$ROMDIR" ] || die "rom dir not found: $ROMDIR (set VJ_ROMS or pass one)"

HASH_AB=./test/tools/frame_hash_ab
[ -x "$HASH_AB" ] || die "$HASH_AB not built -- see its header for the cc line"

FRAMES=${FRAMES:-300}
TMO=${TMO:-60}
TO=$(command -v gtimeout || command -v timeout) || die "need timeout(1) or gtimeout(1)"

# md5 on macOS, md5sum elsewhere.
if command -v md5 >/dev/null 2>&1; then MD5() { md5 -q "$1"; }
else MD5() { md5sum "$1" | cut -d' ' -f1; }; fi

OUT=$(mktemp -d) || die "mktemp failed"
# EXIT cleans up; INT/TERM must additionally stop, or Ctrl-C kills only the
# current frame_hash_ab and the sweep grinds on through the rest.
trap 'command rm -rf "$OUT"' EXIT
trap 'exit 130' INT TERM

find_roms() {
   find -L "$ROMDIR" -maxdepth 1 -type f \
        \( -iname '*.jag' -o -iname '*.j64' -o -iname '*.rom' \) | sort
}

total=$(find_roms | wc -l | tr -d ' ')
[ "$total" -gt 0 ] || die "no ROMs found under $ROMDIR"
echo "corpus_ab_sweep: $total ROMs, $FRAMES frames, ${TMO}s timeout"
echo "  base: $BASE"
echo "  new:  $NEW"

n_same=0; n_diff=0; n_skip=0; i=0
while IFS= read -r rom; do
   i=$((i + 1)); b=$(basename "$rom")
   if ! $TO "$TMO" "$HASH_AB" "$BASE" "$rom" --csv "$OUT/b.csv" \
           --frames "$FRAMES" >/dev/null 2>&1; then
      printf 'SKIP  [%3d/%3d] %-58s (base timeout/fail)\n' "$i" "$total" "${b:0:58}"
      n_skip=$((n_skip + 1)); continue
   fi
   if ! $TO "$TMO" "$HASH_AB" "$NEW" "$rom" --csv "$OUT/f.csv" \
           --frames "$FRAMES" >/dev/null 2>&1; then
      printf 'SKIP  [%3d/%3d] %-58s (new timeout/fail)\n' "$i" "$total" "${b:0:58}"
      n_skip=$((n_skip + 1)); continue
   fi

   if [ "$(MD5 "$OUT/b.csv")" = "$(MD5 "$OUT/f.csv")" ]; then
      n_same=$((n_same + 1))
   else
      tb=$(awk -F, 'NR>1{if(p!=""&&$6!=p)n++;p=$6}END{print n+0}' "$OUT/b.csv")
      tf=$(awk -F, 'NR>1{if(p!=""&&$6!=p)n++;p=$6}END{print n+0}' "$OUT/f.csv")
      nd=$(paste -d'|' <(awk -F, 'NR>1{print $6}' "$OUT/b.csv") \
                       <(awk -F, 'NR>1{print $6}' "$OUT/f.csv") \
           | awk -F'|' '$1!=$2{n++}END{print n+0}')
      printf 'DIFF  [%3d/%3d] %-58s transitions %s->%s, %s frames differ\n' \
             "$i" "$total" "${b:0:58}" "$tb" "$tf" "$nd"
      n_diff=$((n_diff + 1))
   fi
   [ $((i % 25)) -eq 0 ] && printf '      ... %d/%d (same=%d diff=%d skip=%d)\n' \
                                   "$i" "$total" "$n_same" "$n_diff" "$n_skip"
done < <(find_roms)

echo "---- identical: $n_same   differing: $n_diff   skipped: $n_skip   (of $total) ----"
# Differences are the point of the run, not a failure; a SKIP is, because it
# means a ROM went unchecked and the result is not the clean sweep it looks
# like.
[ "$n_skip" -eq 0 ] || exit 1
