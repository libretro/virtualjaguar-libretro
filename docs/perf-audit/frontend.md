> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# Frontend/Presentation-Path Performance Audit
Scope: libretro.c glue, shadowfb, texreplace/texdump, DAC hand-off, titledb, input polling,
crash_detect, perf_iface, vjtrace, netlink polling — everything in `retro_run()` outside the
GPU/DSP/blitter/bus emulation core itself.

Repo: virtualjaguar-libretro, worktree alien-predator-save-state-ad621b. Read-only audit, no
edits made.

## Headline conclusion

This layer is already well-optimized. I could not find a full-frame copy, an unconditional
per-pixel conversion, redundant geometry/variable-update calls, or a chunked audio hand-off.
The video buffer TOM renders into IS the buffer handed to `video_cb` — zero extra copies. Audio
is one `audio_batch_cb` call per frame with a fixed sample count. `check_variables()` and
`SET_GEOMETRY` are both correctly edge-gated. Diagnostic/optional features (crash watchdog,
texture dump, blit-memo, cheats, netlink) all short-circuit cheaply when off or idle.

Net implication for the weak-ARM-host investigation: the frontend glue is very unlikely to be
where RPi4/A10X frame time is going. The other agents' territory (GPU/DSP interpreters, blitter,
bus) is the much more plausible place — consistent with existing perf_iface capture data already
in project memory (GPU RISC ~74%, blitter ~17% of frame time on captured devices).

---

## 1. Video path: NO full-frame copy found (POSITIVE finding, not an action item)

