# Network link setup: usable configuration for JagLink, CatBox and the Voice Modem

**Date:** 2026-08-18
**Status:** implemented (see `libretro.c`, `src/jerry/jlink.c`, `src/jerry/jlink_discover.c`)
**Branch:** `feat/netlink-ux`, stacked on `release/v3.4.0`
**Issues:** follows #481 (voice modem), #494 (netpacket untested)

## Problem

The core supports three Jaguar networking products over two transports, and
the configuration UI does not explain any of it. Three concrete failures, all
observed:

1. **The netplay path is invisible.** The core already registers the netpacket
   interface unconditionally (`libretro.c`, `retro_set_environment`) and
   `JLinkNPStart()` takes the link over for the duration of a frontend netplay
   session, restoring the previous mode on stop. This means "link over
   RetroArch netplay, zero IP configuration" **already works**. Nothing in the
   UI says so; it is mentioned only inside a 60-word blurb attached to the
   *TCP client host* option, which a user only reads once they are already
   committed to manual IP entry.

2. **The host address cannot be typed.** libretro core options are strictly
   enumerated. RetroArch stores a value *index* into a `string_list`
   (`struct core_option.vals` / `core_option_manager_set_val(opt, idx,
   val_idx, ...)`) and there is no keyboard path in any core-option menu
   callback. The libretro header agrees: `default_value` "must equal one of
   the value members in the `values` array, or else this option will be
   ignored." Free-text host entry is therefore impossible for **any** core, on
   any platform. Today's workarounds — a `.local` preset, `vj_netlink.txt`, or
   localhost — are all poor, and on iOS they are the only choices.

3. **`vj_netlink.txt` is undocumented and over-explained.** The option
   description is long, yet never states the file format.

Underneath all three: until this release the entire link stack logged nothing,
so a link that never came up was indistinguishable from one that was never
configured. `[NETLINK]` logging landed in d3e5caa / 2e03bac and is the
foundation this design builds on.

## Constraints discovered

These are verified facts, not assumptions. They drove the design.

- **Free-text options are impossible.** See above. Discovery replacing text
  entry is not a nicety; it is the only route to "pick a host" on a touch
  device.
- **Dynamic option values are supported in RetroArch.** A second
  `SET_CORE_OPTIONS_V2` tears down and rebuilds the option manager
  (`runloop.c`), flushing current values to disk first. Feasible, but a full
  teardown — so re-send only on real change, never on a timer.
- **Bonjour from a core is blocked on iOS/tvOS.** RetroArch's `Info.plist`
  declares exactly `_ra-bless._tcp` and `_ra_netplay._tcp` under
  `NSBonjourServices`; iOS permits only enumerated service types. A core
  advertising `_vjaglink._tcp` is silently blocked. Unblocking needs an
  upstream RetroArch plist change.
- **UDP broadcast is permitted on iOS.** RetroArch's iOS entitlements carry
  `com.apple.developer.networking.multicast` (which covers broadcast) and both
  iOS and tvOS declare `NSLocalNetworkUsageDescription`. A UDP beacon from
  inside the core works on the platform where Bonjour does not.
  *Caveat:* that entitlement is in official builds; a self-signed RetroArch
  lacking it will not broadcast.

Consequence: **UDP is the primary discovery mechanism, not the fallback.**
mDNS is deferred to a follow-up ticket, blocked upstream on iOS/tvOS.

### Testing trap: Local Network permission is per binary

On macOS (and iOS), Local Network access is approved **per executable**, not
per user or per machine. A freshly built test binary is a new subject and
raises its own prompt; an unanswered prompt fails closed, silently.

This cost a full misdiagnosis during implementation. A run of
`netlink_discover_pair.sh` failed, and three independent from-scratch
reproductions "confirmed" that local UDP broadcast did not loop back on the
host at all — so the failure was attributed to the machine's network stack.
It was not. Every reproduction ran under an interpreter that already held
the permission, while the newly compiled probe did not, and its prompt had
timed out. A direct three-destination measurement on the same machine showed
`255.255.255.255` and `127.0.0.1` both delivering normally, and the real pair
test passed on the next run once the prompt was settled.

When a discovery test fails on macOS, check the permission before suspecting
the code:

- answer the Local Network prompt (it may appear behind the terminal window)
- System Settings → Privacy & Security → Local Network, confirm the terminal
  or test binary is listed and enabled
- a rebuild can re-prompt, so a test that passed yesterday can fail today for
  this reason alone

A timed-out prompt and a broken network stack are indistinguishable from
inside the process: `sendto` succeeds either way and nothing arrives.

## Design

### 1. Option model

Every existing key is retained and values are **added**, never renamed, so no
existing `.opt` file breaks and no migration code is needed.

