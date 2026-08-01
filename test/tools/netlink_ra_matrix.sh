#!/usr/bin/env bash
# netlink_ra_matrix.sh — drive the netpacket link under REAL RetroArch.
#
# Launches a netplay host and client of this core (windowed; macOS
# RetroArch has no true headless video) with the same content, waits for
# the session to establish, then greps both verbose logs for netplay +
# netpacket markers.  The windows stay up for RUN_SECS so a human can
# play the actual link game over RetroArch netplay.
#
# Usage: netlink_ra_matrix.sh <core> <rom> [RUN_SECS=45] [KEEP=0]
#   KEEP=1 leaves both instances running (manual play); the script still
#   reports the log verdict and exits.
set -u

CORE="$(cd "$(dirname "${1:?usage: netlink_ra_matrix.sh <core> <rom>}")" && pwd)/$(basename "$1")"
ROM="$(cd "$(dirname "${2:?usage: netlink_ra_matrix.sh <core> <rom>}")" && pwd)/$(basename "$2")"
RUN_SECS="${3:-45}"
KEEP="${4:-${KEEP:-0}}"

RA="${RETROARCH_BIN:-}"
if [ -z "$RA" ]; then
    if [ -x "/Applications/RetroArch.app/Contents/MacOS/RetroArch" ]; then
        RA="/Applications/RetroArch.app/Contents/MacOS/RetroArch"
    elif command -v retroarch >/dev/null 2>&1; then
        RA="$(command -v retroarch)"
    else
        echo "netlink_ra_matrix: RetroArch not found." >&2
        echo "  Install it:  brew install --cask retroarch" >&2
        exit 2
    fi
fi

WORK="${TMPDIR:-/tmp}/vj_ra_matrix.$$"
mkdir -p "$WORK"
APPEND="$WORK/append.cfg"
cat > "$APPEND" <<EOF
config_save_on_exit = "false"
netplay_public_announce = "false"
netplay_use_mitm_server = "false"
netplay_nat_traversal = "false"
pause_nonactive = "false"
video_fullscreen = "false"
EOF

echo "netlink_ra_matrix: host starting ($RA)"
"$RA" -v --appendconfig="$APPEND" --host -L "$CORE" "$ROM" \
    > "$WORK/host.log" 2>&1 &
HOST_PID=$!
sleep 5
echo "netlink_ra_matrix: client connecting"
"$RA" -v --appendconfig="$APPEND" --connect 127.0.0.1 -L "$CORE" "$ROM" \
    > "$WORK/client.log" 2>&1 &
CLIENT_PID=$!

sleep "$RUN_SECS"

HOST_OK=0
CLIENT_OK=0
grep -qiE "netplay.*(connected|joined|peer|client)" "$WORK/host.log" && HOST_OK=1
grep -qiE "netplay.*(connected|joined|paired|synchronized)" "$WORK/client.log" && CLIENT_OK=1
NP_MARK=0
grep -qiE "netpacket|vjag-netlink" "$WORK/host.log" "$WORK/client.log" && NP_MARK=1

echo "--- host log (netplay lines) ---"
grep -iE "netplay|netpacket" "$WORK/host.log" | tail -8
echo "--- client log (netplay lines) ---"
grep -iE "netplay|netpacket" "$WORK/client.log" | tail -8

if [ "$KEEP" != "1" ]; then
    kill "$HOST_PID" "$CLIENT_PID" 2>/dev/null
    wait "$HOST_PID" "$CLIENT_PID" 2>/dev/null
else
    echo "netlink_ra_matrix: KEEP=1 — instances left running (host=$HOST_PID client=$CLIENT_PID)"
fi

echo "logs: $WORK"
if [ "$HOST_OK" = 1 ] && [ "$CLIENT_OK" = 1 ]; then
    echo "netlink_ra_matrix: PASS (netplay session established; netpacket_marker=$NP_MARK)"
    exit 0
fi
echo "netlink_ra_matrix: FAIL (host_ok=$HOST_OK client_ok=$CLIENT_OK)" >&2
exit 1
