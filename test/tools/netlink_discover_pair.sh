#!/usr/bin/env bash
# netlink_discover_pair.sh -- two cores on one host: the server beacons,
# the client's peer table must populate.  SO_REUSEADDR/SO_REUSEPORT on the
# listener is what makes two instances on one machine work at all, which
# is exactly how every other netlink test runs.
set -u
# PID-spread, below Linux's ephemeral range (32768-60999).  A fixed port plus
# SO_REUSEPORT lets two concurrent `make test` runs silently share the socket
# and steal each other's beacons -- the same class the netlink TCP tests fixed.
VJ_DISC_PORT="${VJ_DISC_PORT:-$(( 23000 + ($$ % 4000) ))}"
export VJ_DISC_PORT
CORE="${1:?usage: netlink_discover_pair.sh <core>}"
BIN="$(dirname "$0")/netlink_discover_probe"
if [ ! -x "$BIN" ]; then
    echo "netlink_discover_pair: $BIN not built" >&2
    exit 1
fi

# Can this HOST deliver a broadcast between two local sockets, as seen by
# THIS binary?  Asked of the probe itself, not of a script interpreter:
# macOS grants Local Network permission PER BINARY, so probing from python3
# would test the wrong identity and let a denied core binary FAIL here
# instead of skipping.  Exit 77 -> ledgered skip, never a silent pass.
if ! "$BIN" --selftest; then
    echo "netlink_discover_pair: SKIP (host does not deliver UDP broadcast between local processes)" >&2
    exit 77
fi
"$BIN" "$CORE" --role beacon &
BPID=$!
sleep 1
"$BIN" "$CORE" --role listen --expect 1
rc=$?
kill "$BPID" 2>/dev/null; wait "$BPID" 2>/dev/null
if [ "$rc" -eq 0 ]; then
    echo "netlink_discover_pair: PASS"
else
    echo "netlink_discover_pair: FAIL (listener saw no peer)" >&2
fi
exit "$rc"
