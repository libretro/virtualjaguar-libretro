# Frontend pacing, fast-forward, and the av_info contract

This note exists because "fast-forward doesn't work with this core" is a
recurring report, and the answer is almost never a wall-clock limiter in the
core. Read this before adding one, and before filing it as a core bug.

## What the core promises the frontend

`retro_get_system_av_info()` (libretro.c) advertises:

| field | NTSC | PAL |
|---|---|---|
| `timing.fps` | 60 | 50 |
| `timing.sample_rate` | 48000 | 48000 |

`retro_run()` submits audio exactly once per frame via
`SoundCallback()` (`src/jerry/dac.c`), with `BUFNTSC / 2 = 800`
sample-frames for NTSC and `BUFPAL / 2 = 960` for PAL. Both work out to
exactly 48 000 sample-frames per emulated second, so the advertised timing
and the samples actually produced agree.

**That agreement is load-bearing.** If the core submitted more samples per
second than `sample_rate * 1` implies, the frontend's audio buffer would
never drain, and the audio driver — not the emulation — would become the
pacing bottleneck. In that state fast-forward has nothing to give, because
the run loop is blocked on `audio_driver_write()` regardless of how fast
`retro_run()` returns. `test/test_frontend_pacing.c` asserts this contract
on every `make test`.

The core contains **no** `usleep` / `nanosleep` / `clock_gettime`-based
frame limiter, does not register
`RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK` or
`RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK`, and never blocks inside
`retro_run()`. Do not add any of these. Pacing is the frontend's job.

## Why fast-forward can still look broken

RetroArch injects its own sleep in `runloop_iterate()` only when
`frame_limit_minimum_time != 0`. That value comes from
`runloop_set_frame_limit(av_info, ratio)`:

```c
if (fastforward_ratio < 0.1f)
   frame_limit_minimum_time = 0;                       /* unlimited */
else
   frame_limit_minimum_time = 1000000.0f / (fps * fastforward_ratio);
```

So the frontend settings that decide whether fast-forward does anything are:

- **Settings → Frame Throttle → Fast-Forward Rate** (`fastforward_ratio`).
  Set to `1.0x` this paces fast-forward at exactly the content frame rate —
  holding the fast-forward key then changes nothing at all. `0.0`
  (unlimited) is what most people want.
- **Settings → Frame Throttle → Sync to Exact Content Framerate**
  (`vrr_runloop_enable`). When enabled, the frame limiter is applied on
  every iteration rather than only during fast-forward.
- **Settings → Audio → Synchronous Audio** (`audio_sync`) and
  **Settings → Video → Vertical Sync**. RetroArch drops both into
  non-blocking mode while fast-forwarding; if something keeps them
  blocking, the run loop stays paced by the audio buffer or the display.
- On macOS, a **background or occluded RetroArch window** is throttled by
  the OS. Measured on an M2 Max with a 120 Hz panel: the same core, same
  config, ran 1800 frames at 120.8 fps with the window forward and at
  59.5 fps when it was not. That alone can look exactly like
  "fast-forward is broken".

## What the ceiling actually is

Fast-forward cannot exceed the core's own throughput. Virtual Jaguar's
`retro_run()` costs roughly 3.3 ms/frame on an idle Apple M2 Max for
`test/roms/yarc.j64` (≈300 fps, ≈5x realtime) — but heavier titles and
slower hosts land much closer to 16.7 ms, at which point holding
fast-forward produces little or no visible speed-up even though everything
is working as designed. Check `make benchmark` for the host's headroom
before treating a small fast-forward gain as a bug.

Instrumenting `retro_run()` under RetroArch is the way to tell the two
apart: bracket the whole function and compare time spent *inside*
`retro_run()` against wall-clock time between calls. Time outside
`retro_run()` collapsing to near zero when fast-forward is engaged means
the frontend is unblocked and the remaining limit is the core's own cost.

## Regression test

`test/test_frontend_pacing.c` (in `make test`, needs `test/roms/yarc.j64`):

1. `fastest_frame_beats_realtime` — the quickest of 300 `retro_run()` calls
   must finish in under half a frame period. A core that sleeps pays that
   cost on nearly every frame, so even its fastest frame lands at ~1/fps.
   Deliberately phrased against the *minimum* rather than the mean:
   background CPU load can only make frames slower, so a busy CI host
   cannot turn this red, while an average-based check can (measured 0.59x
   realtime on a machine at load average 250).
2. `audio_rate_contract` — submitted sample-frames must equal
   `frames * sample_rate / fps` within 1%.
3. `one_batch_per_frame` — exactly one `retro_audio_sample_batch` call per
   `retro_run`.
4. `geometry_stability` — `RETRO_ENVIRONMENT_SET_GEOMETRY` must not be
   spammed; frontends re-allocate the video texture on each call.
