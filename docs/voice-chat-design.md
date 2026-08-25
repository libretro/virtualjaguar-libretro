# Design: Voice chat over the netlink transport (#485)

**Date:** 2026-08-25
**Status:** IMPLEMENTED
**Issue:** [#485](https://github.com/libretro/virtualjaguar-libretro/issues/485)
**Follow-up to:** [#481](https://github.com/libretro/virtualjaguar-libretro/issues/481) / PR #483 (Voice Modem *data* path)

## Framing: this is not emulation

The Jaguar Voice Modem's headline feature was simultaneous voice + data.
Voice was compressed by the modem and carried modem-to-modem over the
phone line; the Jaguar only issued audio-path control words (`$A040` /
`$A0A0`, see `docs/voice-modem.md`). Voice samples never entered the
console.

So this feature is a **host-side voice channel** added to our netlink
transport. It must contribute **zero** emulated state: no savestate
fields, no effect on the framebuffer or any register. That is the same
"the machine cannot observe it" property that makes texture dump and
blit-memo host-side work safe, and it is asserted by
`test/tools/test_voicechat_inertness.c` the same way (savestate digests
+ framebuffer hashes identical with voice on and off).

**Explicitly not in scope:** synthesising DTMF tone audio. DTMF is already
handled functionally (`$8A2n` / `$6800`); the tones themselves never
reached the TV on real hardware. Inventing them would invent observable
behaviour. See issue #485.

## Why a side channel (not the UART byte pipe)

Two hard reasons, both in the code today:

1. `JLinkStateSave` serialises the 256-byte RX ring into savestates
   (`src/jerry/jlink.c`). Voice bytes there would become emulated state.
2. The wire is paced at emulated 19200 baud. An ~8 KB/s audio stream
   would starve game packets.

Voice therefore never touches `jlinkRing`, the UART, or any JERRY
register.

## Transport: ride the discovery socket

`jlink_discover.c` already exposes an extension seam for "#552 or any
future second protocol":

- `JLinkDiscSetRawHandler` — every non-beacon datagram
- `JLinkDiscSendTo` — unicast on the same bound socket

Reusing UDP port **42170** means:

- no second Local Network permission prompt on iOS/macOS
- no new firewall port
- automatic backward compatibility — a peer without this build fails
  the beacon magic check and silently drops the datagram (same path as
  a corrupt packet)

Peer addressing copies the #552 choreography:

- TCP **client** already knows the server address (it dialed it) and
  sends first (voice frames + a periodic keepalive so the server learns
  the client even when the mic gate is closed)
- TCP **server** learns the client address from the inbound datagram
  source and can reply

Availability follows `JLinkDiscActive()` (TCP host / TCP client only):

| Mode | Voice |
|------|-------|
| TCP host / TCP client | Works |
| Netpacket (RetroArch netplay) | Unavailable — no peer address exposed to the core; log once, data-only |
| Loopback | No peer; local mic monitor still works for a mic check |

Netpacket was rejected for voice: its payloads have no framing today, so
multiplexing would break wire compat with 3.4.x peers, and the core
never sees a peer IP to dial a side channel.

## Wire format

Magic **`VJVC`** (distinct from `VJAG` beacons and `VJNG` negotiation).

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Magic `VJVC` |
| 4 | 1 | Version (`1`) |
| 5 | 1 | Flags (`0x01` = keepalive / silence presence) |
| 6 | 2 | Sequence (big-endian) |
| 8 | 4 | `senderId` (random per-process; SO_REUSEPORT self-filter) |
| 12 | 160 | G.711 µ-law payload (20 ms at 8 kHz) |

Total **172 bytes**, far under any practical MTU. `JLinkDiscPoll`'s
`recvfrom` buffer is enlarged past the 40-byte beacon size so these
datagrams are not truncated before the raw handler sees them.

## Codec and bandwidth

G.711 µ-law at 8 kHz mono ≈ **8 KB/s** — period-appropriate and matching
issue #485's stated budget. Encode/decode is a table, not a compressor;
compression is deferred until bandwidth proves a problem.

Receive path: jitter buffer → 6× upsample to 48 kHz (exact ratio) with
zero-order hold (each 8 kHz sample repeated across six stereo pairs) →
saturating add into both channels of `sampleBuffer` immediately before
`SoundCallback`. `dac.c` is untouched; `sampleBuffer` is not in any
serialize path. Linear interpolation is deliberately not used here —
period-correct phone-line fidelity does not require it, and the hold
matches what a simple µ-law path historically did.

## Gate modes

Core option `virtualjaguar_voice_chat_gate`:

- **`open_mic`** — full-duplex with a noise-gate / VAD threshold
  (`virtualjaguar_voice_chat_vad`); works on every frontend including
  mobile.
- **`push_to_talk`** — keyboard key via `RETRO_DEVICE_KEYBOARD`
  (`virtualjaguar_voice_chat_ptt_key`). No RetroPad button is free (all
  16 map to Jaguar buttons), so PTT is desktop-oriented.

Default for the master switch `virtualjaguar_voice_chat` is **disabled** —
mic capture must never start unasked.

## Graceful degradation

- Frontend without `RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE`, or
  `open_mic` returning NULL: log once, leave the **receive** path armed
  so a player with no mic can still hear others; data netplay unchanged.
- Permission denied / `read_mic` returning -1: treat as silence that
  frame.
- Peer without voice chat: datagrams dropped at magic check; data path
  unaffected.

## Inertness argument

Voice state lives only in host-side buffers (jitter queue, peer address,
seq counters, mic handle). None of it is written by `JLinkStateSave`,
UART state, or any JERRY/TOM register path. Mixing into `sampleBuffer`
happens after `JaguarExecuteNew()` and before `audio_batch_cb`, so the
emulated machine cannot observe it. The inertness test runs `yarc.j64`
with voice off then on (synthetic mic + monitor) and requires identical
per-frame framebuffer hashes and savestate digests at N/2 and N.

## Files

| Path | Role |
|------|------|
| `src/jerry/voicechat.c` / `.h` | Codec, framing, jitter, VAD/PTT, mix, peer keepalive |
| `src/jerry/jlink_discover.c` | Larger `recvfrom` buffer |
| `src/jerry/jlink.c` | Magic-keyed raw dispatcher (`VJNG` / `VJVC`) |
| `libretro.c` / `libretro_core_options.h` | Mic iface, options, tick + mix |
| `test/test_voicechat.c` | Pure-logic unit tests |
| `test/tools/test_voicechat_inertness.c` | Savestate + fb inertness gate |
| `test/tools/voicechat_pair_test.sh` | Two-core TCP energy check |
| `test/harness/harness.c` | Synthetic mic behind `--mic-tone` |
