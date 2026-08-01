# Jaguar Network Play — User Guide

Virtual Jaguar emulates the Jaguar's link hardware (JagLink / CatBox), so
networked titles — **Doom** (2-player deathmatch), **AirCars** (up to 8
players), **BattleSphere Gold** — can play against other emulator instances.

There are **two completely separate ways** to connect. They cannot talk to
each other — every player in a session must use the same one.

## Option A: RetroArch netplay (easiest — RetroArch to RetroArch only)

Requires RetroArch 1.16 or newer on every device (desktop, iOS, Android…).

1. Everyone loads the **same core build and the same ROM**.
2. Leave the core option *Network Link (JagLink / CatBox)* on **disabled** —
   netplay takes over the link automatically.
3. One player: *Netplay → Host → Start Netplay Host*.
4. Everyone else: *Netplay → Refresh Netplay Host List* — hosts on the same
   Wi-Fi/LAN **appear automatically**; select one. (Or *Connect to Netplay
   Host* and type the host's IP.)
5. In-game, set up link play as on real hardware (e.g. Doom: Game Mode →
   Deathmatch; AirCars: Game Selection → Two Player Direct Serial or
   Multiple Player Network).

Notes:
- On iOS, accept the "local network" permission prompt or discovery fails.
- Which side is the RetroArch "host" doesn't matter to the game — the link
  itself is symmetric.
- Known issue: with some RetroArch builds the `--connect` **command line
  flag** silently does nothing — use the Netplay menu instead.

## Option B: Direct TCP (any frontend on a platform with sockets)

Works in any frontend on desktop and mobile platforms (macOS, Windows,
Linux, iOS, Android), including mixing frontends (e.g. Provenance on
iPhone vs RetroArch on a Mac). Console targets without BSD-style sockets
build an inert stub — the TCP modes silently do nothing there.
**No auto-discovery** — you wire it up:

1. Pick one device as the hub. Set its core option
   *Network Link = TCP Host (listen)*.
2. Every other device: *Network Link = TCP Client (connect)*, and tell it
   the hub's LAN IP by creating a text file **`vj_netlink.txt`** in the
   frontend's *system/BIOS* directory containing just the IP, e.g.:
   ```
   192.168.1.20
   ```
   (Without the file, clients try `127.0.0.1` — same-machine only.)
3. *Network Link Port* must match on all devices (default 42171).
4. Load the same ROM everywhere and set up link play in-game.

Up to 8 units total (1 host + 7 clients) — enough for AirCars' CatNet.
Desktop/testing shortcuts: environment variables `VJ_NETLINK_HOST` and
`VJ_NETLINK_PORT` override the file and option.

## Troubleshooting

- **Never mix options A and B** in one session — a netplay host and a TCP
  client are different transports and will never see each other.
- Cross-device play needs a core build that actually contains networking.
  A frontend bundling an older core has no *Network Link* option at all —
  that's the tell.
- Both sides sit at their link menu but never connect: verify same
  Wi-Fi/LAN, the hub's IP in `vj_netlink.txt`, matching port, and any
  firewall prompt on the host machine.
- *Loopback (echo to self)* is a solo test mode: the console hears its own
  transmissions. Games will correctly report "no players found" — useful
  only to check the link menu works.
- Localhost/LAN latency is the supported envelope; internet play over VPNs
  (Tailscale etc.) may work but is best-effort.
- Play feels laggy on Wi-Fi even though ping looks fine: make sure
  *Network Link Latency Hiding* is enabled (it is by default). Without
  it every link exchange rounds up to whole video frames regardless of
  actual network speed. The wait adapts to the measured connection
  automatically — there is nothing to tune.
