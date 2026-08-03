# Jaguar Link Networking (JERRY UART + Pluggable Transport)

**Date:** 2026-07-31
**Status:** COMPLETE — all four phases implemented and play-tested (Doom
deathmatch confirmed full speed under RetroArch netplay after the sub-frame
latency fix, PR #244). Known limitations: internet play is best-effort
(localhost/LAN validated); BattleSphere Gold validated to the networked
lobby, sustained dogfight play unverified; AirCars under *netpacket* netplay
untested (its interrupt-driven RX does not hit the ASISTAT pump path — TCP
mode fully validated); iOS/Provenance TCP mode not yet device-tested.
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

- `virtualjaguar_netlink`: `disabled` (default; UART still emulated —
  registers behave, SERIN reads idle, nothing connects) | `loopback` |
  `tcp_server` | `tcp_client` | `netpacket` (phase 3).
- `virtualjaguar_netlink_port`: enum of a few ports (default 42171).
- Host for `tcp_client`: `virtualjaguar_netlink_host` option. Stock
  RetroArch options cannot take free text, so the option ships preset
  values (`127.0.0.1`, and a `vj_netlink.txt` sentinel meaning "read
  `<system_dir>/vj_netlink.txt`"), but the core accepts ANY string the
  frontend returns — frontends with free-text option UIs (Provenance)
  supply arbitrary addresses directly. Resolution order: env
  `VJ_NETLINK_HOST`, then the option (sentinel defers to the file), then
  `<system_dir>/vj_netlink.txt`, then `127.0.0.1`.

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
shared RX ring. Protocol version pinned as `vjag-netlink-1` so frontends
refuse cross-incompatible core pairings. **No core option required** —
stock settings + RetroArch's normal netplay UI just work.

Validation:
- `test_jlink_netpacket` — a self-contained fake frontend exercising the
  full contract (registration, start/receive/stop, RELIABLE+broadcast
  flags, mode handoff and restore); in `make test`.
- `test/tools/netlink_ra_matrix.sh` under **real RetroArch 1.22.2** on
  macOS: host + client instances of this core with Doom, netplay session
  established (`SET_NETPACKET_INTERFACE` accepted, "joined as player 2",
  ~11–16 ms ping), windows playable by humans. `KEEP=1` leaves the
  session up for manual play; `RETROARCH_BIN` overrides discovery;
  missing RetroArch suggests `brew install --cask retroarch`.

Frontend matrix: RetroArch ≥ 1.16 → netpacket netplay; any other frontend
(Provenance, etc.) → TCP modes; every frontend → loopback/disabled.

## Decisions log

- TCP before netpacket (user, 2026-07-31) — frontend-agnostic + testable first.
- Validation corpus: BattleSphere Gold, Jaguar Doom, AirCars (user).
- Plain BSD/winsock sockets everywhere; no per-platform native networking
  frameworks (perf irrelevant at serial baud rates) (user OK'd).
- Automated multi-client testing must support spawning real RetroArch
  instances installed on this Mac, with a brew-installable fallback (user).
