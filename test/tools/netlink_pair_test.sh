#!/usr/bin/env bash
# netlink_pair_test.sh — two-process TCP link test driver.
# Launches a server-role and a client-role instance of the core on
# localhost and requires both to exchange their bytes (see
# netlink_pair.c).  Usage: netlink_pair_test.sh <core> [port]
set -u

CORE="${1:?usage: netlink_pair_test.sh <core> [port]}"
PORT="${2:-${VJ_NETLINK_PORT:-42171}}"
BIN="$(dirname "$0")/netlink_pair"

if [ ! -x "$BIN" ]; then
    echo "netlink_pair_test: $BIN not built" >&2
    exit 1
fi

"$BIN" "$CORE" --role server --port "$PORT" &
SERVER_PID=$!
# Give the listener a moment before the client connects.
sleep 0.3
"$BIN" "$CORE" --role client --port "$PORT"
CLIENT_RC=$?
wait "$SERVER_PID"
SERVER_RC=$?

if [ "$SERVER_RC" -eq 0 ] && [ "$CLIENT_RC" -eq 0 ]; then
    echo "netlink_pair_test: PASS (port $PORT)"
    exit 0
fi
echo "netlink_pair_test: FAIL (server=$SERVER_RC client=$CLIENT_RC)" >&2
exit 1
