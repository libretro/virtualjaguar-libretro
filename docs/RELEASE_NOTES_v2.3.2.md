# Virtual Jaguar libretro v2.3.2

A correctness and housekeeping release: one game-compatibility fix, a batch of
blitter accuracy work checked against the Jaguar Technical Reference Manual,
clearer core options, smaller binaries, and new regression tests around save
states and frontend pacing.

Save states written by v2.3.0 and v2.3.1 **still load** in this release. That
was not free — see "Save-state compatibility" below.

## Highlights

- **Battle Sphere Gold now launches with the HLE BIOS** ([#181]). Previously a
  black, silent screen unless you supplied a real BIOS image; it now boots
  through the intro to a playable main menu on default settings. The HLE DSPGO
  auto-clear now keys off non-zero audio samples instead of raw write count, so
  a DSP that spins while writing silence no longer wedges a game's boot.

- **Clearer core options** ([#184]). The two most confusing toggles now read as
  the choices they actually are:

  | Option | Was | Now |
  |---|---|---|
  | Blitter | "Fast Blitter" — enabled/disabled | **Blitter** — Accurate / Fast |
  | BIOS | "BIOS" — enabled/disabled | **BIOS** — HLE / Real |

  Descriptions were rewritten to explain what each choice does. Your existing
  settings carry over unchanged — the stored values were deliberately left
  alone, only the labels changed.

- **Blitter accuracy.** Ten fixes bringing the fast and accurate blitters closer
  to the JTRM-documented hardware: DCOMPEN colour-keying against PATTERNDATA,
  A2 texture-wrap masking, CLIP_A1 caching at sread and dwrite, the final
  outer-loop step in accurate writeback, and z-comparator lane matching in
  16 bpp pixel mode. The BIOS boot-animation artifacts reported in [#180] are
  fixed. These are correctness fixes: on the titles we test, no other rendering
  change was measurable, so treat the rest as groundwork rather than a visible
  improvement. [#189] remains open, narrowed to A2_PIXEL writeback on
  UPDA2-clear commands (cosmetically inert in the cases measured).

- **Smaller binaries.** Two unused 128 KB boot-ROM blobs
  (`jaguarDevBootROM1`/`2`) were linked into every build and referenced by
  nothing. Removing them cuts **258 KB** off each binary — about 4 MB across the
  16 platforms CI publishes.

- **Correct video geometry advertisement.** `retro_get_system_av_info` reported
  `max_height = 240` for NTSC while the core can emit 241-line frames (games
  program `VDB`/`VDE` themselves; `yarc` asks for a 241-line window). A frame
  taller than the advertised maximum violates the libretro spec and can be
  clipped or dropped by some video drivers. The advertised bound is now 256,
  matching what the core can actually produce.

## Save-state compatibility

States from v2.3.0 and v2.3.1 continue to load. A change during this cycle added
a field to the DAC block and bumped the state format version, and because
`retro_unserialize` demanded an exact version match that would have silently
invalidated every existing save. `retro_unserialize` now accepts a version
*range* while still writing the current format, with the newer field skipped for
older states.

States from **v2.2.0 and earlier** are still rejected — that break shipped in
v2.3.0 and is not new here.

## Fast-forward

RetroArch fast-forward was investigated after a report that it "does nothing"
with this core. **No core-side defect was found**: there is no wall-clock
throttle in the frame path, and the advertised fps/sample-rate contract matches
the audio actually submitted (800 sample-frames/frame NTSC, 960 PAL — both
exactly 48 kHz). Measured under RetroArch, enabling fast-forward collapses
out-of-core blocking by 5x; the frame rate is then limited by emulation cost.

If fast-forward appears to do nothing, check **Frame Throttle → Fast-Forward
Rate** (a value of `1.0x` paces at exactly content speed — use `0.0` for
unlimited), **Sync to Exact Content Framerate**, and **Synchronous Audio**. On
macOS, also keep the RetroArch window frontmost, which throttles background
windows. Details in [`docs/frontend-pacing.md`](frontend-pacing.md).

## Testing

New regression tests, all wired into `make test`:

- **`test_state_compat`** — 14 assertions on the save-state version gate: an
  older-layout state loads, older-still and future versions are refused, and the
  post-load DAC block must match byte-for-byte. Verified to fail against the
  broken behaviour, separately for each half of the fix.
- **`test_frontend_pacing`** — asserts the core can be driven faster than
  realtime and that samples-per-frame match the advertised timing. The timing
  assertion is phrased against the *fastest* frame, so a loaded CI machine
  cannot produce a false failure while a self-throttling core still cannot pass.
- **`test_framebuffer_integrity`** gains a check that emitted frames fit the
  advertised geometry, run for both NTSC and PAL.
- **Build-identity guard** — harnesses now refuse to run against a stale binary
  when `VJ_EXPECT_BUILD` is set, closing a class of silently-testing-old-code
  failures.

Also fixed: an incorrect WID (window width) encoding in the distilled blitter
reference, which mis-described the hardware's floating-point width format.

## Known issues

- **Iron Soldier** ([#186]): starting a mission or DEMO mode drops to a black
  screen, and the briefing's wireframe tank is missing with the Blitter set to
  Fast. Unchanged from v2.3.1.
- **Alien vs Predator** ([#178]) and **Tempest 2000** ([#187]) still show the
  reported graphical glitches. The Tempest 2000 one-frame playfield collapse is
  now known to recur every 161 frames exactly.
- **Pitfall: The Mayan Adventure** ([#138]) still black-screens shortly after
  starting; now reproducible headlessly, and the blitter has been ruled out.
- **Skyhammer** and **Iron Soldier 2** still produce clipped/saturated audio.
- Jaguar CD support is still in development on a branch and is not part of this
  release.
- Bus contention is not modelled, so the related acid tests remain expected
  failures.

## Stats

```
40 files changed, 1804 insertions(+), 8352 deletions(-)
```

30 commits since v2.3.1.

## Downloads

CI publishes builds for 16 platforms alongside this release, with a
`SHA256SUMS` manifest. See [`SECURITY.md`](../SECURITY.md) for verification.

## Maintainers

Joseph Mattiello, with contributions from altiereslima and dependabot.

[#138]: https://github.com/libretro/virtualjaguar-libretro/issues/138
[#178]: https://github.com/libretro/virtualjaguar-libretro/issues/178
[#180]: https://github.com/libretro/virtualjaguar-libretro/issues/180
[#181]: https://github.com/libretro/virtualjaguar-libretro/issues/181
[#184]: https://github.com/libretro/virtualjaguar-libretro/issues/184
[#186]: https://github.com/libretro/virtualjaguar-libretro/issues/186
[#187]: https://github.com/libretro/virtualjaguar-libretro/issues/187
[#189]: https://github.com/libretro/virtualjaguar-libretro/issues/189
