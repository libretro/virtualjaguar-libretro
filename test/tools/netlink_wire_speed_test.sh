#!/usr/bin/env bash
# netlink_wire_speed_test.sh — proves what the #498/#552 wire-speedup
# mechanism (UARTFrameUsec()'s divisor, applied via jlink.c's
# VJ_FORCE_WIRE_SPEEDUP test-only escape hatch -- see
# test/tools/netlink_latency.c's file header) actually does to link
# throughput under REAL event-driven emulation timing.
#
# Two real core pairs over TCP, same synthetic 68K ping-pong cart, the
# only difference being whether the wire-speedup divisor is forced on:
#
#   off/off  both consoles stock        -> baseline exchanges/sec
#   N/N      both forced to N (default 4x) -> should beat baseline
#
# #552 retired the MISMATCHED (one-side-only) configuration this script
# used to also drive: the real virtualjaguar_netlink_speed option only
# ever offers disabled/auto now, and "auto" only ever accelerates a side
# once its peer has independently confirmed the same -- a side running
# alone at a faster rate cannot happen through real option + negotiation
# any more, so there is nothing left to test it against. What "one side
# faster, does the link still complete" needs is proven differently now:
# test/test_jlink_negotiate.c asserts a client with a silent (never-
# confirming) peer stays at stock for as long as it keeps trying, which
# is the actual safety property #552 replaced the mismatch tolerance
# with.
#
# The cart is driven at --asiclk 1999 (a ~13.2 ms character frame) on
# purpose.  At the tool's stock ASICLK of 1 a character costs ~13 us and
# NOTHING here would be measurable: the 60 fps frame slot, not the emulated
# wire, would set the rate in every configuration and both runs would tie.
# Both sides are also --pace'd, because a free-running echo burns its
# emulated wire time faster than real time and would hide the effect this
# script exists to measure.
#
# --wait disabled throughout: the adaptive reply wait hides NETWORK
# latency, which on loopback is ~0.  Leaving it on only adds wall-clock
# noise on top of the emulated wire time being measured.
#
# Usage: netlink_wire_speed_test.sh <core> [base_port]
# Exit codes: 0 pass, 1 fail.
set -u

CORE="${1:?usage: netlink_wire_speed_test.sh <core> [base_port]}"
# Never >= 32768: the Linux ephemeral range is where CI collided on
# EADDRINUSE (see project_ci_port_flake).  PID-spread inside a fixed band,
# the same pattern the Makefile uses for the other netlink tools.
BASE_PORT="${2:-$(( 29000 + ($$ % 3000) ))}"
BIN="$(dirname "$0")/netlink_latency"
OUT="${TMPDIR:-/tmp}/vj_wire_speed_$$"
ASICLK="${VJ_WIRE_ASICLK:-1999}"
SECS="${VJ_WIRE_SECS:-2}"
SPEEDUP="${VJ_WIRE_SPEEDUP:-4}"

if [ ! -x "$BIN" ]; then
    echo "netlink_wire_speed_test: $BIN not built" >&2
    echo "  Rebuild it with:  make TEST_EXPORTS=1 $BIN" >&2
    exit 1
fi
mkdir -p "$OUT"

# run_pair <tag> <speed> <port>; echoes exchanges/sec.  Both sides always
# get the same --speed: see the header comment for why a mismatch is no
# longer a configuration worth driving here.
run_pair() {
    tag="$1"; speed="$2"; port="$3"
    "$BIN" "$CORE" --role echo --port "$port" --asiclk "$ASICLK" \
        --speed "$speed" --wait disabled --pace \
        > "$OUT/$tag.echo.log" 2>&1 &
    epid=$!
    sleep 1
    "$BIN" "$CORE" --role probe --port "$port" --asiclk "$ASICLK" \
        --speed "$speed" --wait disabled --measure-sec "$SECS" \
        > "$OUT/$tag.probe.log" 2>&1
    prc=$?
    wait "$epid" 2>/dev/null || true
    if [ "$prc" -ne 0 ]; then
        echo "netlink_wire_speed_test: FAIL ($tag probe exited $prc;" \
             "log: $OUT/$tag.probe.log)" >&2
        return 1
    fi
    # "<exchanges/sec> <frames/sec paced>".  The pacing figure is the
    # runner's own achieved frame cadence -- the load telemetry the ratio
    # assertion below needs to tell "the mechanism did nothing" apart from
    # "this machine could not hold 60 fps in either configuration".
    awk '/exchanges\/sec/  { ex = $2 }
         /frames\/sec paced/ { fps = $2 }
         END { print ex, fps }' "$OUT/$tag.probe.log"
}

R=$(run_pair off_off "disabled" "$BASE_PORT")           || exit 1
RATE_OFF=${R%% *};  FPS_OFF=${R##* }
R=$(run_pair fast_fast "$SPEEDUP" "$((BASE_PORT+1))")   || exit 1
RATE_FAST=${R%% *}; FPS_FAST=${R##* }

printf 'netlink_wire_speed_test: asiclk=%s  off/off=%s  %sx/%sx=%s exchanges/sec (paced %s/%s fps)\n' \
    "$ASICLK" "$RATE_OFF" "$SPEEDUP" "$SPEEDUP" "$RATE_FAST" "$FPS_OFF" "$FPS_FAST"

rc=0
check() {
    if awk "BEGIN { exit !($1) }"; then
        echo "netlink_wire_speed_test: PASS $2"
    else
        echo "netlink_wire_speed_test: FAIL $2" >&2
        rc=1
    fi
}

# Liveness is the load-bearing assertion and always a hard failure: a
# configuration that stalls the link reads as rate 0.
check "$RATE_OFF  > 0" "off/off exchanges (baseline alive)"
check "$RATE_FAST > 0" "${SPEEDUP}x/${SPEEDUP}x exchanges"

# The ladder is a wall-clock measurement off two paced processes, so it
# needs the same load guard netlink_latency_test.sh uses: on a runner that
# could not hold its frame slots, the rates measure the machine, not the
# mechanism, and a red line there would be noise. 1.10 rather than a
# tighter bound for the same reason -- measured headroom on an idle host
# is 1.34x-2.3x across the old three-way ladder, so 1.10 still fails a
# genuinely inert mechanism while surviving contention.
PACE_MIN="${VJ_WIRE_MIN_FPS:-45}"
paced=$(awk -v a="$FPS_OFF" -v b="$FPS_FAST" -v m="$PACE_MIN" \
    'BEGIN { print (a >= m && b >= m) ? "yes" : "no" }')
if [ "$paced" = "yes" ]; then
    check "$RATE_FAST > $RATE_OFF * 1.10" "forced divisor beats stock"
else
    echo "netlink_wire_speed_test: SKIP ladder assertion (paced" \
         "$FPS_OFF/$FPS_FAST fps, need >= $PACE_MIN) -- runner too loaded" \
         "to demonstrate the effect; liveness checks above still applied"
    if [ -f "$(dirname "$0")/../../scripts/test-skip.sh" ]; then
        bash "$(dirname "$0")/../../scripts/test-skip.sh" record \
            "Netlink wire speed ladder (#498/#552)" \
            "runner paced $FPS_OFF/$FPS_FAST fps, below $PACE_MIN"
    fi
fi

if [ "$rc" -eq 0 ]; then
    echo "netlink_wire_speed_test: PASS (ports $BASE_PORT-$((BASE_PORT+1)))"
    command rm -rf "$OUT"
else
    echo "netlink_wire_speed_test: logs in $OUT" >&2
fi
exit "$rc"