| Key | Change |
|---|---|
| `virtualjaguar_netlink` | label → **"Network Link"**. New `auto` value, becomes the default. `disabled` / `loopback` / `tcp_server` / `tcp_client` all still parse. |
| `virtualjaguar_uart_device` | unchanged. **"What is plugged in"**: JagLink/CatBox, or Voice Modem. CatBox and JagLink are the same wire protocol and stay one entry. |
| `virtualjaguar_netlink_host` | becomes the **discovered-host picker**. Values rebuilt at runtime: `127.0.0.1`, one row per live beacon, then `vj_netlink.txt` and the `.local` preset as fallbacks. |
| `virtualjaguar_netlink_port` | unchanged. (This design originally proposed moving it to an "Advanced" category; that category was never implemented and there is no plan to add it -- the option stays in its existing top-level position.) |
| `virtualjaguar_netlink_wait` | unchanged. (Same "Advanced" category that was never implemented -- see above.) |

`auto` resolution order, evaluated at load and whenever netplay starts/stops:

1. frontend netplay session live → netpacket (already automatic today)
2. else idle

> **Amended during pre-flight review.** This originally read "else an
> explicit direct mode previously chosen → use it". Dropped: with a single
> option key there is nowhere to read a previous choice from — selecting
> `auto` overwrites it — so honouring it would require hidden persisted
> state whose behaviour the user can neither see nor predict across
> restarts. Users wanting a direct link select `tcp_host`/`tcp_client`
> explicitly, and the OSD says so when the link is idle.

**`auto` never auto-connects to a discovered peer.** Discovery populates the
host list; choosing a host stays a deliberate user action. Silently dialling
whoever answered a broadcast is a surprising thing for an emulator to do, and
with the Voice Modem it would place a "call" the user did not initiate. In
`auto` with no netplay and no prior direct mode, the link stays idle and the
OSD says so.

Fresh installs therefore default to "works over netplay, no configuration".
Existing installs keep whatever value their `.opt` already holds.

Row visibility is driven by the `SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK` the
core already registers:

| Mode | Rows shown |
|---|---|
| `auto`, `disabled`, `loopback` | none beyond device |
| `tcp_server` | port |
| `tcp_client` | host, port |

### 2. Discovery protocol

New file `src/jerry/jlink_discover.c` / `.h`. No dependencies; plain BSD
sockets, guarded the same way `jlink_tcp.c` guards its platform support.

**Beacon.** A host in `tcp_server` broadcasts once per second to
`255.255.255.255:42170`. (As shipped, `auto` never resolves to a direct
host -- see the "amended during pre-flight review" note above -- so this is
`tcp_server` only, not `tcp_server` or `auto`.)

```
offset  size  field
0       4     magic 'V','J','A','G'
4       1     protocol version (1)
5       1     device: 0 = jaglink/catbox, 1 = voice modem
6       2     TCP port, big-endian
8       32    display name, NUL-padded UTF-8
```

40 bytes fixed. Port **42170** is deliberately outside the
`virtualjaguar_netlink_port` option range (42171–42174) so discovery can never
collide with a link port.

The display name is the system hostname (`gethostname`, `GetComputerNameA` on
Windows), truncated to 31 chars + NUL. No new option: a name the user must
type is the problem this feature exists to remove.

