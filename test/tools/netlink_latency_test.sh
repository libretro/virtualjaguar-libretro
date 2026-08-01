#!/usr/bin/env bash
# netlink_latency_test.sh — proves the netlink reply wait defeats
# receive-side frame quantization.  Both cases run the 68K ping-pong
# exchange through netlink_delay_proxy adding 3 ms each way (~Wi-Fi):
#
#   wait=0  (disabled): replies quantize to whole video frames — the
#           exchange rate collapses to ~35/s regardless of actual RTT.
#           Asserted with --max-rate as the experiment control.
#   wait=12 (default):  the core waits out the RTT inside the frame and
#           sustains >2x the quantized rate.  Asserted with --min-rate.
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

run_case()
{
    # run_case <label> <core_port> <proxy_port> <wait_ms> <bound_flag> <bound>
    local label="$1" core_port="$2" proxy_port="$3" wait_ms="$4"
    local bound_flag="$5" bound="$6"
    local probe_log
    probe_log="$(mktemp "${TMPDIR:-/tmp}/vj_nl_lat_XXXXXX.log")"

    "$TOOLS/netlink_delay_proxy" --listen "$proxy_port" \
        --connect "127.0.0.1:$core_port" --delay-ms 3 2>/dev/null &
    PROXY_PID=$!
    sleep 0.3
    "$TOOLS/netlink_latency" "$CORE" --role probe --port "$core_port" \
        --measure-sec 2 --wait-ms "$wait_ms" \
        "$bound_flag" "$bound" >"$probe_log" 2>&1 &
    PROBE_PID=$!
    sleep 0.3
    "$TOOLS/netlink_latency" "$CORE" --role echo --port "$proxy_port" \
        --wait-ms "$wait_ms" >/dev/null 2>&1
    wait "$PROBE_PID"
    local rv=$?
    PROBE_PID=""
    kill "$PROXY_PID" 2>/dev/null
    wait "$PROXY_PID" 2>/dev/null
    PROXY_PID=""
    grep -E "exchanges/sec|FAIL" "$probe_log" | sed "s/^/[$label] /"
    rm -f "$probe_log"
    return $rv
}

fails=0

# Control: without the wait, the injected latency must visibly quantize
# the rate (also proves the proxy is actually in the path).
run_case "control wait=0" 42751 42761 0 --max-rate 55 || fails=$((fails+1))

# Fix: with the default wait the rate must clear the quantized ceiling.
run_case "fixed wait=12" 42752 42762 12 --min-rate 55 || fails=$((fails+1))

if [ "$fails" -ne 0 ]; then
    echo "netlink_latency_test: FAIL ($fails case(s))"
    exit 1
fi
echo "netlink_latency_test: PASS"
