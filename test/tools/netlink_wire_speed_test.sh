#!/usr/bin/env bash
# netlink_wire_speed_test.sh — proves what the #498 wire-speed enhancement
# (virtualjaguar_netlink_speed) actually does to link latency, and what it
# does when only ONE side turns it on.
#
# Three real core pairs over TCP, same synthetic 68K ping-pong cart, the
# only difference being each side's virtualjaguar_netlink_speed:
#
#   off/off    both consoles stock          -> baseline exchanges/sec
#   4x/off     one console accelerated      -> in between
#   4x/4x      both accelerated             -> best
#
# Two things are being asserted, and the second matters more than the first:
#
#   1. Turning it on raises the exchange rate (it does something).
#   2. The MISMATCHED pair still exchanges, and lands BETWEEN the two
#      symmetric configurations.  A lockstep link is gated by the slower
#      side, so one-sided enablement can only under-deliver the benefit --
#      it must never break or stall the exchange.  If the mismatched run
#      ever comes back at zero, the option needs a both-sides guard rather
#      than a documentation note.
#
# The cart is driven at --asiclk 1999 (a ~13.2 ms character frame) on
# purpose.  At the tool's stock ASICLK of 1 a character costs ~13 us and
# NOTHING here would be measurable: the 60 fps frame slot, not the emulated
# wire, would set the rate in every configuration and all three runs would
# tie.  Both sides are also --pace'd, because a free-running echo burns its
# emulated wire time faster than real time and would hide exactly the
# asymmetry this script exists to measure.
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

if [ ! -x "$BIN" ]; then
    echo "netlink_wire_speed_test: $BIN not built" >&2
    echo "  Rebuild it with:  make TEST_EXPORTS=1 $BIN" >&2
    exit 1
fi
mkdir -p "$OUT"

# run_pair <tag> <probe_speed> <echo_speed> <port>; echoes exchanges/sec.
run_pair() {
    tag="$1"; pspeed="$2"; espeed="$3"; port="$4"
    "$BIN" "$CORE" --role echo --port "$port" --asiclk "$ASICLK" \
        --speed "$espeed" --wait disabled --pace \
        > "$OUT/$tag.echo.log" 2>&1 &
    epid=$!
    sleep 1
    "$BIN" "$CORE" --role probe --port "$port" --asiclk "$ASICLK" \
        --speed "$pspeed" --wait disabled --measure-sec "$SECS" \
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
    # assertions below need to tell "the option did nothing" apart from
    # "this machine could not hold 60 fps in any configuration".
    awk '/exchanges\/sec/  { ex = $2 }
         /frames\/sec paced/ { fps = $2 }
         END { print ex, fps }' "$OUT/$tag.probe.log"
}

R=$(run_pair off_off   disabled disabled "$BASE_PORT")      || exit 1
RATE_OFF=${R%% *};  FPS_OFF=${R##* }
R=$(run_pair four_off  4        disabled "$((BASE_PORT+1))") || exit 1
RATE_ONE=${R%% *};  FPS_ONE=${R##* }
R=$(run_pair four_four 4        4        "$((BASE_PORT+2))") || exit 1
RATE_BOTH=${R%% *}; FPS_BOTH=${R##* }

printf 'netlink_wire_speed_test: asiclk=%s  off/off=%s  4x/off=%s  4x/4x=%s exchanges/sec (paced %s/%s/%s fps)\n' \
    "$ASICLK" "$RATE_OFF" "$RATE_ONE" "$RATE_BOTH" "$FPS_OFF" "$FPS_ONE" "$FPS_BOTH"

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
# configuration that stalls the link reads as rate 0, and ruling that out
# for the MISMATCHED pair is the whole reason this script exists.
check "$RATE_OFF  > 0" "off/off exchanges (baseline alive)"
check "$RATE_ONE  > 0" "4x/off exchanges (mismatched pair does NOT stall)"
check "$RATE_BOTH > 0" "4x/4x exchanges"

# The ladder is a wall-clock measurement off two paced processes, so it
# needs the same load guard netlink_latency_test.sh uses: on a runner that
# could not hold its frame slots, the rates measure the machine, not the
# option, and a red line there would be noise. 1.10 rather than a tighter
# bound for the same reason -- measured headroom on an idle host is 1.34x
# (16.9 -> 22.6) and 2.3x (22.6 -> 52.5), so 1.10 still fails a genuinely
# inert option while surviving contention.
PACE_MIN="${VJ_WIRE_MIN_FPS:-45}"
paced=$(awk -v a="$FPS_OFF" -v b="$FPS_ONE" -v c="$FPS_BOTH" -v m="$PACE_MIN" \
    'BEGIN { print (a >= m && b >= m && c >= m) ? "yes" : "no" }')
if [ "$paced" = "yes" ]; then
    check "$RATE_ONE  > $RATE_OFF * 1.10" "one side accelerated beats stock"
    check "$RATE_BOTH > $RATE_ONE * 1.10" "both sides beats one side"
else
    echo "netlink_wire_speed_test: SKIP ladder assertions (paced" \
         "$FPS_OFF/$FPS_ONE/$FPS_BOTH fps, need >= $PACE_MIN) -- runner too" \
         "loaded to demonstrate the effect in either direction; liveness" \
         "checks above still applied"
    if [ -f "$(dirname "$0")/../../scripts/test-skip.sh" ]; then
        bash "$(dirname "$0")/../../scripts/test-skip.sh" record \
            "Netlink wire speed ladder (#498)" \
            "runner paced $FPS_OFF/$FPS_ONE/$FPS_BOTH fps, below $PACE_MIN"
    fi
fi

if [ "$rc" -eq 0 ]; then
    echo "netlink_wire_speed_test: PASS (ports $BASE_PORT-$((BASE_PORT+2)))"
    command rm -rf "$OUT"
else
    echo "netlink_wire_speed_test: logs in $OUT" >&2
fi
exit "$rc"
