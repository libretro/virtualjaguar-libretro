#!/usr/bin/env bash
#
# test/tools/fb_ab_sweep.sh — A/B framebuffer sweep across a ROM corpus.
#
# Runs every ROM under $ROMS_ROOT through fb_row_digest against two cores and
# classifies the difference with fb_row_diff.py.  Written for the AvP "brown
# bar" change (#178), where the fix is only acceptable if every differing
# pixel is a row the old build left unwritten becoming opaque black.
#
# Usage:
#   ROMS_ROOT=/path/to/roms \
#   STOCK_CORE=/path/to/stock.dylib \
#   PATCHED_CORE=./virtualjaguar_libretro.dylib \
#   [FRAMES=1200] [OUTDIR=/tmp/fbsweep] [PRESS="--press 700:b:8 ..."] \
#     bash test/tools/fb_ab_sweep.sh
#
# Exits non-zero if any title shows a difference the rule does not allow.

set -uo pipefail

ROMS_ROOT=${ROMS_ROOT:?set ROMS_ROOT to the ROM corpus root}
STOCK_CORE=${STOCK_CORE:?set STOCK_CORE to the reference build}
PATCHED_CORE=${PATCHED_CORE:-./virtualjaguar_libretro.dylib}
FRAMES=${FRAMES:-1200}
OUTDIR=${OUTDIR:-/tmp/fbsweep}
PRESS=${PRESS:-}
DIGEST=${DIGEST:-./test/tools/fb_row_digest}
DIFF=${DIFF:-./test/tools/fb_row_diff.py}

mkdir -p "$OUTDIR"
: > "$OUTDIR/summary.txt"

# Does this digest hold at least one video frame?
#
# Read the frame count out of the header rather than judging on file size.
# fb_row_digest patches that count in with an fseek at the END of the run, so
# a digest from an interrupted run is large but still reports zero -- and a
# size test would wave it through as valid, silently comparing against
# nothing.  A ROM the core refused to load leaves just the 16-byte header.
has_frames() {
    [ -f "$1" ] || { echo no; return; }
    if [ "$(od -An -tu4 -j8 -N4 "$1" | tr -d ' ')" -gt 0 ] 2>/dev/null; then
        echo yes
    else
        echo no
    fi
}

fail=0
count=0
changed=0

# shellcheck disable=SC2206
press_args=($PRESS)

while IFS= read -r rom; do
    name=$(basename "$rom")
    name=${name%.*}
    slug=$(printf '%s' "$name" | tr -c 'A-Za-z0-9' '_')
    count=$((count + 1))

    # The reference build does not change between iterations of the fix, so
    # keep its digest.  Delete $OUTDIR (or set REDO_STOCK=1) to force a re-run.
    if [ -s "$OUTDIR/$slug.stock.bin" ] && [ "${REDO_STOCK:-0}" != 1 ]; then
        s_out="frames=cached"
    else
        s_out=$("$DIGEST" "$STOCK_CORE" "$rom" --frames "$FRAMES" \
                    "${press_args[@]}" --out "$OUTDIR/$slug.stock.bin" --quiet 2>&1)
    fi
    p_out=$("$DIGEST" "$PATCHED_CORE" "$rom" --frames "$FRAMES" \
                "${press_args[@]}" --out "$OUTDIR/$slug.patched.bin" --quiet 2>&1)

    # A ROM the core refuses to load writes only the 16-byte header.  That is
    # a corpus fact, not a regression -- but it must be true of BOTH builds,
    # so report a one-sided failure loudly instead of skipping it.  Judge on
    # file size rather than stdout so the cached-stock path behaves the same.
    s_ok=$(has_frames "$OUTDIR/$slug.stock.bin")
    p_ok=$(has_frames "$OUTDIR/$slug.patched.bin")
    : "$s_out" "$p_out"
    if [ "$s_ok" != yes ] || [ "$p_ok" != yes ]; then
        if [ "$s_ok" = "$p_ok" ]; then
            echo "SKIP $name: neither build loads this ROM" \
                | tee -a "$OUTDIR/summary.txt"
        else
            echo "FAIL $name: loads on one build only (stock=$s_ok patched=$p_ok)" \
                | tee -a "$OUTDIR/summary.txt"
            fail=$((fail + 1))
        fi
        continue
    fi

    line=$(python3 "$DIFF" "$OUTDIR/$slug.stock.bin" "$OUTDIR/$slug.patched.bin" \
                   --label "$name")
    rc=$?
    echo "$line" | tee -a "$OUTDIR/summary.txt"
    [ $rc -ne 0 ] && fail=$((fail + 1))
    case "$line" in *"changed_frames=0 "*) ;; *) changed=$((changed + 1));; esac
done < <(find "$ROMS_ROOT" -type f \( -iname '*.j64' -o -iname '*.jag' \) | sort)

echo "" | tee -a "$OUTDIR/summary.txt"
echo "titles=$count changed=$changed unexpected=$fail" | tee -a "$OUTDIR/summary.txt"
exit $((fail > 0 ? 1 : 0))
