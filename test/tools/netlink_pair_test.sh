#!/usr/bin/env bash
# netlink_pair_test.sh — two-process TCP link test driver.
# Launches a server-role and a client-role instance of the core on
# localhost and requires both to exchange their bytes (see
# netlink_pair.c).  Usage: netlink_pair_test.sh <core> [port]
#
# Round 1 dials the hub by IP (the resolver's AI_NUMERICHOST fast path).
# Round 2 dials it by *name*, which is the only coverage of the core's
# getaddrinfo path -- the path that makes "somebox.local" work.
#
# Round 2 deliberately uses "localhost" rather than this machine's own
# LAN name.  Both would prove the resolver, but a LAN address means an
# *inbound* connection, and a host firewall that allows loopback while
# dropping inbound LAN (macOS Application Firewall does exactly this to
# unsigned binaries) fails the round for reasons that have nothing to do
# with the core.  Loopback keeps the test measuring name resolution.
set -u

CORE="${1:?usage: netlink_pair_test.sh <core> [port]}"
# Default port: PID-spread inside 17000-20999 — BELOW the ephemeral
# range of every CI OS (Linux 32768+, macOS 49152+).  The old fixed
# 42171 sat inside Linux's ephemeral range, and a transient outgoing
# connection on the runner occasionally held it: the server's bind
# failed and the pair test flaked.  Explicit [port] / VJ_NETLINK_PORT
# still override.
PORT="${2:-${VJ_NETLINK_PORT:-$(( 17000 + ($$ % 4000) ))}}"
BIN="$(dirname "$0")/netlink_pair"

if [ ! -x "$BIN" ]; then
    echo "netlink_pair_test: $BIN not built" >&2
    exit 1
fi

# Run one server+client pair against $1 as the client's hub address.
# Echoes PASS/FAIL; returns the usual 0/1.
run_pair() {
    local host="$1" label="$2" port="$3"
    local server_rc client_rc server_pid

    "$BIN" "$CORE" --role server --port "$port" &
    server_pid=$!
    # Give the listener a moment before the client connects.
    sleep 0.3
    "$BIN" "$CORE" --role client --port "$port" --host "$host"
    client_rc=$?
    wait "$server_pid"
    server_rc=$?

    if [ "$server_rc" -eq 0 ] && [ "$client_rc" -eq 0 ]; then
        echo "netlink_pair_test: PASS ($label, host=$host, port $port)"
        return 0
    fi
    # Server exit 2 = bind failed (port held by something else on the
    # machine): the caller retries on a different port.
    if [ "$server_rc" -eq 2 ]; then
        return 2
    fi
    echo "netlink_pair_test: FAIL ($label, host=$host, server=$server_rc client=$client_rc)" >&2
    return 1
}

# run_pair, retrying on a shifted port when the server's bind fails.
run_pair_retry() {
    local host="$1" label="$2" port="$3" attempt rc
    for attempt in 1 2 3; do
        run_pair "$host" "$label" "$port"
        rc=$?
        [ "$rc" -ne 2 ] && return "$rc"
        port=$((port + 211))
        echo "netlink_pair_test: port busy, retrying $label on $port" >&2
    done
    echo "netlink_pair_test: FAIL ($label — no bindable port after 3 tries)" >&2
    return 1
}

run_pair_retry "127.0.0.1" "by IP" "$PORT" || exit 1
# Second round on the next port: the first server may still be in
# TIME_WAIT, and SO_REUSEADDR does not cover a listener that raced.
run_pair_retry "localhost" "by name" "$((PORT + 1))" || exit 1
exit 0