- `libretro.c:4678` — `JaguarSetScreenBuffer(videoBuffer)` is called once at init. TOM's scanline
  renderer (`src/tom/tom.c`, out of this audit's scope) writes pixels directly into
  `videoBuffer`.
- `libretro.c:5550` — `video_cb(videoBuffer, game_width, game_height, game_width << 2)` hands
  that SAME pointer straight to the frontend. No `memcpy`, no per-pixel conversion loop, no
  intermediate staging buffer anywhere between TOM's render and `video_cb` in libretro.c.
- `RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER` is not used — and doesn't need to be,
  since there is already no extra copy to eliminate. Grepped `libretro.c` for
  `GET_CURRENT_SOFTWARE_FRAMEBUFFER`: no hits.
- Pitch is `game_width << 2` (4 bytes/pixel, XRGB8888), consistent with `JaguarSetScreenPitch`
  called on geometry change (`libretro.c:5358`).
- The only per-frame write to `videoBuffer` from libretro.c itself is the stale-tail-row
  blanking block (`libretro.c:5492-5541`), which is conditionally gated (`written > 0 && written
  < stock_height && ... <= MAX_BLANK_TAIL_ROWS && ...coverage...`) and bounded to at most 32 rows
  when it fires at all (issue #178 fix, well-commented, not a steady-state cost). No action
  needed.

**Shadowfb / True Color**: `src/tom/shadowfb.h:1-25` documents that True Color pixel substitution
happens INLINE inside TOM's CRY scanline renderer at read time (value-check against
`shadowTag`), not as a separate post-frame merge pass over `videoBuffer`. There is therefore no
"shadowfb merge" step to find in the frontend glue — it's architecturally already fused into the
render, which is the efficient design. `shadowFBActive` (default: off, `virtualjaguar_true_color`
defaults to `"disabled"`, `libretro_core_options.h:155`) gates all of it; with the option off this
costs nothing beyond the option check.

**Hi-res / Internal Resolution**: `virtualjaguar_internal_resolution` defaults to `"1x"`
(`libretro_core_options.h:169`), so `shadowHiresN == 1` and the Nx surface is never allocated on
a default install. `ShadowHiresFrameTick()` (`src/tom/shadowfb.c:644-669`) is called
unconditionally every frame (`libretro.c:5465`) but is a single `hiresEpoch` increment plus an
`if` — deliberately kept O(1) even at 1x per the comment there (the field had to become a pure
frame-phase counter for issue #400 savestate-determinism reasons; verified in project memory
`project_400_runahead_determinism`). No finding.

## 2. Dynamic resolution / geometry: correctly change-gated

- `libretro.c:5345` — the `SET_GEOMETRY` block only runs
  `if ((tomWidth != videoWidth || tomHeight != videoHeight) && tomWidth > 0 && tomHeight > 0)`.
  Confirmed this is the ONLY call site of `RETRO_ENVIRONMENT_SET_GEOMETRY` in `retro_run`; it is
  not called every frame. No finding — already optimal.

## 3. Audio hand-off: single fixed-size batch call, no chunking

- `src/jerry/dac.c:425` — `audio_batch_cb((int16_t *)buffer, length / 2)` is called exactly once
  per `SoundCallback()` invocation, which itself is called exactly once per `retro_run()`
  (`libretro.c:5470`). `length` is fixed per field (`BUFNTSC`/`BUFPAL`), so this is one batch call
  of ~800/960 sample-pairs per frame, not many small calls. No finding.
- `DACPrepareFrame()` (`dac.c:332-374`) does a handful of `double` divisions/multiplications
  (`frame_us`, `i2sStepScale`, etc.) once per frame — trivial cost, not a hot loop. Per-sample
  resampling math (`DSPSampleCallback`, the I2S ring) is DSP/audio-engine territory, out of this
  audit's frontend-glue scope, and is event-driven rather than looped once per frame here.

## 4. Input polling

- `libretro.c:2717` — `input_poll_cb()` is called exactly once per frame. Confirmed no other call
  site in `retro_run`'s call graph.
- Default config (`virtualjaguar_alt_inputs` defaults `"disabled"`,
  `libretro_core_options.h:583`): the cheap path is taken —
  `libretro.c:2731-2735`, 2 calls to `input_state_cb(player, RETRO_DEVICE_JOYPAD, 0,
  RETRO_DEVICE_ID_JOYPAD_MASK)` when `libretro_supports_bitmasks` is true (virtually all modern
  frontends including RetroArch). The more expensive per-button-bit fallback loop
  (`libretro.c:2738-2744`, up to 13 calls x 2 players) only triggers on a frontend that lacks
  bitmask support.
- **Finding (LOW, informational)**: in the default (non-`enable_alt_inputs`) branch, libretro.c
  unconditionally issues 24 `input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_*)` calls every
  frame regardless of whether any keyboard is attached — 12 for player 0
  (`libretro.c:2879-2901`: `RETROK_0..4`, `RETROK_5/6` folded into OR-expressions at 2889/2891,
  `RETROK_7/8/9`, `RETROK_MINUS/EQUALS`) and 12 for player 1 (`libretro.c:2951-2973`, same shape
  with `p,q,w,e,r,t,y,u,i,o,[,]`). These exist to give the numpad-digit shortcuts a keyboard
  fallback even without `enable_alt_inputs`. Per the task brief, `input_state_cb` can be
  comparatively expensive per call on some frontends (RetroArch routes it through its input
  remap/driver layer). 24 extra calls/frame is a small absolute number — I estimate LOW impact,
  likely sub-1% of frame time even on a weak host — but it is unconditional and not obviously
  skippable without either (a) a "is a physical keyboard present" signal the libretro API doesn't
  reliably expose, or (b) moving the fallback behind `enable_alt_inputs` as a behavior change
  (risk: regresses existing keyboard-shortcut users). Effort: S (mechanical instrumentation to
  measure/confirm) to M (any change bigger than measurement is a design decision, not mechanical
  — do NOT hand this to a cheap model to "just fix"). Recommend: measure first before touching;
  do not change without user sign-off given the behavior-visible risk.
- `InputDevAnyAttached()` (`src/jerry/inputdev.c:283-286`) is an O(1) bitmask check
  (`inputdev_attach_mask ? 1 : 0`), and the non-pad device loop at `libretro.c:3009` is gated
  behind it — with no alternate devices configured (default) this is one branch, not a loop. No
  finding.
- Team Tap: `JoystickSetTeamTap`/`JoystickGetTeamTap` (`src/jerry/joystick.c`) only affect how the
  Jaguar-side matrix decode routes reads; team-tap sockets 1-3 are NOT separately polled via
  `input_state_cb` — the adapter emulation lives entirely on the emulated-bus side, reusing the
  same `joypad0Buttons`/`joypad1Buttons` arrays already filled from the 2 polled ports. So Team
  Tap does not add extra `input_state_cb` calls even when attached. No finding.
- The per-frame `for (i = 0; i < JOYPAD_BUTTON_SLOTS; i++)` clear loop (`libretro.c:2726-2729`,
  `JOYPAD_BUTTON_SLOTS = JOYPAD_SOCKETS * 21`) clears both port arrays unconditionally
  (`src/jerry/joystick.h:61-63`) — on the order of ~168 bytes total, trivial. No finding.

## 5. `check_variables()`: correctly update-gated

- `libretro.c:5333-5334` —
  `if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) check_variables();`
  This is the ONLY call site in `retro_run`. `check_variables()` (`libretro.c:2064`) is not called
  every frame, only when RetroArch signals a variable actually changed. No finding.

## 6. Dormant-feature costs — all confirmed cheap

- **Crash-detect watchdog** (`src/core/crash_detect.c`): default option state is `"enabled"`
  (`libretro_core_options.h:226`), so it runs on every default install, not opt-in. Cost per
  frame when ON: `CrashDetectFrameTick()` (`crash_detect.c:396`) does a handful of O(1) PC/state
  checks plus one call to `fb_hash()` (`crash_detect.c:211-227`), which is a **256-sample rolling
  hash**, explicitly NOT a full-frame scan (`step = total/256` when `total > 256`) — the comment
  at `crash_detect.c:207-208` states this is deliberate ("costs 256 ops per frame"). This is
  genuinely cheap regardless of resolution (1x or Nx supersampled) since it's a fixed sample
  count, not `O(w*h)`. When OFF (`cd_mode == CRASH_DETECT_OFF`), `CrashDetectFrameTick` returns at
  `crash_detect.c:408`, one branch. No finding — already well-designed; flagging only because the
  audit brief specifically asked about its default state and per-frame cost.
- **vjtrace**: gated entirely behind `#ifdef VJ_TRACE` in `libretro.c:5301-5313`.
  `VJ_TRACE` is only defined when `TEST_EXPORTS=1` (`Makefile:161`, inside the
  `ifeq ($(TEST_EXPORTS),1)` block) — it is NOT compiled into a normal release build at all
  (`make` without `TEST_EXPORTS=1` never defines it). No finding — confirms it costs literally
  zero bytes of code in shipped builds.
- **perf_iface / `vjPerfActive`**: `src/core/perf_iface.h:30-38` documents (and this audit
  confirms by reading the header, no need to re-derive) that RetroArch accepts
  `RETRO_ENVIRONMENT_GET_PERF_INTERFACE` unconditionally and registration is never gated, so
  under ordinary RetroArch play `vjPerfActive` is 1 and every `VJP_ENTER`/`VJP_LEAVE` probe makes
  a real (fast, returns-immediately) indirect call — "on the order of 2,000 per frame" per the
  header's own comment. This is a whole-codebase number; this audit's scope only contains ONE
  probe pair (`VJP_DAC` at `dac.c:339` and `dac.c:373`), so the frontend-glue contribution here is
  2 calls/frame, negligible. The bulk of the 2,000 originates in GPU/DSP/blitter interpreter
  loops, which are the other agents' scope, not this one. Already documented as an accepted
  tradeoff in the source and in project memory (`project_509_device_perf_capture.md`) — not a new
  finding, just confirming it's understood and out of this audit's actionable scope.
- **Texture replace / dump**: `texDumpEnabled` gates `TexDumpFrame()` at the retro_run call site
  itself (`libretro.c:5320-5321`), and `texReplaceEnabled`/`tr_tab == NULL` gates
  `TexReplacePreBlit()` (`src/tom/texreplace.c:598-604`) at entry — both are off by default
  (diagnostic/opt-in features) and cost one branch when off. `TexReplacePreBlit`/`PostBlit` are
  invoked per-blit from the blitter, not per-frame from libretro.c, so their steady-state cost
  when a pack IS loaded belongs to the blitter agent's scope, not this one.
- **Cheats**: `cheat_apply_all()` (`libretro.c:5469`) → `cheat_list_apply()`
  (`src/core/cheat.c:290-306`) iterates `list->count` entries (0 with no cheats set) and skips
  disabled ones — no cost with no cheats active. No finding.
- **Blit-memo**: `BlitMemoFrame()` gated by `blitMemoMode` at the call site
  (`libretro.c:5316-5317`) — did not investigate its internals (blitter agent's scope) but the
  call-site gate is correct.
- **Netlink / JLink / UART polling**: `JLinkFrameTick()`, `JLinkPoll()`, `UARTPoll()`
  (`libretro.c:5368-5370`) run **unconditionally every frame regardless of whether netlink is
  enabled**. Traced each:
  - `JLinkPoll()` (`src/jerry/jlink.c:392-413`): checks `jlinkMode` first; with the default
    `JLINK_MODE_DISABLED` it hits neither the `NETPACKET` nor `TCP_*` branches and returns
    immediately — no socket syscalls. Cheap.
  - `JLinkFrameTick()` (`src/jerry/jlink.c:666-705`): calls `JLinkNowMs()` UNCONDITIONALLY (every
    frame, even fully disabled) → `JLinkNowUsec()` → `clock_gettime(CLOCK_MONOTONIC, ...)` on
    POSIX (`jlink.c:46-58`). This is a real kernel time query on every single frame regardless of
    netlink state, though on Linux (including RPi) `clock_gettime(CLOCK_MONOTONIC)` is normally
    vDSO-backed (no syscall trap), so the practical cost is a handful of nanoseconds — I could not
    confirm vDSO support specifically on the target RPi kernels from static code alone. Then
    checks `JLinkDiscActive()` (`src/jerry/jlink_discover.c:343-345`, an `discSock >= 0` check,
    O(1)) and `jlinkDevice == JLINK_DEVICE_VOICEMODEM` (O(1)), then returns early at
    `jlink.c:678-682` since `jlinkWaitEnabled` defaults off.
  - `UARTPoll()` (`src/jerry/uart.c:325-328`) is just `UARTKickRx()`, did not chase further — one
    call regardless.
  **Verdict**: well-gated overall; the only unconditional real work is one `clock_gettime` call
  per frame. LOW impact, almost certainly not measurable, but technically "cost when the feature
  is off should be zero branches" is violated by one syscall/vDSO-call. Effort S if someone wants
  to gate `JLinkNowMs()` behind `jlinkMode != JLINK_MODE_DISABLED || JLinkDiscActive()`, but I'd
  call this not worth the risk/churn for the expected gain — flagging only because it's the one
  place this scope found a feature-off frame calling into the OS clock unconditionally.

## 7. Per-title DB: NOT consulted per frame

- `grep -n "TitleDB" libretro.c` shows all call sites are in `check_variables()`'s option-default
  resolution path (`libretro.c:1864-1887`, itself only reachable from
  `check_variables()`, which is update-gated per item 5) and in `retro_load_game`/
  `retro_unload_game`/`retro_deinit` (`libretro.c:4479-5208`, all load/unload-time, not per
  frame). No finding — confirmed not a per-frame cost.

## 8. Allocation / memset churn per frame

- Grepped all `memcpy`/`memset` call sites in `libretro.c`. None are in `retro_run`'s call graph
  except the conditional, bounded stale-tail-row blank (item 1) and the input-button-array clears
  (item 4, ~168 bytes). All other `memcpy`/`memset` calls are at init/load/unload/state-save-load
  time. No malloc/free/calloc found in `retro_run` or its direct callees within this audit's
  scope. No finding.

## 9. Apple platform build flags (frontend-adjacent, brief compiler-flag spot check only)

- `Makefile:70` — `OPT_O3_PLATFORMS` includes `ios-arm64`, `tvos-arm64` (and `osx`), each getting
  `-O3` per the measured +5.5% macOS arm64 result documented at `Makefile:30-45` (issue #515).
  RPi targets (`rpi0`..`rpi5_64`) are also in this list — consistent with the already-merged
  per-SoC tuning in `perf/560-rpi-soc-tuning` (this branch's own recent commits,
  `project_rpi_soc_tuning.md`).
- `Makefile.common:157` — `ifneq (,$(filter ios-arm64 tvos-arm64,$(platform)))` sets NEON for
  those two platforms explicitly; `Makefile.common:164` further notes armv8+ implies NEON anyway.
  So Apple arm64 targets get both `-O3` and NEON. No gap found in the subset I checked.
- Did not do a full compiler-flag audit (explicitly another agent's job per the task brief) — did
  not check `-flto`, `-fwhole-program` interaction on iOS/tvOS specifically, or
  `-fomit-frame-pointer`/PGO opportunities. `Makefile:219` shows `-flto=4 -fwhole-program
  -fuse-linker-plugin` is used somewhere (need the surrounding platform guard to know which
  targets) — flagging as a pointer for the dedicated compiler-flags agent, not verified further
  here.

## 10. Compiler-hint opportunities in libretro.c hot paths

- Did not find an obvious high-value `__builtin_expect`/`restrict` opportunity within this
  scope's actual hot path, because there ISN'T a hot per-pixel/per-sample loop living in
  libretro.c itself (confirmed above — TOM/DAC own those loops, and they're out of scope). The
  only candidate branches are already on cold/rare paths (tail-blank mismatch, geometry change,
  crash-detect log-rate-limited warnings) where a misprediction hint would not be measurable.

---

## Summary table

| # | Item | File:line | Impact | Risk | Effort | Model |
|---|------|-----------|--------|------|--------|-------|
| 1 | Full-frame video copy | — | N/A (none found) | — | — | — |
| 2 | Shadowfb per-frame merge | — | N/A (fused into scanline render, not a separate pass) | — | — | — |
| 3 | Geometry set every frame | — | N/A (already change-gated) | — | — | — |
| 4 | Audio chunking | dac.c:425 | N/A (single batch call already) | — | — | — |
| 5 | Unconditional keyboard scan | libretro.c:2879-2901, 2951-2973 | LOW (24 calls/frame, default config) | MED if changed (visible behavior) | S to measure / M to change | measure: sonnet; change: needs design sign-off |
| 6 | check_variables() cadence | libretro.c:5333-5334 | N/A (already update-gated) | — | — | — |
| 7 | crash_detect fb_hash | crash_detect.c:211-227 | N/A (already sampled, 256 ops) | — | — | — |
| 8 | vjtrace in release | libretro.c:5301 / Makefile:161 | N/A (compiled out unless TEST_EXPORTS=1) | — | — | — |
| 9 | vjPerfActive ~2000 calls/frame | perf_iface.h:30-38 | Out of this scope's actionable area (only 2 calls here); known/accepted tradeoff | — | — | — |
| 10 | Netlink clock_gettime when disabled | jlink.c:666-682, 81-89 | LOW (one clock query/frame, likely vDSO) | LOW | S | sonnet, but likely not worth it |
| 11 | TitleDB per-frame | — | N/A (load-time/option-time only) | — | — | — |
| 12 | Apple/RPi -O3 + NEON | Makefile:70, Makefile.common:157 | N/A (already present) | — | — | — |

No HIGH or MED-tier actionable findings surfaced in this scope. The one candidate worth a second
look (#5, unconditional keyboard polling) is LOW impact and its only real "fix" carries a
user-visible behavior-change risk, so I recommend NOT touching it without explicit product
direction, and instead pointing investigation time at the GPU/DSP/blitter/bus layers other agents
are covering.
