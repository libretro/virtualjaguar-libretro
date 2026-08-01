#!/usr/bin/env bash
# netlink_latency_test.sh — proves the netlink reply wait defeats
# receive-side frame quantization.  Both cases run the 68K ping-pong
# exchange through netlink_delay_proxy adding 3 ms each way (~Wi-Fi):
#
#   disabled: replies quantize to whole video frames — the exchange
#             rate collapses to ~1 per frame regardless of actual RTT.
#   enabled (default): the adaptive wait rides out the RTT inside the
#             frame and sustains a multiple of the quantized rate.
#
# CI runners vary widely in speed, so the assertion is a RATIO —
# enabled must beat disabled by >= 1.5x (measured ~2x locally, ~2.7x on
# GH Actions macOS) — plus an absolute ceiling on the control proving
# the injected delay actually quantized it.
#
# Usage: netlink_latency_test.sh <core>
set -u

CORE="${1:?usage: netlink_latency_test.sh <core>}"
TOOLS="$(cd "$(dirname "$0")" && pwd)"
PROXY_PID=""
PROBE_PID=""

cleanup()
{
    [ -n "$PROXY_PID" ] && kill "$PROXY_PID" 2>/dev/null
    [ -n "$PROBE_PID" ] && kill "$PROBE_PID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT
trap 'cleanup; trap - EXIT; exit 130' INT TERM

# run_case <label> <core_port> <proxy_port> <wait>
# Prints the measured exchanges/sec on stdout; returns nonzero on error.
run_case()
{
    local label="$1" core_port="$2" proxy_port="$3" wait_opt="$4"
    local probe_log rate
    probe_log="$(mktemp "${TMPDIR:-/tmp}/vj_nl_lat_XXXXXX.log")"

    "$TOOLS/netlink_delay_proxy" --listen "$proxy_port" \
        --connect "127.0.0.1:$core_port" --delay-ms 3 2>/dev/null &
    PROXY_PID=$!
    sleep 0.3
    "$TOOLS/netlink_latency" "$CORE" --role probe --port "$core_port" \
        --measure-sec 2 --wait "$wait_opt" >"$probe_log" 2>&1 &
    PROBE_PID=$!
    sleep 0.3
    "$TOOLS/netlink_latency" "$CORE" --role echo --port "$proxy_port" \
        --wait "$wait_opt" >/dev/null 2>&1
    wait "$PROBE_PID"
    local rv=$?
    PROBE_PID=""
    kill "$PROXY_PID" 2>/dev/null
    wait "$PROXY_PID" 2>/dev/null
    PROXY_PID=""
    grep -E "exchanges/sec|FAIL" "$probe_log" | sed "s/^/[$label] /" >&2
    rate="$(sed -n 's/.*\] \([0-9.]*\) exchanges\/sec.*/\1/p' "$probe_log" | head -1)"
    rm -f "$probe_log"
    [ "$rv" -ne 0 ] && return "$rv"
    [ -z "$rate" ] && return 1
    echo "$rate"
    return 0
}

rate_off="$(run_case "control disabled" 42751 42761 disabled)" || {
    echo "netlink_latency_test: FAIL (control case errored)"; exit 1; }
rate_on="$(run_case "fixed enabled" 42752 42762 enabled)" || {
    echo "netlink_latency_test: FAIL (enabled case errored)"; exit 1; }

# Frame quantization needs emulation to run much faster than real time:
# a frame's 68K spin then burns out in ~1 ms of wall clock and the
# delayed reply always slips to the next retro_run.  On heavily
# instrumented builds (ASan, gcov) run_frame is SLOWER than real time,
# the spin itself spans the network delay, and the control never
# quantizes (seen: 110/s on the ASan runner).  The bug physically
# cannot manifest there, so the test skips rather than asserting.
slow=$(awk -v off="$rate_off" 'BEGIN { print (off > 65) ? "yes" : "no" }')
if [ "$slow" = "yes" ]; then
    echo "netlink_latency_test: SKIP (control=$rate_off/s never quantized —" \
         "runner emulates slower than real time, cannot exhibit the bug)"
    exit 0
fi

# Control sanity: the chain ran, and the injected delay quantized it
# (paced 60 fps probe can't exceed ~60/s when every reply slips a frame).
ok=$(awk -v off="$rate_off" -v on="$rate_on" 'BEGIN {
    print (off >= 5 && on >= 1.5 * off) ? "yes" : "no" }')
if [ "$ok" != "yes" ]; then
    echo "netlink_latency_test: FAIL (disabled=$rate_off enabled=$rate_on;" \
         "need disabled >= 5 and enabled >= 1.5x disabled)"
    exit 1
fi
echo "netlink_latency_test: PASS (disabled=$rate_off enabled=$rate_on)"
