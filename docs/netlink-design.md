# Jaguar Link Networking (JERRY UART + Pluggable Transport)

**Date:** 2026-07-31
**Status:** COMPLETE — all four phases implemented and play-tested (Doom
deathmatch confirmed full speed under RetroArch netplay after the sub-frame
latency fix, PR #244). Known limitations as of this writing: internet play
is best-effort (localhost/LAN validated); BattleSphere Gold validated to the
networked lobby, sustained dogfight play unverified; AirCars under
*netpacket* netplay untested (its interrupt-driven RX does not hit the
ASISTAT pump path — TCP mode fully validated); iOS/Provenance TCP mode not
yet device-tested.

**Since resolved (v3.4.0):** iOS TCP mode is device-tested and confirmed
working. The setup/discovery/OSD-narration overhaul and a video-corruption
fix (missing TOM interrupt gate on the JERRY UART IRQ, `ea42736`) both
shipped in v3.4.0 — see `docs/netlink-ux-design.md` and
`docs/netlink-user-guide.md` for current behaviour; treat those two as
authoritative over this section for anything they cover.
**Merged:** PR #250 / #263 lineage.

## Overview

Add emulation of the Atari Jaguar's inter-console link hardware so networked
titles — BattleSphere Gold, AirCars, and Jaguar Doom's 2-player deathmatch —
can play against other emulator instances. Every real-world link peripheral
(Atari's JagLink RJ11 interface and ICD's CatBox with its multi-drop CatNet
bus) attaches to the DSP port and drives the same silicon: JERRY's
asynchronous serial UART. Emulating that UART once, behind a small pluggable
byte-transport interface, covers all of them.

## Current state (verified 2026-07-31)

- Reads/writes to the UART registers `$F10030`–`$F10034` fall through to
  plain JERRY RAM (`src/jerry/jerry.c`, tail of the read/write dispatchers).
  A game probing for a link partner reads back its own writes — nonsense.
- `IRQ2_ASI` (0x10) is defined in `src/jerry/jerry.h` but never raised.
- The J_ASYNENA enable bit and J_ASYNCLR clear bit already exist in the
  JINTCTRL documentation/handling.
- Bundled `libretro.h` already declares
  `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE` (env 78) and the
  `retro_netpacket_callback` types.
- The in-tree `libretro-common/` is trimmed and has **no `net/`** — the
  socket abstraction must be vendored from upstream.

## Why standard RetroArch netplay does not apply

RetroArch's classic netplay frame-syncs the *inputs of one shared emulated
console*. Jaguar link play is N separate consoles, each running its own
machine, exchanging serial bytes. The correct RetroArch mechanism is the
**netpacket interface** (env 78), which turns a netplay session into a raw
packet pipe between independently running cores (as used by DOSBox-Pure for
IPX). That is phase 3; a direct TCP transport ships first because it works in
every frontend (including Provenance on iOS/macOS) and enables headless
automated testing.

## Goals

1. Hardware-accurate JERRY UART emulation (registers, status bits, baud
   pacing, interrupts), verified against the JTRM PDFs in
   `docs/atari-jaguar-1999/` — not source comments.
2. Pluggable link transport: loopback, TCP, libretro netpacket.
3. Two-instance link play of BattleSphere Gold and Doom over TCP on
   localhost/LAN; netpacket play under real RetroArch netplay.
4. Automated tests at every layer, runnable headlessly on this Mac.

## Non-goals (for now)

- Internet-scale matchmaking or a public relay service (DCNet-style). Nothing
  exists for Jaguar today; the TCP backend plus user-supplied tunneling
  (Tailscale etc.) is the interim story. A relay is a separate future project.
- Modeling CatBox RS-232 DSP-port registers beyond the UART itself, MIDI, or
  ComLynx cross-compatibility.
- Cycle-exact UART timing beyond baud-rate pacing (start/8-data/stop bit
  times); games rate-limit on TBE/RBF status, not on bit edges.

## Architecture

### 1. UART core — new `src/jerry/uart.c` / `uart.h`

Register behavior (JTRM "Asynchronous Serial" section; the distilled map is
`docs/jtrm-jerry.md`, to be re-verified against the PDFs during planning):

| Address   | Name    | Behavior |
|-----------|---------|----------|
| `$F10030` | ASIDATA | W: load TX holding register, clear TBE, schedule TX-complete event at baud. R: pop RX buffer, clear RBF. |
| `$F10032` | ASICTRL (W) | ODD/PAREN/TXOPOL/RXIPOL config; TINTEN/RINTEN interrupt enables; CLRERR clears PE/FE/OE; TXBRK. |
| `$F10032` | ASISTAT (R) | TBE (bit 8), RBF (bit 7), PE/FE/OE (9–11), SERIN (13), TXBRK (14), ERROR (15 = PE|FE|OE), plus config readback. |
| `$F10034` | ASICLK  | Baud divider N; baud = sysclock / (16 × (N+1)) per the distilled doc — **verify the ×16 vs ×32 factor against the JTRM PDF before implementation**. |

- **TX path:** ASIDATA write → TBE clears → event scheduled via the existing
  event system (`src/core/event.c`) at 10 bit-times → byte handed to the
  active transport → TBE sets → `IRQ2_ASI` raised if TINTEN and J_ASYNENA.
- **RX path:** transport-delivered bytes queue in a small RX FIFO, delivered
  to ASIDATA at baud pacing; RBF sets, `IRQ2_ASI` if RINTEN; OE latches if a
  byte lands while RBF is still set. Parity/framing errors are only
  synthesized where a transport can express them (loopback tests).
- **Interrupt route:** `jerryPendingInterrupt |= IRQ2_ASI` gated by
  J_ASYNENA, delivered over the existing JINTCTRL → 68K IPL2 path. Whether
  the UART can also interrupt the DSP is a JTRM question resolved during
  planning; the 68K path is the known-required one.
- **Dispatch:** `JERRYReadWord/WriteWord` (and byte variants) get a
  `$F10030–$F10035` branch calling into `uart.c`, placed before the RAM
  fall-through.
- **Savestates:** UART registers, FIFOs, and pending-event state serialize
  with the rest of JERRY. Live socket state does not; loading a state during
  a session behaves like a pulled link cable (games treat it as link drop).
- **Reset/iOS:** full static-state reset in init/deinit (iOS cannot dlclose
  cores — established project rule).

### 2. Link transport — `src/jerry/jlink.c` / `jlink.h`

Minimal internal API consumed only by `uart.c`:

```c
int  JLinkOpen(void);          /* per core option config */
void JLinkClose(void);
void JLinkSendByte(uint8_t b);
int  JLinkRecvByte(uint8_t *b); /* nonblocking, 0 = none */
void JLinkPoll(void);           /* once per frame from retro_run */
int  JLinkConnected(void);
```

Backends selected by core option:

- **loopback** — TX feeds RX after the baud delay. Zero network dependencies;
  drives unit tests and lets a user smoke-test a game's link menu solo.
- **tcp** (phase 2) — one instance is `tcp_server` (listen), the other
  `tcp_client` (connect). Nonblocking BSD sockets with `TCP_NODELAY`; socket
  files vendored from upstream libretro-common `net/net_socket.*` (MIT) into
  the tree, giving POSIX (macOS/iOS/Linux/Android) + winsock coverage from
  one codebase. Native per-platform frameworks (Network.framework etc.) are
  unnecessary at ~31 kbaud serial rates. 2 endpoints in this phase.
- **netpacket** (phase 3) — `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE`;
  bytes batched per frame, sent `RETRO_NETPACKET_RELIABLE |
  RETRO_NETPACKET_FLUSH_HINT`, broadcast to peers. Gives RetroArch ≥ 1.16
  lobby-based connection UX. Cores in frontends without env-78 support fall
  back cleanly (env call returns false → option hidden/inert).
- **multi-node hub** (phase 4, CatNet/AirCars) — TCP server rebroadcasts each
  byte stream to all other endpoints, modeling the shared multi-drop wire.
  Topology questions (whether a node hears its own TX) resolved against
  AirCars' actual protocol behavior during that phase.

### 3. Core options (`libretro_core_options.h`)

- `virtualjaguar_netlink`: `disabled` | `loopback` | `tcp_server` |
  `tcp_client` | `netpacket` (phase 3) | `auto` (added #467, now the
  default — see "Transport selection" below; UART is still emulated in
  every mode: registers behave, SERIN reads idle, nothing connects while
  the resolved mode has no live peer).
- `virtualjaguar_netlink_port`: enum of a few ports (default 42171).
- Host for `tcp_client`: `virtualjaguar_netlink_host` option. Stock
  RetroArch options cannot take free text, so the option ships preset
  values (`127.0.0.1`, and a `vj_netlink.txt` sentinel meaning "read
  `<system_dir>/vj_netlink.txt`"), but the core accepts ANY string the
  frontend returns — frontends with free-text option UIs (Provenance)
  supply arbitrary addresses directly. Resolution order: env
  `VJ_NETLINK_HOST`, then the option (sentinel defers to the file), then
  `<system_dir>/vj_netlink.txt`, then `127.0.0.1`. Since #467 the value
  list is also rebuilt at runtime with LAN-discovered hosts spliced in
  ahead of the `jaghub.local`/`vj_netlink.txt` presets — see "Transport
  selection" below. The `vj_netlink.txt` file itself is **one line, the
  host address only, no port, trailing newline optional** — `fgets` reads
  the first line and strips trailing CR/LF/space. The port always comes
  from `virtualjaguar_netlink_port`, never from the file.

### 3a. Transport selection & LAN discovery (2026-08-18 addendum, #467)

Full design rationale, wire format, and the iOS Local-Network permission
testing trap live in [`docs/netlink-ux-design.md`](netlink-ux-design.md);
this is the short version for readers of the base design.

`virtualjaguar_netlink` resolves to exactly one of three transport
families at any given time:

| Family | Modes | Selection |
|---|---|---|
| Frontend netplay | `netpacket` | Automatic whenever a RetroArch ≥ 1.16 netplay session is live (env 78, already unconditional); `auto` resolves here first. |
| Direct TCP | `tcp_server`, `tcp_client` | Explicit user choice. `tcp_client` additionally needs a host, resolved per the order above. |
| Idle | `disabled`, `loopback`, or `auto` with no netplay session | No transport (`disabled`), local TX→RX echo only (`loopback`), or genuinely nothing yet (`auto` idle — the OSD says so; see below). |

`auto` (default since #467) is netplay-when-live-else-idle **only** — it
does not restore a previously chosen direct mode (there is nowhere to
read a prior choice from with a single option key) and it never
auto-connects to a discovered peer. A "call" the Voice Modem places
without the player asking for it is exactly the surprise this design
avoids.

**LAN discovery beacon.** `tcp_server` (and `tcp_client`, listen-only) run
a UDP broadcast beacon/listener on port **42170** — deliberately outside
the `virtualjaguar_netlink_port` range (42171–42174) so the two can never
collide. Fixed 40-byte packet: 4-byte magic `VJAG`, 1-byte protocol
version, 1-byte device (0 = JagLink/CatBox, 1 = Voice Modem), 2-byte
big-endian TCP port, 32-byte NUL-padded hostname. A host also beacons
(so peers can see it) while listening (so it can see other peers too); a
client only listens — neither dials out on its own. Peers expire after
10 s; the peer table caps at 8 entries and drives a rebuilt
`virtualjaguar_netlink_host` value list, rate-limited to at most one
`SET_CORE_OPTIONS_V2` re-send per 2 s. Discovery deliberately does **not**
run in `auto` or `disabled` — starting a UDP listener on every load for
every user, most of whom never touch networking, would trip the OS Local
Network permission prompt unconditionally.

Because the beacon carries device type, a Voice Modem host discovered by
a JagLink client (or the reverse) is flagged both in the host picker's
label and with an on-screen warning — the two products share the wire
protocol's transport but never interoperate at the device layer, and
today that failure is otherwise silent.

**iOS/tvOS Bonjour constraint.** mDNS is not used for discovery, on any
platform, and this is a hard platform constraint rather than a stylistic
choice: RetroArch's iOS/tvOS `Info.plist` declares only `_ra-bless._tcp`
and `_ra_netplay._tcp` under `NSBonjourServices`; iOS permits only
enumerated service types, so a core advertising `_vjaglink._tcp` is
silently blocked with no error the core could detect or work around.
UDP broadcast has no such allowlist — iOS/tvOS both carry the
`com.apple.developer.networking.multicast` entitlement (covers broadcast)
and declare `NSLocalNetworkUsageDescription` — so broadcast discovery
works on exactly the platform where mDNS cannot, making it the primary
mechanism rather than a fallback. Unblocking Bonjour needs an upstream
RetroArch `Info.plist` change and is tracked as a separate follow-up, not
part of this core.

### 4. Testing

ROM corpus: the private ROM tree symlinked at `test/roms/private` (see
CLAUDE.md; discover with `find -L`).

1. **Unit (phase 1):** harness-based (`test/harness/`) loopback test driving
   the real registers via `harness_dlsym` — write ASIDATA, poll ASISTAT for
   TBE/RBF transitions, assert byte round-trip, IRQ latch in
   `jerryPendingInterrupt`, OE on deliberate overrun, CLRERR behavior.
   Added to `make test`.
2. **Two-instance TCP (phase 2):** `test/tools/netlink_pair_test` — forks two
   processes, each dlopens the core (separate address spaces, so statics are
   safe), server + client on localhost, asserts cross-instance byte streams
   in both directions. Also added to `make test` (localhost only, no external
   network).
3. **Game acceptance (phase 2):** scripted `--press` navigation of
   BattleSphere Gold and Doom into their link/comm menus on two linked
   headless instances; the bar is **mutual link detection**. `cd_visual_verify`
   -style screenshot output for agent-readable confirmation.
4. **Real-RetroArch matrix (phase 3):** `test/tools/netlink_ra_matrix.sh`
   spawns N real RetroArch instances on this Mac via CLI (`retroarch -L
   <core> <rom> --host` / `--connect localhost`) to exercise the netpacket
   backend end-to-end. The script auto-detects the installed
   `/Applications/RetroArch.app` (verified present on this Mac; CLI binary at
   `RetroArch.app/Contents/MacOS/RetroArch`) or `retroarch` on PATH, and
   offers `brew install --cask retroarch` when absent. Windowed instances on macOS (RetroArch's macOS video needs a
   window); still script-driven and assert-based via logs/network state.
5. **Manual sign-off:** actual 2-player BattleSphere Gold dogfight and Doom
   deathmatch on this Mac (and Provenance iOS for the TCP backend), per the
   project rule that headless tests can't fully replace in-frontend checks.

### 5. C89 / platform rules

C89-strict (all vars at top of block; `scripts/c89-lint.sh` before push).
Vendored socket files added to the lint exempt list only if upstream code
requires it. `DEVELOPER_DIR=/Library/Developer/CommandLineTools` for host
builds. Conventional commits: `feat(jerry): …`, `feat(net): …`,
`test(net): …`.

## Phases

| Phase | Deliverable | Acceptance |
|-------|-------------|------------|
| 1 | UART emulation + loopback + unit tests + savestate | Loopback tests green in `make test`; no regressions in existing suite (UART idle when disabled) |
| 2 | TCP backend + core options + pair test | Two headless instances exchange bytes; BSG + Doom mutual link detect on localhost |
| 3 | netpacket backend | Link play under real RetroArch netplay on this Mac via `netlink_ra_matrix.sh` |
| 4 | Multi-node hub | AirCars ≥3-instance session detect |

Each phase is a separately reviewable PR to `develop`.

## Risks

- **Latency tolerance.** Link protocols assume a microsecond wire.
  Localhost/LAN is the honest first target; internet play is best-effort
  until BattleSphere's timeout behavior is measured. Netpacket's
  reliable-ordered delivery adds RetroArch's own buffering on top.
- **Baud-formula ambiguity.** The ÷16 vs ÷32 factor changes pacing 2×; must
  be settled from the JTRM PDF (and, if ambiguous, against BattleSphere's
  observed divider writes) before the event timing is coded.
- **Game-side protocol unknowns.** BattleSphere's CatNet handshake may probe
  status bits (SERIN idle level, break conditions) in ways only discoverable
  by tracing; the phase-2 acceptance bar is deliberately "link detect," with
  playable sessions proven in phase 3 sign-off.
- **Frontend support matrix.** Netpacket needs RetroArch ≥ 1.16; Provenance
  gets the TCP path. Both are by design, documented in README.

## Phase 1 outcome (2026-07-31)

Delivered: `src/jerry/uart.c` (registers, 11-bit-time frame pacing,
double-buffered TX/RX, OE latching, TINTEN/RINTEN → JINTCTRL bit 4 → 68K
IPL2), `src/jerry/jlink.c` (transport seam, loopback backend), core option
`virtualjaguar_netlink` (disabled/loopback), STATE_VERSION 6 with a
version-gated UART chunk (event callbacks registered as ids 8/9), and three
test layers (`test_jlink`, `test_uart_loopback`, `test_uart_core`) in
`make test`. Baud formula and frame length verified against JTRM Rev 8
pp. 93–95 (`docs/atari-jaguar-1999/jag_v8.pdf`); JINTCTRL bit 4 assignment
confirmed on p. 87 (the emulator's `IRQ2_ASI=0x10` was already correct).

## Phase 2 outcome (2026-07-31)

Delivered: `src/jerry/jlink_tcp.c` (compact nonblocking POSIX/winsock layer —
**deviation from this spec:** libretro-common `net_socket`/`net_compat` was
NOT vendored; it is a multi-console portability layer far larger than the
~300 lines actually needed, and socketless console targets compile an inert
stub instead), TCP server/client modes with client reconnect retry,
per-frame link polling from `retro_run`, core options
(`tcp_server`/`tcp_client` values + `virtualjaguar_netlink_port`), host
resolution (`VJ_NETLINK_HOST` → `<system_dir>/vj_netlink.txt` → 127.0.0.1),
the two-process `netlink_pair` test in `make test`, and the `netlink_game`
probe tool (per-frame UART sampling + PPM screenshots + `--realtime` pacing).

**Game validation results (two instances on localhost TCP):**

- **BattleSphere Gold** — programs the UART at ~72.3 kbaud (ASICLK=0x16,
  ODD parity) on leaving the title screen. Menu path: title → A →
  Main Menu → down → B ("Network Mode") → B ("Free-For-All"). Loopback
  control: full scan then explicit "Network Failure / unable to locate any
  other players" (correct — it only hears its own echo). Over TCP, **both
  instances passed "Locating Players" and reached the networked Free For
  All Options screen** — mutual link detection achieved.
- **Doom** — menu path: title → A → set "Game Mode: Deathmatch" (right) →
  A to start. Programs ASICLK=0x0D (~118.7 kbaud, JagLink's rate class) and
  handshakes. Over TCP, **both instances entered the deathmatch level**
  (frag-counter HUD, distinct spawn views). Playability/lockstep quality
  over longer sessions is phase-3 validation work.

Both games poll ASISTAT rather than enabling TINTEN/RINTEN, so interrupt
delivery remains covered by unit tests only so far.

## Phase 4 outcome (2026-07-31)

Delivered: CatNet-style multi-peer hub in the TCP server (up to 7 peers;
local TX reaches every peer, a byte from one peer is delivered locally and
forwarded to all others — shared multi-drop bus semantics; unit-tested with
the test process owning two raw peer sockets), plus lifetime TX/RX byte
counters on the transport (`JLinkTxTotal`/`JLinkRxTotal`, dlsym diagnostics).

**AirCars validation (the CatNet title):**

- Programs the UART at boot: ASICLK=0x2A (~38.6 kbaud) with **RINTEN** — the
  only known title using interrupt-driven RX, so its gameplay exercises the
  UART→JINTCTRL→68K IPL2 path end-to-end (BSG/Doom poll ASISTAT instead).
- Menu map (discovered headlessly; cursor state is boot-dependent, so paired
  runs navigate once solo and share a savestate at the seat-select screen):
  title → A → Game Selection {Set Game Difficulty / Enter Your Name /
  Single Player / Two Player Direct Serial / Multiple Player Network} →
  seat select → difficulty → name entry → mission select → play. The
  standalone "Set Game Difficulty" submenu looks identical to the in-wizard
  difficulty step; A/B there just applies and returns (a menu-navigation
  trap that cost several runs).
- **Two Player Direct Serial over TCP: both consoles enter the mission**
  (cockpit HUDs, mission clock), sustained symmetric protocol traffic
  (~2.7 KB each way over the session).
- **Three-console Multiple Player Network over the hub: all three enter the
  mission**; per-node traffic shows exact bus math (each tx≈3.26 KB,
  rx≈6.52 KB = sum of the other two), and a node's radar displays the other
  players. Nodes never see their own bytes echoed (the hub forwards to
  *other* peers only) — AirCars is confirmed happy with that topology.

## Phase 3 outcome (2026-07-31)

Delivered: `src/jerry/jlink_netpacket.c` — the libretro netpacket transport
(env 78). Registered unconditionally in `retro_set_environment` and inert
until the frontend starts a netplay session; `start()` takes over the link
(saving the option-configured mode, restored on `stop()`), TX batches go
out once per frame as `RELIABLE|FLUSH_HINT` broadcasts (multi-console
CatNet sessions work over netplay for free), received payloads feed the
shared RX ring. Protocol version pinned as `vjag-netlink-2` (#585): every
payload carries a 1-byte type (`NP_UART` / `NP_VOICE` / `NP_VOICE_HELLO`).
Older cores advertising `vjag-netlink-1` are refused by the frontend
protocol check — intentional break. Host-side voice rides `NP_VOICE` after
a hello/ack negotiate (falls back to data-only). **No core option required**
for the link itself — stock settings + RetroArch's normal netplay UI just
work; enable *Voice Chat* on both sides for talk.

Validation:
- `test_jlink_netpacket` — a self-contained fake frontend exercising the
  full contract (registration, start/receive/stop, RELIABLE+broadcast
  flags, mode handoff and restore); in `make test`.
- `test_voicemodem_netpacket` — the Voice Modem device over this same
  transport (#494): the test is the netplay frontend *and* both players,
  dlopening two private copies of the core (distinct files, so each image
  gets its own statics) and relaying every packet between them while both
  consoles are driven through the real Ultra Vortek modem choreography.
  The other modem tests (`voicemodem_pair`, `uv_modem_game_test.sh`) all
  run over TCP; this is the only coverage of the transport most users
  actually get. No sockets, no ports, no ROM; in `make test`.
- `test/tools/netlink_ra_matrix.sh` under **real RetroArch 1.22.2** on
  macOS: host + client instances of this core with Doom, netplay session
  established (`SET_NETPACKET_INTERFACE` accepted, "joined as player 2",
  ~11–16 ms ping), windows playable by humans. `KEEP=1` leaves the
  session up for manual play; `RETROARCH_BIN` overrides discovery;
  missing RetroArch suggests `brew install --cask retroarch`.

Frontend matrix: RetroArch ≥ 1.16 → netpacket netplay; any other frontend
(Provenance, etc.) → TCP modes; every frontend → loopback/disabled.

## Wire-latency enhancement (2026-08-20 addendum, #498)

`UARTFrameUsec()` is the single choke point for the whole link's emulated
latency — **both** the TX drain and the RX arrival schedule through it — so
`virtualjaguar_netlink_speed` (default off; originally values 2 / 4,
replaced by a negotiated `auto` in #552 below) divides exactly that one
number. Three things about the shape are load-bearing:

- **Gated on an active transport, evaluated per call.** With
  `JLinkMode() == JLINK_MODE_DISABLED` the function takes a branch that is
  textually develop's original expression, not a divide-by-one, so a stale
  setting can never perturb a non-link session by a rounding step.
  `"auto"` resolves to DISABLED until a netplay session actually exists,
  so the gate is honest about the default. Not cached, so netpacket
  takeover is followed automatically.
- **The accelerated receiver applies back-pressure.** Stock hardware drops
  the new byte when a character completes into a full RBF (an overrun) —
  correct silicon, and preserved exactly when the option is off. But the
  *reader's* polling rate belongs to the game, so dividing the character
  time divides the reader's budget with it. Measured, not theorised: at 4x
  with the plain hardware rule, Ultra Vortek's DSP-poll driver lost the
  second byte of the `$B800` wake reply and the modem handshake never
  completed — **0 pad words against 7044 stock**. With `UARTKickRx()`
  refusing to start a character while RBF is unread, 4x completes normally
  (6948). Without back-pressure, only 2x is shippable; with it, the byte
  stream is provably unchanged and only timing moves.
- **Not in the savestate — AS OF #498; #552 below changes this.** Like
  NTSC/PAL the *config-level request* is a runtime setting and stays
  outside the state blob. But once #552 lets the two sides *negotiate* a
  value at runtime, the negotiated result is no longer purely config-
  derived, and that part now IS serialized. See the #552 section.

Symmetry was a documentation matter under #498, not a guard: a lockstep
exchange is gated by the sum of both sides' clocking, so a mismatched pair
still completes and stays in sync, it simply gets less of the benefit.
Measured on the synthetic ping-pong cart at ASICLK 1999
(`netlink_wire_speed_test.sh`), two independent runs: **16.9 / 22.6 / 52.5**
and **16.4 / 25.0 / 49.9** exchanges per second for off-off, 4x-off, 4x-4x.
One side alone lands between the two symmetric configurations, well short
of half the benefit. Ultra Vortek's full-game pair completes the
choreography in the mismatched configuration too (6972 pad words), and
4x-4x reproduces across runs (6948, 6920) rather than being bimodal —
which matters, because the pre-back-pressure failure was catastrophic (0),
so a single pass would not have shown the margin. #552 removes the player's
ability to create this mismatch in the first place (see below) — this
measurement stays here as the historical justification for why that
mattered enough to build negotiation instead of just better docs.

Because the wall-clock ladder is measured off two paced processes, the
ratio assertions are gated on the probe's own pacing telemetry and SKIP
(loudly, through `scripts/test-skip.sh`) on a runner that could not hold
its frame slots. The liveness assertions — every configuration, including
the mismatched one, still exchanges — are unconditional failures.

## Wire-speedup negotiation (2026-08-21 addendum, #552)

Replaces the player-facing part of #498 above: `virtualjaguar_netlink_speed`
is now `disabled` / `auto` only — no magnitude to pick, and no longer a
coordination burden on two people who are, by definition, not at the same
console. `auto` means the two *emulator instances* agree with each other
at link-up, out-of-band, and fall back to stock timing if the peer
rejects, does not understand the request, or does not answer. There is
exactly one non-stock divisor now (`UART_WIRE_SPEEDUP_MAX`, still 4), so
"negotiate" only ever has to answer one question — is the peer also in
auto — not agree on a magnitude. A derive-from-shared-state approach
(ASICLK, frame period) was considered first and rejected: `asiClk` is 0
after reset and only becomes the game's value once the game writes it, so
at link-up the two sides can legitimately disagree purely from boot skew
— derivation is not symmetric at the moment negotiation needs it.

**Channel: the LAN discovery beacon's UDP socket (`jlink_discover.c`,
port 42170), not the TCP/netpacket data path.** Three candidates were
weighed against one hard requirement — a negotiation message must never
be mistakable for emulated UART bytes, in either direction, including on
a peer that does not implement this:

- **The raw byte pipe (TCP or netpacket) — rejected.** Both are pure byte
  transports today with zero framing; a peer without #552 forwards
  *everything* it receives straight into `jlinkRing`, which the UART reads
  as real console traffic. Any negotiation bytes sent this way corrupt an
  old peer's game state, not just fail to negotiate.
- **The netpacket backend specifically — rejected for the same reason,**
  plus it does not exist at all outside frontend netplay: the core only
  ever sees a `client_id` from `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE`,
  never a peer address, so there is nowhere to address a control message
  even if framing were safe.
- **The discovery beacon socket — chosen.** It is a wholly separate
  UDP port/protocol an old peer either doesn't bind (netpacket/loopback
  sessions never start it) or, if bound (tcp_server/tcp_client, shipped
  since #467/v3.4.0), rejects on sight: `JLinkDiscDecode()` requires an
  exact magic + version + length match, so a differently-magicked packet
  (`"VJNG"` vs the beacon's `"VJAG"`, `JLINK_NEG_PKT_LEN` = 8 vs
  `JLINK_DISC_PKT_LEN` = 40) takes the same silent-drop path a corrupt or
  hostile packet already took before #552 existed. Nothing negotiation-
  related ever reaches `jlinkRing`.

**Protocol** (`src/jerry/jlink.c`, `JLinkNegTick`/`JLinkNegOnRaw`; codec in
`jlink_discover.c`): the TCP client already knows the server's address (it
dialed it) and periodically sends a hello to the effective discovery port
there until it gets one back or exhausts ~8 retries over ~4s. The server is
purely reactive — it never needs the client's address ahead of time, because
`recvfrom()` hands it over for free — and replies with its own packet as an
ack the first time it sees one. Neither side raises
`UARTWireSpeedupEffective` above 1 until it has *received* a decodable
packet from the other, so the accelerated timing is never applied
unilaterally — the asymmetric-benefit measurement above cannot recur by
construction, not just by policy.

Each packet carries a random per-session `senderId`, and a receiver ignores
any incoming packet carrying its *own* id back — not part of the negotiation
semantics, but required because two cores on one machine sharing the
discovery port via `SO_REUSEPORT` (`jlink_discover.c`'s own documented
dev/test topology) can otherwise receive their own outbound hello back and
"confirm" against themselves. The beacon protocol already solved the
analogous problem for itself (name+port self-filtering in `JLinkDiscPoll()`);
this is the same class of fix for negotiation.

**Measured limitation, not just closed as a theoretical risk: same-host
negotiation usually does not confirm at all.** `test/test_jlink_negotiate.c`'s
first cut modeled two cores on one machine exactly as `jlink_discover.c`
documents — both wildcard-bound to the discovery port with `SO_REUSEPORT` —
and measured the hello and the ack both hairpinning back to their own sender
every time, never crossing over. `SO_REUSEPORT` load-balances a unicast
datagram to exactly one member of the sharing group by an internal hash; it
has no concept of "the other peer," so there is no guarantee — and this
measurement suggests no reasonable expectation — that the hash ever picks
the far side over the near side. The `senderId` self-filter above stops a
hairpinned packet from being mistaken for a real confirmation (a safety
fix), but it does not make same-host confirmation happen (not a liveness
fix) — the practical result is that two cores dev-tested against each other
on `127.0.0.1` with `auto` on will most likely sit at stock forever, which
is safe (identical to "peer never answers") but is a real gap worth knowing
about before spending time debugging "why won't auto confirm on localhost".
Play across two different machines is unaffected — there is no shared
`SO_REUSEPORT` group when the sending socket lives on a different host than
the one being dialed. `test/test_jlink_negotiate.c` works around this for
CI by having its fake peer send from an unbound socket that never joins the
group, which is not something two real jlink.c instances on one machine can
do (both bind the fixed discovery port by protocol design).

**Scope, deliberately conservative:** only negotiates over a single TCP
peer (`JLinkTCPPeerCount() == 1`). A CatNet-style multi-drop hub
(`tcp_server` with more than one peer) would need every peer to agree, and
there is no multi-party protocol here, so it stays stock. Frontend netplay
(netpacket) also always stays stock — no address to negotiate with, as
above. Loopback stays stock too (`JLinkDiscActive()` is never true there).
These are real, current limitations, not merely unproven — say so in the
option text rather than implying broader coverage.

**Savestate: THE SHARP EDGE.** `uartWireSpeedupIntent` (the config
request) stays out of the blob, unchanged from #498's reasoning above. But
`uartWireSpeedupEffective` — the negotiated result — is now machine-
affecting the instant a peer confirms it (it reschedules
`UARTRXCallback`/`UARTTXCallback` through `UARTFrameUsec()`), so it is
serialized: `STATE_VERSION_TEAMTAP` (v13, extended in place — see
`src/core/state.h`), trailing chunk, strictly after Team Tap.
`JLinkNegTick()` reconciles this every frame regardless of the loader: a
restored Effective value survives only as long as the session is still
eligible (still connected, still the SAME connection instance, intent
still auto) — so a state saved mid-negotiation and reloaded with the peer
gone reverts to stock within one frame rather than silently running
ahead of a peer that no longer agrees.

**Unproven, stated plainly rather than assumed:** #498's asymmetry
question (does compressing wire time desync the two sides if only one
enables it?) is *mostly* moot under #552 by construction — a side only
applies the speedup after the peer proves it will too — but the brief
window during negotiation, and the moment either side falls back after a
mid-session peer loss, still exist. `test/test_uart_loopback.c` cannot
prove or disprove this: it is one process talking to itself and
structurally cannot model two peers at different divisors (same
limitation the loopback transport has always had for this class of
question).

## Decisions log

- TCP before netpacket (user, 2026-07-31) — frontend-agnostic + testable first.
- Validation corpus: BattleSphere Gold, Jaguar Doom, AirCars (user).
- Plain BSD/winsock sockets everywhere; no per-platform native networking
  frameworks (perf irrelevant at serial baud rates) (user OK'd).
- Automated multi-client testing must support spawning real RetroArch
  instances installed on this Mac, with a brew-installable fallback (user).
