# Jaguar Network Play — User Guide

Virtual Jaguar emulates the Jaguar's link hardware (JagLink / CatBox), so
networked titles — **Doom** (2-player deathmatch), **AirCars** (up to 8
players), **BattleSphere Gold** — can play against other emulator instances.

There are **two completely separate ways** to connect. They cannot talk to
each other — every player in a session must use the same one.

**If everyone is on RetroArch, use Option A and stop reading there.** It
needs no IP address from you at all: RetroArch finds hosts on the LAN
itself. Option B exists for mixed frontends (Provenance on an iPhone
against RetroArch on a Mac) and for frontends without netplay.

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
- **Voice chat** (core option *Voice Chat*) works over netplay when both
  sides enable it and run a core that speaks `vjag-netlink-2`. If the peer
  never confirms voice the link stays data-only — but the offer keeps
  repeating, so turning Voice Chat on after the session is already up
  still works. Cores still on `vjag-netlink-1` cannot join a
  `vjag-netlink-2` session (update both).
- The microphone has to reach the core too: in RetroArch, *Settings >
  Audio > Microphone* must be On, the Microphone Device must not be
  `null`, and the app needs OS microphone permission. When the core
  cannot get a mic it says so once in the log (`[VOICE] frontend offers
  no microphone interface`) and voice becomes receive-only — you still
  hear the other player.

## Option B: Direct TCP (any frontend on a platform with sockets)

Works in any frontend on desktop and mobile platforms (macOS, Windows,
Linux, iOS, Android), including mixing frontends (e.g. Provenance on
iPhone vs RetroArch on a Mac). Console targets without BSD-style sockets
build an inert stub — the TCP modes silently do nothing there.
**No auto-discovery** — you wire it up:

1. Pick one device as the hub. Set its core option
   *Network Link = TCP Host (listen)*.
2. Every other device: *Network Link = TCP Client (connect)*, and tell it
   where the hub is via the *Network Link Host* core option. This takes
   an IP, a DNS name, **or a Bonjour/mDNS `.local` name** — so on a
   typical home LAN you can skip looking the IP up and just use the
   hub's computer name:
   ```
   jaghub.local
   ```
   A `.local` name survives the hub getting a different DHCP lease
   tomorrow, which a hard-coded IP does not. Find it in *System Settings
   → General → About → Name* on macOS, or `hostname` on Linux; Windows
   hosts need Bonjour installed to answer to one, so use their IP.

   Frontends with free-text option entry (e.g. Provenance) let you type
   any of these directly. In stock RetroArch (dropdown-only options) pick
   *From file* and create a text file **`vj_netlink.txt`** in the
   frontend's *system/BIOS* directory containing just the address, e.g.:
   ```
   192.168.1.20
   ```
   (Default is `127.0.0.1` — same-machine only.)
3. *Network Link Port* must match on all devices (default 42171).
4. Load the same ROM everywhere and set up link play in-game.

Up to 8 units total (1 host + 7 clients) — enough for AirCars' CatNet.
Desktop/testing shortcuts: environment variables `VJ_NETLINK_HOST` and
`VJ_NETLINK_PORT` override both the options and the file.

## Making link play feel snappier (optional, not authentic)

*Network Link Wire Speed* (default **Off**, values **Off** / **Auto**)
clocks the emulated serial port faster than the real hardware ran it, when
the console on the other end agrees to.

You do not need it to play. What it fixes is a specific feel: in a
strictly lockstep title neither console can draw the next frame until the
pad data has been swapped both ways, and the Jaguar's link is slow enough
that the swap can outlast the frame it belongs to — so your own move shows
up a frame late even on a perfect connection. Ultra Vortek's Voice Modem
mode is the clearest case: it settles at 19200 baud and trades about ten
bytes each way every frame, roughly 5.8 ms of pure wire time per
direction against a 16.7 ms frame.

That slowness is *real* — a Voice Modem or a JagLink cable behaved exactly
this way — which is why this is off by default and labelled an
enhancement rather than a fix.

- **Set *Auto* on either or both consoles — there is nothing to match.**
  The two cores agree on the speedup between themselves the moment the
  link comes up. If the other side is running an older core, has this set
  to *Off*, or never answers, this side quietly stays at authentic timing
  instead of running ahead on its own — a mismatch is no longer something
  you can accidentally create.
- Only works over a direct *Network Link* (TCP host/client). Frontend
  netplay (RetroArch's own netplay session) has no channel for the two
  cores to negotiate over, so link play there always runs authentic
  timing regardless of this setting.
- If a game starts behaving oddly on the link, put it back to Off before
  reporting anything — link timing bug reports are only meaningful at
  Off.
- The setting does nothing at all unless *Network Link* is actually
  selected, so leaving it on will never affect a single-player session.

This is a different knob from *Network Link Latency Hiding*, which deals
with the **network** between the two machines. Wire Speed deals with the
emulated **cable**, and helps even at zero network latency.

## Troubleshooting

- **Never mix options A and B** in one session — a netplay host and a TCP
  client are different transports and will never see each other.
- Cross-device play needs a core build that actually contains networking.
  A frontend bundling an older core has no *Network Link* option at all —
  that's the tell.
- Both sides sit at their link menu but never connect: verify same
  Wi-Fi/LAN, the hub's address in *Network Link Host* (or
  `vj_netlink.txt`), matching port, and any firewall prompt on the host
  machine. A host firewall is the most common cause when the address is
  definitely right — macOS's Application Firewall, for instance, allows
  loopback but silently drops inbound LAN connections to an app that has
  not been allowed, so the client looks connected while the hub never
  sees anyone arrive.
- A `.local` name that never connects while the raw IP works: the hub is
  not answering mDNS. Windows only does after Bonjour is installed, and
  some Wi-Fi routers block multicast between clients ("AP isolation").
  Use the IP there.
- The client retries on its own — a wrong-then-corrected address, a hub
  started later, or a `.local` name whose owner is not on the network
  yet all recover without reloading the core. A failed *name lookup*
  backs off about five seconds between attempts, a refused connection
  about half a second.
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
