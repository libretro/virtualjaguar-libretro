# Core -> app event channel (private environment ABI)

The standalone app (`app/`) gets structured events from the core -- link up/down, link statistics,
wire speed, crash-watchdog hits -- instead of parsing log text. The channel rides on libretro's
environment callback using ids with `RETRO_ENVIRONMENT_PRIVATE` (0x20000, "frontend-internal
environment callbacks should include this bit"), so no other frontend understands them.

**Under RetroArch (and any frontend that does not know it) the channel is a no-op:** the core asks
once, is told no, latches the channel off, and never sends an event. Cost: one never-taken branch
per would-be event.

Header (shared verbatim by the C89 core and the C++17 app): `src/core/vj_frontend_ext.h`.
Core side: `src/core/vj_frontend_ext.c`. App side: `CoreHost::env` (`app/src/core_host.cpp`).

## Commands

| id | name | data |
| --- | --- | --- |
| `0x5600 \| PRIVATE` | `VJ_ENV_BASE` | (base, not a command) |
| `VJ_ENV_BASE + 0` | `VJ_ENV_HELLO` | `struct vj_env_hello*`: `abi` (in, 1), `core_version` (in), `caps_out` (out) |
| `VJ_ENV_BASE + 1` | `VJ_ENV_EVENT` | `const struct vj_event*`: `type`, `a`, `b`, `c`, `d`, `s` |

Handshake: `retro_init()` calls `VJ_ENV_HELLO` once. The frontend returns true and sets
`caps_out |= VJ_CAP_EVENTS` to opt in; anything else (false, or true with no caps) keeps the channel
off for the session. `retro_deinit()` resets the latch (iOS never dlcloses the core).

## Events

| type | `a` | `b` | `c` | `d` | `s` |
| --- | --- | --- | --- | --- | --- |
| `VJ_EVT_LINK_UP` | 1 voice modem, 0 JagLink | | | | `"<mode>"`, or `"tcp_client <host>"` |
| `VJ_EVT_LINK_DOWN` | | | | | mode name |
| `VJ_EVT_LINK_STATS` | hops per second | mean hop ms x100 | slipped hops | timeouts | |
| `VJ_EVT_WIRE_SPEED` | multiplier (1 = stock) | 1 confirmed, 0 back to stock | | | |
| `VJ_EVT_CRASH_WATCHDOG` | frame number | PC / hash / `B_COUNT` of the log line | signature extra | signature extra | signature: `gpu_pc_escape`, `gpu_runaway`, `dsp_pc_escape`, `gpu_wedge`, `dsp_wedge`, `video_stall`, `cd_seek_wedge`, `inframe_hang` |

LINK_UP/DOWN/WIRE_SPEED are emitted next to the `[NETLINK]` log lines, LINK_STATS next to the
"link stats" line (about every 5 s while traffic flows), CRASH_WATCHDOG next to each
`[CRASH-DETECT]` signature line (already rate-limited by the watchdog). All existing log lines are
unchanged.

## Rules

- **Threading.** Events are emitted only from inside `retro_run()` (the link, stats and watchdog
  code all runs there), synchronously, on the emulation thread. The frontend's handler runs
  inline; nothing is queued.
- **Lifetime.** `s` and `core_version` are valid for the call only; copy what you keep.
- **Return value.** The answer to `VJ_ENV_EVENT` is ignored.
- **Versioning.** `VJ_FRONTEND_EXT_ABI` is 1. New event types and trailing struct fields are
  compatible; anything else bumps the ABI, and a frontend answers HELLO false for an ABI it does
  not know (the app only answers abi == 1).
- **Fallback.** A frontend that never said HELLO is read the old way, from the log text (Netplay in
  the app does exactly this).

## Tests

- `test/test_frontend_ext.c` (core, `make TEST_EXPORTS=1 test`): a refusing frontend gets exactly
  one HELLO and zero events; an answering one gets a LINK_UP in loopback; a caps-less answer is
  silent; refusing again after an answered session is silent (latch reset).
- `app/tests/netplay_test.cpp` (`netplay_unit`, `netplay_pair*`): the link comes up through events
  alone with the log tap cut, and through the log text when the app refuses HELLO.