**Listener.** A core in `tcp_client` (listen-only) or `tcp_server` (beacon +
listen) binds 42170 and keeps a peer table: source IP, name, device, port,
last-seen timestamp. Entries expire after 10 s. **`auto` never binds the
socket at all** -- discovery is deliberately not started for `auto` (see the
`JLinkDiscStart()` call site in `libretro.c`'s `netlink_apply()`), because
`auto` is the option's default: opening a UDP listener on port 42170 for
every user on every load would trip the OS's Local Network permission
prompt for players who never touched the networking options. Do not "fix"
this back to matching an earlier draft of this doc that said otherwise.

The listener runs from `JLinkFrameTick()` (`src/jerry/jlink.c`), not from
`JLinkPoll()`. `JLinkPoll()` is also reached via `JLinkPump()`, which games
call thousands of times per frame while spinning on a reply, so a
`recvfrom()` drain loop there would be wasteful and, unlike TCP polling, has
no mode gate to keep it rare. `JLinkFrameTick()` runs once per video frame
regardless of `jlinkMode`, which matches the 1 Hz beacon / 10 s expiry
cadence this module works on (see `JLinkFrameTick()`'s declaration comment
in `jlink.c` for the full rationale).

The listener socket **must** set `SO_REUSEADDR` (and `SO_REUSEPORT` where it
exists) before binding. Two cores on one machine is the normal development and
test configuration — it is exactly what `netlink_pair_test.sh` and
`uv_modem_game_test.sh` do — and without it the second instance fails to bind
and discovery silently does nothing on the very setup used to test it. A host
also ignores its own beacons, matched on the magic+name+port tuple rather than
source IP, since loopback and LAN addresses differ for the same machine.

**Option refresh.** When the peer set changes (an add, or an expiry — not on
every beacon), the core rebuilds `virtualjaguar_netlink_host`'s values and
re-sends `SET_CORE_OPTIONS_V2`. Cap at 8 discovered peers. Rate-limit rebuilds
to at most one per 2 s so a flapping peer cannot thrash the menu.

**Device mismatch.** The beacon carries device type, so a Voice Modem host
seen by a JagLink client (or the reverse) is flagged in the host label and in
an OSD warning. These two will never interoperate and today fail silently.

### 3. OSD narration

`SET_MESSAGE_EXT`, on state transitions only, mirroring the `[NETLINK]` log
lines so screen and log agree:

| Trigger | Message |
|---|---|
| link resolved at load | `Network Link: netplay session (Voice Modem)` / `…: direct client -> 192.168.1.42` |
| link up | `Jaguar link connected (Voice Modem)` |
| link down | `Jaguar link lost` |
| peer set changed | `Found 2 Jaguar hosts on the LAN` |
| device mismatch | `Host is running JagLink, you are set to Voice Modem` |
| device set, link idle | `Voice Modem selected but link is idle -- start netplay or pick a host` |

The last one is the exact failure that motivated this work.

### 4. Documentation

- `virtualjaguar_netlink_host` description shortened, and the
  `vj_netlink.txt` format finally stated: **one line, the host address only,
  no port, trailing newline optional.** (Port comes from
  `virtualjaguar_netlink_port`.) The existing parser already behaves this way
  — it `fgets` the first line and strips trailing CR/LF/space — so this is
  documenting reality, not changing it.
- `docs/netlink-design.md` gains a transport-selection section.
- `docs/voice-modem.md` troubleshooting section updated for `auto`.

### 5. What is deliberately not in scope

- mDNS/Bonjour (blocked upstream on iOS/tvOS; separate ticket)
- WAN/internet discovery — netplay's own lobby already solves that
- changing the netpacket takeover semantics, which work correctly today
- any change to the modem protocol or the JagLink wire format

## Testing

Discovery state is host-side, like jlink's sockets, and is **not** serialized
into savestates — consistent with the existing rule for modem session state.

| Test | Gate |
|---|---|
| `test/test_jlink_discover` (new, no core) | beacon encode/decode round-trip; truncated and wrong-magic packets rejected; peer expiry at 10 s; table caps at 8 |
| `test/tools/netlink_discover_pair.sh` (new) | two cores on loopback: host beacons, client's peer table populates. As shipped this asserts population only -- it kills the beacon process but never re-checks the listener afterward, so it does NOT cover expiry after the host exits. (Expiry-at-10s logic is covered separately, beacon-free, by `test/test_jlink_discover` above.) |
| existing `netlink_pair_test.sh`, `voicemodem_pair_test.sh`, `uv_modem_game_test.sh` | must stay green — `auto` must not change explicit-mode behaviour |
| `test/tools/test_option_visibility` | extended: per-mode row visibility matrix |
| manual, two RetroArch instances | OSD text correct on iOS and macOS |

C89 strict throughout (`scripts/c89-lint.sh`), per CLAUDE.md.

## Risks

| Risk | Mitigation |
|---|---|
| `SET_CORE_OPTIONS_V2` re-send disrupts a user sitting in the options menu | rebuild only on real peer-set change, rate-limited to 1 per 2 s |
| Non-RetroArch frontends handle re-send poorly | discovery degrades to "no extra host rows"; every existing manual path still works |
| Self-signed RetroArch lacks the multicast entitlement | documented; manual host selection remains |
| UDP broadcast blocked by AP client isolation | documented; `vj_netlink.txt` and `.local` remain |
| `auto` changes default behaviour for fresh installs | existing `.opt` files keep their value; only new installs see `auto` |

## Landing

This is a feature, and CLAUDE.md says release branches carry bug fixes only.
Three options, for the maintainer to choose:

1. **Stack on the release branch** (chosen): `feat/netlink-ux` is cut from
   `release/v3.4.0`; PR it into the release branch, so v3.4.0 ships with it.
   Delays v3.4.0 by the size of this work.
2. Ship v3.4.0 as-is; retarget this at `develop` as the v3.5.0 headline.
3. Split: the documentation and OSD parts are arguably fixes for the
   diagnosability defect and could ride v3.4.0; discovery and `auto` wait.
