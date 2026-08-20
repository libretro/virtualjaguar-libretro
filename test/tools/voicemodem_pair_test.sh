#!/usr/bin/env bash
# voicemodem_pair_test.sh — two-process Voice Modem handshake test driver.
# Launches an answer-role and a dial-role instance of the core on
# localhost and requires both to complete the Ultra Vortek modem
# choreography (see voicemodem_pair.c).
# Usage: voicemodem_pair_test.sh <core> [port]
set -u

CORE="${1:?usage: voicemodem_pair_test.sh <core> [port]}"
# Port default mirrors netlink_pair_test.sh: PID-spread inside
# 21000-24999, below every CI OS's ephemeral range, and disjoint from
# netlink_pair_test's 17000-20999 so the two suites never collide.
PORT="${2:-${VJ_NETLINK_PORT:-$(( 21000 + ($$ % 4000) ))}}"
BIN="$(dirname "$0")/voicemodem_pair"

if [ ! -x "$BIN" ]; then
    echo "voicemodem_pair_test: $BIN not built" >&2
    exit 1
fi

run_pair() {
    local port="$1"
    local answer_rc dial_rc answer_pid

    "$BIN" "$CORE" --role answer --port "$port" &
    answer_pid=$!
    sleep 0.3
    "$BIN" "$CORE" --role dial --port "$port" --host 127.0.0.1
    dial_rc=$?
    wait "$answer_pid"
    answer_rc=$?

    if [ "$answer_rc" -eq 0 ] && [ "$dial_rc" -eq 0 ]; then
        echo "voicemodem_pair_test: PASS (port $port)"
        return 0
    fi
    if [ "$answer_rc" -eq 2 ]; then
        return 2
    fi
    echo "voicemodem_pair_test: FAIL (answer=$answer_rc dial=$dial_rc)" >&2
    return 1
}

port="$PORT"
for attempt in 1 2 3; do
    run_pair "$port"
    rc=$?
    [ "$rc" -ne 2 ] && exit "$rc"
    port=$((port + 211))
    echo "voicemodem_pair_test: port busy, retrying on $port" >&2
done
echo "voicemodem_pair_test: FAIL (no bindable port after 3 tries)" >&2
exit 1
