#!/usr/bin/env bash
# voicechat_pair_test.sh — two-process UDP voice-chat transport check (#485).
# Usage: voicechat_pair_test.sh [port]
set -u

PORT="${1:-${VJ_DISC_PORT:-$(( 25000 + ($$ % 4000) ))}}"
BIN="$(dirname "$0")/voicechat_pair"

if [ ! -x "$BIN" ]; then
    echo "voicechat_pair_test: $BIN not built" >&2
    exit 1
fi

run_pair() {
    local port="$1"
    local recv_rc send_rc recv_pid

    "$BIN" --role recv --port "$port" &
    recv_pid=$!
    sleep 0.3
    "$BIN" --role send --port "$port" --host 127.0.0.1
    send_rc=$?
    wait "$recv_pid"
    recv_rc=$?

    if [ "$recv_rc" -eq 0 ] && [ "$send_rc" -eq 0 ]; then
        echo "voicechat_pair_test: PASS (port $port)"
        return 0
    fi
    if [ "$recv_rc" -eq 2 ] || [ "$send_rc" -eq 2 ]; then
        return 2
    fi
    echo "voicechat_pair_test: FAIL (recv=$recv_rc send=$send_rc)" >&2
    return 1
}

port="$PORT"
for attempt in 1 2 3; do
    run_pair "$port"
    rc=$?
    [ "$rc" -ne 2 ] && exit "$rc"
    port=$((port + 211))
    echo "voicechat_pair_test: port busy, retrying on $port" >&2
done
echo "voicechat_pair_test: FAIL (no bindable port after 3 tries)" >&2
exit 1
