# Design: Voice chat over RetroArch netplay (#585)

**Date:** 2026-08-25
**Status:** IMPLEMENTED (#585)
**Issue:** [#585](https://github.com/libretro/virtualjaguar-libretro/issues/585)
**Parent:** [#485](https://github.com/libretro/virtualjaguar-libretro/issues/485) / PR #584 (UDP discovery path, TCP only)
**Intent:** next PR after #584 — make host-side voice work inside a RetroArch
netplay session via the netpacket interface (env 78).

## Product decision (from Joe, 2026-08-25)

- **Wanted:** voice chat over RetroArch netplay, with auto-negotiation so both
  sides agree the peer understands framed voice packets.
- **Fallback:** if negotiation never confirms, degrade gracefully (data-only
  netplay, same as today when voice cannot TX) — still “just work” for the
  game link.
- **Compat:** **OK to break older clients.** Last release is minor; bumping
  `protocol_version` / framing the netpacket pipe is acceptable. Do not spend
  effort preserving `vjag-netlink-1` unframed payloads for voice multiplexing.

## Availability (after #585)

| Path | Voice |
|------|-------|
| TCP host / TCP client | Works — `VJVC` on discovery UDP :42170 |
| RetroArch netplay (`JLINK_MODE_NETPACKET`) | Works — framed `vjag-netlink-2` + hello negotiate |
| Loopback | Local mic monitor only |

Voice is still **host-side only** (zero emulated state). That invariant must
hold. See `docs/voice-chat-design.md`.

Current netpacket pipe ([`src/jerry/jlink_netpacket.c`](../src/jerry/jlink_netpacket.c)):

- Batches UART bytes into ≤512 B packets
- Sends `RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT`
- **No framing** — payload is raw UART bytes end-to-end
- Protocol string in [`libretro.c`](../libretro.c): `"vjag-netlink-1"`

That is why voice was kept off netplay: stuffing µ-law into the same
unframed pipe would corrupt an old peer’s UART ring.

## Proposed approach

### 1. Version the netpacket protocol (intentional break)

Bump to e.g. `"vjag-netlink-2"`.

Wire every netpacket payload with a 1-byte type prefix (or a short header):

| Type | Name | Flags | Contents |
|------|------|-------|----------|
| `0x01` | `NP_UART` | RELIABLE (+ flush as today) | raw UART bytes (current payload) |
| `0x02` | `NP_VOICE` | **UNRELIABLE \| UNSEQUENCED** | same body as UDP `VJVC` (172 B) or compact: seq + µ-law without repeating magic |
| `0x03` | `NP_VOICE_HELLO` | RELIABLE | negotiation hello/ack (see below) |

Old cores advertising `vjag-netlink-1` will fail the frontend protocol
check and refuse to connect — acceptable per product decision. Document
in release notes / `docs/netlink-user-guide.md`.

### 2. Auto-negotiation (voice capability)

Mirror the spirit of #552 wire-speed negotiation, but over netpacket (no
UDP address needed):

- On `JLinkNPStart`, if `virtualjaguar_voice_chat` is enabled, send
  `NP_VOICE_HELLO` (include a random `senderId` / capability flags).
- Peer replies with `NP_VOICE_HELLO` ack.
- Only after bidirectional hello: arm TX (`VoiceChatSendFrame` →
  `npSend` with voice type) and RX (`JLinkNPReceive` demux →
  `VoiceChatOnRaw` / jitter).
- If no ack within N frames / timeout: stay data-only, log once
  (`[VOICE] peer has no voice chat — data-only`), leave mic closed unless
  local monitor is on.
- Re-negotiate on reconnect / `JLinkNPStop` → `Start`.

“Fallback to just work” = game link always works; voice is best-effort
overlay after hello confirms.

### 3. Send / receive plumbing

- Extend [`jlink_netpacket.c`](../src/jerry/jlink_netpacket.c):
  - Prefix UART flushes with `NP_UART`
  - Add `JLinkNPSendVoice(const uint8_t *pkt, size_t len)` using
    `RETRO_NETPACKET_UNRELIABLE | RETRO_NETPACKET_UNSEQUENCED` (voice is
    best-effort; must not block lockstep UART)
  - Demux in `JLinkNPReceive` / deliver path by first byte
- Extend [`voicechat.c`](../src/jerry/voicechat.c):
  - When `JLinkMode() == JLINK_MODE_NETPACKET` && voice negotiated,
    send via netpacket instead of `JLinkDiscSendTo`
  - Keep UDP path for TCP modes unchanged
- Soften mic gating in [`libretro.c`](../libretro.c)
  `voicechat_ensure_mic`: allow open when netpacket + voice negotiated
  (today requires `JLinkDiscActive()` + TCP)

### 4. Keepalives

On netplay, peer identity is `client_id` from the frontend — no UDP
presence keepalive needed for address learning. Keep UDP keepalives for
TCP only. Optionally a rare `NP_VOICE_HELLO` refresh is enough.

### 5. Tests

- Unit: encode/decode of framed netpacket (UART vs VOICE vs HELLO),
  demux rejects unknown type, hello state machine
- Extend / add `test/test_voicemodem_netpacket.c`-style fake frontend:
  two cores, relay packets, assert voice energy on far side after hello
- Inertness still required (voice on/off identical fb + savestate digests)
- Explicit test: hello timeout → data-only, no mic open

### 6. Docs / options

- Update `docs/voice-chat-design.md` availability table
- Update `docs/netlink-user-guide.md` — netplay now carries voice when
  both sides enable the option and speak `vjag-netlink-2`
- Core option copy: remove “unavailable under RetroArch netplay”
- Release note: protocol bump breaks 3.4.x netplay peers (they must
  update); TCP voice unchanged

## Non-goals

- Relaying voice over the emulated UART / `jlinkRing` (still forbidden)
- DTMF tone synthesis
- Preserving unframed `vjag-netlink-1` coexistence for mixed-version
  sessions (frontend will refuse the session)

## Implementation sketch (file touch list)

| File | Change |
|------|--------|
| `src/jerry/jlink_netpacket.c` / `.h` | Framing, voice send, demux, hello helpers |
| `src/jerry/voicechat.c` / `.h` | Netpacket TX backend; negotiate state |
| `libretro.c` | `protocol_version` → `vjag-netlink-2`; mic gate; optional hello tick |
| `libretro_core_options.h` | Option help text |
| `docs/voice-chat-design.md` | Point at this doc; update table |
| `docs/netlink-user-guide.md` | Protocol bump + voice-over-netplay |
| `test/…` | Framing + pair-over-fake-netpacket + inertness |

## Handoff checklist for a fresh session

1. Branch off `libretro/develop` (contains #584).
2. Read this file + `docs/voice-chat-design.md` + `src/jerry/jlink_netpacket.c`.
3. Open / link a GitHub issue (follow-up to #485); PR → `develop`; link in
   Development panel by hand.
4. Prefer surgical framing in netpacket first, then wire voicechat TX/RX,
   then hello state machine, then tests.
5. C89 strict; `make TEST_EXPORTS=1 test`; both audio tests if mixer
   changes (should not); RetroArch: two instances, netplay, voice option
   on, confirm talk + Ultra Vortek data still connects.
