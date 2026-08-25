#!/usr/bin/env bash
# test/tools/dram_scale_sweep.sh -- regression gate for issue #406.
#
# The #406 deadlock is a knife-edge race, NOT a slowness threshold: the
# wedge relocates whenever the timing model changes (scale 8 wedged
# before the Verilator constants landed and runs clean after; scale 3
# started wedging at the same commit).  A fix validated at one
# VJ_DRAM_SCALE value therefore proves nothing -- it just moves the
# knife edge.  This sweeps the whole calibration range with BOTH cost
# models enabled and fails if any single scale stops presenting.
#
# Wedge detector: present_rate_probe reports flips per window of
# fields.  Deliberately NOT a flips==0 cliff test -- a "fix" that turns
# the hard stop into a 3-flips-per-300 crawl would sail through that,
# and the pre-fix sweep already produced such windows (scale 5 window 5
# read 100.00 fields/flip, i.e. 3 flips).  The gate is a LIVENESS FLOOR:
# healthy post-warmup windows run ~90-150 flips per 300 fields, so
# anything under VJ_MIN_FLIPS (default 30) is a stall, dead or crawling.
# The minimum per scale is always printed so a liveness regression is
# visible even on a pass.
#
# Warmup: the first two windows are exempt.  Window 1 is boot/logo (one
# static image, legitimately 60-75 fields/flip) and window 2 is the
# level load, which slows monotonically with the scale (measured 6.25 ->
# 10.34 -> 13.64 -> 18.75 -> 23.08 for scales 4..8) -- that is the cost
# model working, not a stall.
#
# Usage:
#   test/tools/dram_scale_sweep.sh [core] [rom] [frames]
# Env:
#   VJ_SCALES     scales to sweep (default 1..16, the full legal range of
#                 busArbiter.contention_scale; 1..8 is the mandated floor)
#   VJ_WINDOW     fields per report window (default 300)
#   VJ_MIN_FLIPS  liveness floor per scored window (default 30)
#   VJ_WARMUP_W   leading windows exempt from the floor (default 2)
#
# Exits 0 when every scale runs clean, 1 on any wedge, 77 when the ROM
# is absent (private corpus -- skip, never a silent pass).

set -u

here="$(cd "$(dirname "$0")/../.." && pwd)"
core="${1:-$here/virtualjaguar_libretro.dylib}"
[ -f "$core" ] || core="$here/virtualjaguar_libretro.so"
# Corpus spellings differ per machine, and this script reports a MISSING rom
# as exit 77 = SKIP.  A hardcoded default therefore turns "your Doom is called
# something else" into a silently green gate -- the same way the Skyhammer
# clipping sentinel went inert looking for "Skyhammer_(1999).jag".  Fall back
# to the shared lookup before concluding the corpus has no Doom.
rom="${2:-}"
if [ -z "$rom" ]; then
    rom="$here/test/roms/private/ROMS/Doom - Evil Unleashed (1994).jag"
    if [ ! -f "$rom" ]; then
        rom=$( cd "$here" && bash scripts/find-rom.sh \
                 'Doom - Evil Unleashed*' 'Doom (World)*' 'Doom*' 2>/dev/null ) || rom=""
        case "$rom" in ""|/*) ;; *) rom="$here/$rom" ;; esac
    fi
fi
frames="${3:-1500}"
scales="${VJ_SCALES:-1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16}"
window="${VJ_WINDOW:-300}"
min_flips="${VJ_MIN_FLIPS:-30}"
warmup="${VJ_WARMUP_W:-2}"
probe="$here/test/tools/present_rate_probe"

if [ ! -f "$core" ]; then
    echo "SKIP: core not built ($core)"
    exit 77
fi
if [ ! -f "$rom" ]; then
    echo "SKIP: ROM not present ($rom)"
    exit 77
fi
if [ ! -x "$probe" ]; then
    echo "building present_rate_probe"
    ${CC:-cc} -O2 -Wall -std=c99 -I"$here" -I"$here/libretro-common/include" \
        -o "$probe" "$here/test/tools/present_rate_probe.c" \
        "$here/test/harness/harness.c" -ldl -lm || exit 1
fi

fail=0
echo "#406 DRAM-scale sweep: dram_timing + gpu_pipeline_timing"
echo "  scales: $scales   window: $window fields   floor: $min_flips flips (after $warmup warmup windows)"
for s in $scales; do
    out=$(VJ_DRAM_SCALE="$s" "$probe" "$core" "$rom" \
            --frames "$frames" --quiet --window "$window" \
            --option virtualjaguar_dram_timing=enabled \
            --option virtualjaguar_gpu_pipeline_timing=enabled 2>&1)
    rc=$?
    flips=$(printf '%s\n' "$out" | sed -n 's/^w[0-9]* *flips=\([0-9]*\) .*/\1/p')
    stalled=$(printf '%s\n' "$flips" | awk -v w="$warmup" -v m="$min_flips" \
        'NR>w && $1+0 < m { n++ } END { print n+0 }')
    minflip=$(printf '%s\n' "$flips" | awk -v w="$warmup" \
        'NR>w { if (n=="" || $1+0 < n) n=$1+0 } END { print (n=="" ? "n/a" : n) }')
    all=$(printf '%s\n' "$flips" | tr '\n' ' ')
    if [ "$rc" -ne 0 ]; then
        echo "scale $s: ERROR rc=$rc"
        printf '%s\n' "$out" | tail -5
        fail=1
    elif [ -z "$flips" ]; then
        echo "scale $s: ERROR no windows reported (frames < window?)"
        fail=1
    elif [ "$stalled" -gt 0 ]; then
        echo "scale $s: STALLED  $stalled window(s) below $min_flips  min=$minflip  flips: $all"
        fail=1
    else
        echo "scale $s: ok       min=$minflip  flips: $all"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAIL: #406 wedge/stall reachable"
    exit 1
fi
echo "PASS: live at every scale ($scales)"
exit 0
