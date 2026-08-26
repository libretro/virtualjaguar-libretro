#!/bin/bash
# Overclock gate-1 grid (#378): does a clock scale actually lift the INTERNAL
# frame rate of this title?
#
#   FRAMES=900 scripts/ocgrid.sh out.txt "<rom>" "<state>"
#
# Results are written to <out.txt> and echoed to stdout.  Pass /dev/stdout (or
# /dev/null) if you only want one of those.
#
# Measures framebuffer TRANSITIONS -- frames whose hash differs from the
# previous frame -- starting from a gameplay savestate.  Two guards, both
# earned the hard way (see docs/overclock-gate1-checklist.md):
#
#   * transitions == window length means the counter SATURATED.  That is
#     UNMEASURED, not a null: every frame differed, so a gain has nowhere to
#     show.  Reported as SATURATED, never as a number.
#   * the 0.5x underclock arm runs FIRST and always.  A flat 1x/1.5x/2x row
#     cannot distinguish a genuine frame cap from a dead measurement; if 0.5x
#     does not drop, the metric was never responding and the whole sweep is
#     void.  This is the control that both retracted result tables lacked.
#
# CORE may be overridden for a non-macOS library name, e.g.
#   CORE=./virtualjaguar_libretro.so scripts/ocgrid.sh ...
set -u

CORE=${CORE:-./virtualjaguar_libretro.dylib}
OUT=$1; shift
ROM=$1; shift
STATE=$1; shift
FRAMES=${FRAMES:-900}

TMP=$(mktemp -d)
# EXIT does the cleanup; INT/TERM must additionally *stop*. Without the
# second trap a Ctrl-C killed only the current cell's frame_hash_ab (the
# process group gets the signal), that cell was recorded FAILED, and the
# sweep carried on through six more multi-minute cells.
trap 'command rm -rf "$TMP"' EXIT
trap 'exit 130' INT TERM

emit() { printf '%s\n' "$*" | tee -a "$OUT"; }

: > "$OUT"
emit "rom:    $ROM"
emit "state:  $STATE"
emit "frames: $FRAMES  (saturation ceiling $((FRAMES - 1)))"
emit ""
emit "$(printf '%-22s %-12s %s' cell transitions verdict)"

# --- phase 1: measure every cell -------------------------------------------
# Two phases so the stock cell is known before anything is quoted against it:
# the 0.5x control is more useful printed as a percentage than as a raw count,
# and it has to run regardless of ordering.
cells="risc=0.5x,m68k=1x risc=1x,m68k=1x risc=1.5x,m68k=1x risc=2x,m68k=1x \
       risc=1x,m68k=1.5x risc=1.5x,m68k=1.5x risc=2x,m68k=1.5x"
base=""
for cell in $cells; do
    r=${cell#risc=}; r=${r%%,*}
    m=${cell##*m68k=}
    tag=$(printf '%s' "$cell" | tr ',=.' '___')
    csv=$TMP/$tag.csv

    if ! "./test/tools/frame_hash_ab" "$CORE" "$ROM" --csv "$csv" \
            --load-state "$STATE" --frames "$FRAMES" \
            --option "virtualjaguar_risc_clock_scale=$r" \
            --option "virtualjaguar_m68k_clock_scale=$m" >"$TMP/$tag.log" 2>&1; then
        printf 'FAILED\n' > "$TMP/$tag.res"
        continue
    fi
    t=$(awk -F, 'NR>1{if(p!=""&&$6!=p)n++;p=$6}END{print n+0}' "$csv")
    n=$(awk 'NR>1' "$csv" | wc -l | tr -d ' ')
    printf '%s %s\n' "$t" "$n" > "$TMP/$tag.res"
    # Only a MEASURED stock cell may become the baseline. A saturated one
    # is pinned to the ceiling, so quoting other arms against it would
    # print authoritative-looking percentages against a meaningless
    # number -- the exact trap this script exists to make un-missable.
    if [ "$cell" = "risc=1x,m68k=1x" ] && [ "$n" -gt 0 ] && [ "$t" -lt "$((n - 1))" ]; then
        base=$t
    fi
done

# --- phase 2: report --------------------------------------------------------
for cell in $cells; do
    tag=$(printf '%s' "$cell" | tr ',=.' '___')
    read -r t n < "$TMP/$tag.res" 2>/dev/null || { t=FAILED; n=""; }

    if [ "$t" = "FAILED" ]; then
        emit "$(printf '%-22s %-12s %s' "$cell" "-" "RUN FAILED")"
        tail -3 "$TMP/$tag.log" 2>/dev/null | sed 's/^/    /' | tee -a "$OUT"
        continue
    fi
    if [ "$n" -eq 0 ]; then
        v="NO FRAMES (state rejected? see checklist section 2)"
    elif [ "$t" -ge "$((n - 1))" ]; then
        v="SATURATED -- unmeasured, lengthen the window"
    elif [ -z "$base" ] || [ "$base" -eq 0 ]; then
        v="NO USABLE STOCK BASELINE -- stock cell saturated or static; nothing here is comparable"
    else
        v="$(awk -v a="$t" -v b="$base" 'BEGIN{printf "%+.1f%% vs stock", (a-b)*100.0/b}')"
        [ "$cell" = "risc=0.5x,m68k=1x" ] && v="$v   <- CONTROL: must drop, or the sweep is void"
    fi
    emit "$(printf '%-22s %-12s %s' "$cell" "$t/$n" "$v")"
done

emit ""
emit "Reminder: a pass here is gate 1 only. Gate 2 is a human play-test --"
emit "a title can render more frames while its logic runs too fast (#401)."
