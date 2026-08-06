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
# The assertions are ABSOLUTE, anchored to the 60 fps frame ceiling the
# bug imposes (issue #310): the probe paces retro_run at 60 fps, so a
# quantized control physically cannot exceed ~60 exchanges/sec, while
# the fixed path is uncapped and beats it.  A ratio (the old >= 1.5x
# check) compresses under machine load — both rates fall together and
# the test flaked on loaded runners and in CI with the netlink code
# unmodified.  When load drags even the enabled rate under the ceiling
# the effect cannot be demonstrated either way, and the test SKIPs
# loudly (suite skip ledger) instead of failing.
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
# Prints "<exchanges/sec> <frames/sec>" on stdout; returns nonzero on
# error.  frames/sec is the probe's achieved pacing rate — the load
# witness (0.0 if the probe binary predates the telemetry line).
run_case()
{
    local label="$1" core_port="$2" proxy_port="$3" wait_opt="$4"
    local probe_log rate fps
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
    grep -E "exchanges/sec|frames/sec|FAIL" "$probe_log" | sed "s/^/[$label] /" >&2
    rate="$(sed -n 's/.*\] \([0-9.]*\) exchanges\/sec.*/\1/p' "$probe_log" | head -1)"
    fps="$(sed -n 's/.*\] \([0-9.]*\) frames\/sec.*/\1/p' "$probe_log" | head -1)"
    rm -f "$probe_log"
    [ "$rv" -ne 0 ] && return "$rv"
    [ -z "$rate" ] && return 1
    echo "$rate ${fps:-0.0}"
    return 0
}

# skip <reason> — loud skip: prints in-context AND records in the suite
# skip ledger (scripts/test-skip.sh) when run from the repo, so an
# aborted-suite run can never masquerade as "nothing skipped".
ROOT="$(cd "$TOOLS/../.." && pwd)"
skip()
{
    echo "netlink_latency_test: SKIP ($*)"
    if [ -f "$ROOT/scripts/test-skip.sh" ]; then
        bash "$ROOT/scripts/test-skip.sh" record \
            "Netlink latency (frame quantization)" "$*"
    fi
    exit 0
}

out_off="$(run_case "control disabled" 42751 42761 disabled)" || {
    echo "netlink_latency_test: FAIL (control case errored)"; exit 1; }
out_on="$(run_case "fixed enabled" 42752 42762 enabled)" || {
    echo "netlink_latency_test: FAIL (enabled case errored)"; exit 1; }
read -r rate_off fps_off <<<"$out_off"
read -r rate_on  fps_on  <<<"$out_on"

# Chain sanity: something actually ping-ponged in the control case.
ok=$(awk -v off="$rate_off" 'BEGIN { print (off >= 5) ? "yes" : "no" }')
if [ "$ok" != "yes" ]; then
    echo "netlink_latency_test: FAIL (control=$rate_off/s — exchange chain" \
         "never ran; proxy/link problem, not a quantization verdict)"
    exit 1
fi

# The verdict is anchored to the 60/s frame ceiling.  The probe paces
# retro_run at 60 fps, and without the reply wait every delayed reply
# slips to the next frame — so a genuinely quantized control CANNOT
# exceed ~60 exchanges/sec no matter how fast the host is, and the
# fixed path proves itself by beating that ceiling.  Both claims are
# absolute: proportional machine-load slowdown moves both rates DOWN,
# never across these thresholds in the failing direction.
FRAME_CEILING=60

# Skip 1: control never quantized.  Frame quantization needs emulation
# to run much faster than real time: a frame's 68K spin then burns out
# in ~1 ms of wall clock and the delayed reply always slips to the next
# retro_run.  On heavily instrumented builds (ASan, gcov) run_frame is
# SLOWER than real time, the spin itself spans the network delay, and
# the control never quantizes (seen: 110/s on the ASan runner).  The
# bug physically cannot manifest there.
slow=$(awk -v off="$rate_off" -v c="$FRAME_CEILING" \
    'BEGIN { print (off > c) ? "yes" : "no" }')
if [ "$slow" = "yes" ]; then
    skip "control=$rate_off/s never quantized — runner emulates slower" \
         "than real time, cannot exhibit the bug"
fi

# Control was quantized (off <= 60).  Fixed path must beat the ceiling.
fast=$(awk -v on="$rate_on" -v c="$FRAME_CEILING" \
    'BEGIN { print (on > c) ? "yes" : "no" }')
if [ "$fast" = "yes" ]; then
    echo "netlink_latency_test: PASS (disabled=$rate_off quantized <=" \
         "$FRAME_CEILING/s, enabled=$rate_on beat the frame ceiling)"
    exit 0
fi

# Enabled landed under the ceiling too.  Two causes look alike in the
# exchange rates alone — a broken reply wait (enabled quantizes just
# like the control) or a runner too loaded for the uncapped path to
# clear 60/s — and the rates cannot disambiguate them: quantized-run
# variance (~40-57/s observed) swamps any lift threshold.  So FAIL
# only on positive evidence of quantization in the enabled case,
# using the probe's pacing telemetry:
#
#   quantized identity: a quantized run does <= 1 exchange per paced
#   frame (the control shows this exactly: e.g. 105 exchanges in 105
#   frames).  rate_on <= 1.05 * fps_on means the wait bought nothing.
#
#   pacing held: fps_on >= 45 means the probe kept a near-normal frame
#   cadence (idle macOS paces ~52-55 due to usleep overhead), so the
#   core HAD its frame slots and still never beat one exchange per
#   frame — that is the bug.  A load-crushed runner (CI flake: rates
#   21.9/29.5 imply ~30 fps) fails this and SKIPs loudly instead:
#   it cannot demonstrate the effect in either direction.
quant=$(awk -v on="$rate_on" -v fps="$fps_on" 'BEGIN {
    print (fps >= 45 && on <= 1.05 * fps) ? "yes" : "no" }')
if [ "$quant" != "yes" ]; then
    skip "disabled=$rate_off enabled=$rate_on (paced $fps_on fps) —" \
         "under the $FRAME_CEILING/s ceiling but not demonstrably" \
         "quantized; runner too slow/loaded to demonstrate the effect"
fi
echo "netlink_latency_test: FAIL (enabled=$rate_on <= 1 exchange per" \
     "paced frame at $fps_on fps, same frame quantization as the" \
     "control ($rate_off) — reply wait is not defeating receive-side" \
     "frame quantization)"
exit 1
