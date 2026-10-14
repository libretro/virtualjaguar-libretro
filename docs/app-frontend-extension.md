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
| `VJ_ENV_BASE + 2` | `VJ_ENV_GET_BIOS_PATH` | `struct vj_env_bios_path*`: `role` (in: `VJ_BIOS_CART_BOOT` 1 / `VJ_BIOS_CD` 2), `path` (out) |
| `VJ_ENV_BASE + 3` | `VJ_ENV_GET_BOOT_TARGET` | `unsigned*` (out): `VJ_BOOT_CD_BIOS` 0 / `VJ_BOOT_CART_BOOT_ROM` 1 |
| `VJ_ENV_BASE + 4` | `VJ_ENV_SET_DEBUG_IFACE` | `const struct vj_debug_iface*` (static lifetime): the core's debug interface, sent once per session, only to a frontend that set `VJ_CAP_DEBUG` |

Handshake: `retro_init()` calls `VJ_ENV_HELLO` once. The frontend returns true and sets
`caps_out |= VJ_CAP_EVENTS` to opt in (and `VJ_CAP_BIOS` to be asked the BIOS questions below, `VJ_CAP_DEBUG` (0x4) to be handed the debug interface described last); anything else (false, or true with no caps) keeps the channel
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

| `VJ_EVT_BIOS_STAGED` | `VJ_BIOS_*` role | CRC32 of the image the core will run | source: `VJ_BIOS_SRC_BUILTIN` 0, `_SYSTEM` 1 (the core's own system-folder search), `_FRONTEND` 2 (the file named below) | | file path, or the built-in image's name |
| `VJ_EVT_BIOS_REJECTED` | role | | | | the path the frontend named, which the core did not accept |

## Choosing the BIOS (`VJ_CAP_BIOS`)

A frontend that sets `VJ_CAP_BIOS` is asked, while the core stages a BIOS (inside `retro_load_game()` or a
disk-control insert; the CD BIOS only when the CD BIOS actually runs):

- **`VJ_ENV_GET_BIOS_PATH`** -- "which file for this role?".  Answer true with `path` set (valid for the call only,
  the core copies it) to name a file; answer false, or an empty path, for no preference.  A named CD BIOS (exactly
  256 KiB, a known checksum or a plausible run address in its header) is staged ahead of the core's system-folder
  search; a named cart boot ROM (exactly 128 KiB, any content) is staged ahead of `Cart BIOS Type` and its
  search.  A file the core cannot accept is reported with `VJ_EVT_BIOS_REJECTED`, logged, and the core carries on
  exactly as if it had not been asked (system-folder search, then the built-in image).
- **`VJ_ENV_GET_BOOT_TARGET`** -- asked only for a no-content boot (`retro_load_game(NULL)`).  `VJ_BOOT_CD_BIOS` (the
  default, and the answer to a refusal) comes up in the CD BIOS with the CD unit and Memory Track attached;
  `VJ_BOOT_CART_BOOT_ROM` runs the console boot ROM against an empty cartridge slot (`cd_boot_strategy_none`: the
  logo, then it waits; no CD unit, no Memory Track).

Under RetroArch (HELLO refused) or a frontend without the capability bit, neither question is ever asked and
nothing changes.  The app's side is `CoreHost` (`setBiosPath`, `setBootTarget`, `stagedBios`) and `app/src/bios.h`.

## Debug tools (`VJ_CAP_DEBUG`)

A frontend that sets `VJ_CAP_DEBUG` is handed, once per session right after the HELLO and before any content is
loaded, a pointer to `struct vj_debug_iface` (`VJ_ENV_SET_DEBUG_IFACE`; header `src/debug/vj_debug_iface.h`, shared
verbatim with the app, interface version 1).  Nothing is sent to a frontend without the bit, so under RetroArch the
whole feature does not exist.  What it gives the app (details, threading and the halt model in
[`app-debug-tools.md`](app-debug-tools.md)):

- `region` / `peek` / `poke`: the Jaguar bus as the 68000 sees it.  `peek` is the side-effect-free byte path the GDB stub
  uses.  `poke` writes DRAM, the CLUT, line buffers, GPU RAM and DSP RAM freely, refuses ROM and the CD registers
  (`$DFFF00-$DFFFFF`) always, needs `VJ_DBG_POKE_REGS` for any other register, and `VJ_DBG_POKE_DANGEROUS` as well for the six
  that start or stop something (`B_CMD`, `G_CTRL`, `D_CTRL`, `OLP`, `LTXD`, `RTXD`).  All or nothing.
- `get_regs` / `set_reg` / `disasm_risc`: 68000 registers, both GPU and DSP register banks, one RISC instruction as text.
- `attach` / `detach` / `break_set` / `break_clear` / `halt_next` / `halted_target` / `halt_reason`: execution
  breakpoints and a halt on the existing GDB halt machinery, **without a socket**.  While a processor is halted the core
  calls the host's `halt_pump` repeatedly from inside `retro_run()` (the host runs one UI frame per call and answers
  continue / step / detach).  The GDB TCP stub keeps working beside it; either side may resume.
