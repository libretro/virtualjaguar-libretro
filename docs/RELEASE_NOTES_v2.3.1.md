# Virtual Jaguar libretro v2.3.1

Polish release — 17 commits since v2.3.0.  No new emulation features;
this is an infrastructure / triage / safety pass that was on develop
between v2.3.0 (April 2026) and now.  None of the long-standing audio
or game-compatibility issues changed; the difference is that future
bug reports about them have a real chance of being actionable.

## Highlights

- **Runtime crash watchdog** — a new opt-out core option,
  `Crash Detect`, logs `gpu_pc_escape` / `dsp_pc_escape` / `gpu_wedge`
  / `dsp_wedge` / `video_stall` events to the RetroArch log when a
  game hangs or goes black.  Bug reports about freezes can now be
  triaged from the log alone, without save states or replays.
- **DSP wedge fix** — when the DSP PC escapes into unmapped memory,
  the inner exec loop now drains the timeslice instead of busy-looping
  for minutes.  This was a v2.3.0 regression that made the headless
  test harness hang on Wolfenstein 3D for 12+ minutes per session.
- **Audio presence test** (`test/test_audio_presence.c`) — counterpart
  to the existing clipping check.  Catches the silencing-regression
  class where a "fix" drops a game's RMS to zero (clipping check
  passes silence as PASS).  Iron Soldier 1 is the in-tree baseline
  (RMS ~1175 on develop).  Both audio tests now run in `make test`
  and CLAUDE.md documents that audio / DSP changes must clear both
  before being declared done.
- **Bug-report template** clarified — the BIOS-mode dropdown
  previously implied users needed to supply `jagboot.rom` in
  `system/`, which has never been the case for this libretro core
  (the BIOS is bundled in the dylib).  The template now distinguishes
  the boot **path** users had selected, which is what bug triage
  actually cares about.

## What's new

### Bug fixes

- **DSP**: bail out of the exec loop when the DSP PC is in unmapped
  memory.  Avoids a busy-loop that burned the entire timeslice
  emulating opcodes fetched from bus-default 0xFFFF.  Wolf3D headless
  test now completes in <30 seconds, matching v2.2.0 wall-clock.

### Tooling

- **`src/core/crash_detect.c`** — opt-out runtime watchdog.  Once-per-
  frame hook with cheap PC-range checks and a 256-pixel rolling
  framebuffer hash; off-mode short-circuits at the first instruction.
  Modes: `enabled` (default) / `disabled` / `verbose` (heartbeat
  every 600 frames).
- **`test/test_audio_presence.c`** — asserts the audio onset is
  reached and the window RMS lies in `[floor, ceiling]`, with no
  long zero runs.  Per-ROM thresholds via CLI args.  Wired into
  `make test` against Iron Soldier 1.
- **`test/tools/test_video_dim_log.c`** — diagnostic harness that
  prints every video resolution change and black-frame streak.
  Useful for correlating "crash after N seconds" reports against
  actual hardware events.

### CI / infrastructure

- Dependabot bumps for GitHub Actions (codecov-action 5→6,
  actions/cache 4→5, actions/labeler 5→6, actions/upload-artifact
  4→7, cirrus-actions/rebase 1.4→1.8).
- `test/tools/test_frame_timing` added to `.gitignore`.

### Documentation

- **CLAUDE.md** — new "Runtime crash watchdog" and "Audio / DSP work
  — required tests" sections.  Pins the rule that DSP/audio PRs must
  clear both `test_audio_clipping` AND `test_audio_presence`, with
  PR #170 cited as the precedent (clipping passed on a "fix" that
  silenced Iron Soldier 2).
- **Bug-report template** — clarified HLE vs Real BIOS distinction.

## Known issues (unchanged from v2.3.0)

- **Doom**: runs at ~2x speed, music silent.  Bus arbitration / cycle
  accuracy modeling not implemented; PR #169 (draft) is in design
  limbo.
- **Wolfenstein 3D**: no audio.  The dsp-diag tool shows the DSP
  PC drifting to $0006EE in main RAM around frame 48 (Bank1
  registers never finish initializing); the runtime watchdog
  subsequently logs `dsp_pc_escape pc=$00FFF004E8` once the
  drift hits unmapped register space, and the new wedge bail-out
  (this release) prevents the headless harness from hanging on it.
  Long-standing — present in v2.2.0 too, not a v2.3.x regression.
- **Skyhammer / Iron Soldier 2**: saturated square-wave audio in HLE
  (DSP engine state not fully replicated).  Both tests deliberately
  flag these as `EXPECTED-FAIL` in the suite manifest until a real
  fix lands.
- **Battle Sphere**: menu navigation issues.
- **Jaguar CD**: PR #120 is rebased and builds, but multi-track CD
  games still break mid-game; not in this release.

## Compared to v2.3.0

18 files changed, +1,004 / -17 lines across 17 commits.

## Downloads

Pre-built libretro cores for 16 platforms:

- Linux: x86_64, aarch64, i686
- macOS: arm64, x86_64
- Windows: x86_64, i686 (MSYS2/MinGW)
- iOS: arm64; tvOS: arm64
- Android: arm64-v8a, armeabi-v7a, x86_64, x86
- Web: Emscripten WASM
- Consoles: PS Vita, Nintendo Switch

Each binary has a matching `*-debug.tar.gz` with split debug symbols.
SHA256 checksums in `SHA256SUMS.txt`.

## Maintainers

libretro/Provenance fork: Joseph Mattiello (@JoeMatt, Provenance-Emu).

Thanks to @checktext00 for the bug-report template fix (#179).