- `op_snapshot_arm` / `op_snapshot_get`: the whole display list (links followed, both arms of every branch object), read at the
  top of the next field before the OP rewrites `HEIGHT`/`DATA`/`REMAINDER`.
- `blit_log_enable` / `blit_log_head` / `blit_log_get`: a ring of the last 256 blitter launches.
- `cry_to_rgb`, `texreplace_reload`, `frame_number`.

Everything is host-transient: no savestate field, nothing the emulated machine can observe (the determinism gate in
`test/test_debug_iface.c` hashes the frame and all of main RAM with everything switched on).  Detached, the cost is one
global test per `OPProcessList` call and one per blitter launch;
the A/B benchmark in the PR measures it.  Strings and the struct stay valid until
`retro_deinit()`; the core forgets an attached host on unload.

## Rules

- **Threading.** Events are emitted from inside `retro_run()` (the link, stats and watchdog
  code all runs there), except `VJ_EVT_BIOS_*`, which come from inside `retro_load_game()` (or a
  disk-control insert) while the BIOS is staged; always synchronously, on the emulation thread. The
  frontend's handler runs inline; nothing is queued.
- **Lifetime.** `s` and `core_version` are valid for the call only; copy what you keep.
- **Return value.** The answer to `VJ_ENV_EVENT` is ignored.
- **Versioning.** `VJ_FRONTEND_EXT_ABI` is 1. New event types, command ids, capability bits and trailing
  struct fields are compatible (the BIOS commands and `VJ_CAP_BIOS` were added this way); anything else bumps the ABI, and a frontend answers HELLO false for an ABI it does
  not know (the app only answers abi == 1).
- **Fallback.** A frontend that never said HELLO is read the old way, from the log text (Netplay in
  the app does exactly this).

## Tests

- `test/test_debug_iface.c` (core): only a frontend that sets `VJ_CAP_DEBUG` is handed the interface (once); `peek` equals
  `jaguarMainRAM` and the cartridge image; the poke policy; halt / step / continue / detach through a scripted host;
  a breakpoint re-hit; OP snapshot; blit log; frame and RAM hash identical with everything on.
- `app/tests/debug_session_test.cpp` (`debug_session_unit`) against `app/tests/debug_mock.h`, and `app/tests/debug_e2e.cpp`
  (`debug_e2e`): the real core through `CoreHost` + `DebugSession`.

- `test/test_frontend_ext.c` part 3 (core): a refusing frontend and one without `VJ_CAP_BIOS` are never asked;
  a named CD BIOS / boot ROM file is staged (source, checksum, path in `VJ_EVT_BIOS_STAGED`); a wrong-sized or
  missing file is `VJ_EVT_BIOS_REJECTED` and the built-in image runs; the cart boot-ROM no-content target stages
  the boot ROM and no CD BIOS.
- `app/tests/bios_e2e.cpp` (`bios_e2e`): the same through `CoreHost`.

- `test/test_frontend_ext.c` (core, `make TEST_EXPORTS=1 test`): a refusing frontend gets exactly
  one HELLO and zero events; an answering one gets a LINK_UP in loopback; a caps-less answer is
  silent; refusing again after an answered session is silent (latch reset).
- `app/tests/netplay_test.cpp` (`netplay_unit`, `netplay_pair*`): the link comes up through events
  alone with the log tap cut, and through the log text when the app refuses HELLO.
