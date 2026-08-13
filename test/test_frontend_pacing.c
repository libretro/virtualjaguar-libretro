/*
 * test/test_frontend_pacing.c — frontend pacing / fast-forward contract test.
 *
 * Regression guard for the class of bug where the core itself pins the frame
 * rate, so no frontend can run it faster than realtime and RetroArch's
 * fast-forward appears to "do nothing".
 *
 * Four assertions.  Three are exact integer/counter checks with no timing
 * sensitivity; the timing one is deliberately built to survive a loaded CI
 * machine (see fastest_frame_beats_realtime below).
 *
 *   1. fastest_frame_beats_realtime
 *      The FASTEST of N retro_run() calls must complete in well under one
 *      frame period (1/fps).  This is the load-immune form of "the core does
 *      not throttle itself": a core that sleeps, busy-waits, or blocks on an
 *      audio buffer inside retro_run pays that cost on essentially every
 *      frame, so even its quickest frame lands at ~1/fps.  Background CPU
 *      load can only make frames slower, never faster, so a busy host cannot
 *      turn this check red — whereas an average-speed check can and did
 *      (measured 0.59x realtime on this machine at load average 250).
 *      The mean/effective fps is reported as INFO alongside.
 *
 *      THRESHOLD CALIBRATION (do not tighten without re-measuring).  The
 *      fraction has to separate two populations: a self-throttling core,
 *      whose fastest frame sits at ~1.00 x the frame period, and a healthy
 *      core, whose fastest frame is whatever the host can manage.  It is
 *      therefore bounded below by the SLOWEST CI runner's honest throughput,
 *      not by how fast a dev machine happens to be.  Measured fastest-frame
 *      times against the 16.667 ms NTSC period:
 *
 *          arm64 macOS (quiet)      4.35 ms   0.26 x
 *          CI Linux x86_64          7.73 ms   0.46 x
 *          CI Linux i686         8.41-9.60 ms 0.50-0.58 x
 *
 *      The original 0.5 default landed inside the i686 spread, so that job
 *      failed on roughly half of all develop pushes — on commits that could
 *      not affect pacing at all (a core-options text change, a vjtrace perf
 *      gate, a blitter fix).  A ~50% flake on an unrelated gate is worse
 *      than no gate: it trains everyone to ignore a red run.
 *
 *      0.75 keeps the two populations well separated — a throttling core at
 *      16.67 ms still misses the 12.5 ms bar by 33%, while the slowest
 *      observed honest runner clears it by 30%.  The check still catches the
 *      bug class it exists for; it just no longer doubles as a benchmark of
 *      the runner.  If a future core change makes even a fast host approach
 *      12.5 ms, that is a real performance regression worth failing on.
 *
 *   2. audio_rate_contract
 *      Sample-frames handed to retro_audio_sample_batch over N frames must
 *      equal N * sample_rate / fps (within 1%).  A mismatch between the
 *      advertised timing and the samples actually produced makes the
 *      frontend's audio driver the pacing bottleneck: submit too many
 *      samples and the audio buffer never drains, which throttles the
 *      frontend run loop no matter how fast the emulation is.  This is what
 *      catches a future BUFNTSC/BUFPAL vs timing.fps desync (e.g. moving fps
 *      to 59.94 without touching the buffer size, or adding a region without
 *      a matching buffer constant).
 *
 *   3. one_batch_per_frame
 *      Exactly one audio batch submission per retro_run — no partial flushes
 *      that would let the frontend block part-way through a frame.
 *
 *   4. geometry_stability
 *      RETRO_ENVIRONMENT_SET_GEOMETRY must not be spammed.  Frontends
 *      re-allocate the video texture on SET_GEOMETRY; doing it every frame
 *      pins the frame rate however fast the core is, and plain benchmarks
 *      cannot see it because their environment callback is a no-op stub.
 *      Boot-time resolution changes are legitimate, hence a small allowance
 *      rather than zero.
 *
 * Usage:
 *   ./test/test_frontend_pacing [core] [rom] [--frames N]
 *       [--max-fastest-frame-fraction F]   (default 0.75 of one frame period)
 *       [--max-geometry-calls N]           (default 8)
 *       [--force-fail-{throttle,audio,geometry}]  self-test of the asserts
 *
 * The --force-fail-* flags make the corresponding threshold impossible to
 * satisfy, so the failure path can be demonstrated without editing the file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include "harness/harness.h"
#include <libretro.h>

/* Timing comes from harness_time_now() / harness_time_elapsed_sec(), which
 * are monotonic.  A wall clock (gettimeofday) must not be used here: the
 * throttle assertion is phrased against the *minimum* frame interval, so a
 * single backwards clock step — NTP correction, manual clock change — would
 * produce a negative delta, drag min_frame below the limit and report a
 * false PASS on exactly the bug this test exists to catch. */

/* Under ASan/UBSan the core runs roughly 5x slower than realtime, so the
 * throttle assertion cannot pass no matter how the core behaves — it would
 * be a permanent false red in the sanitizer job.  Report it as SKIP there
 * rather than PASS: the point of this assertion is that a self-throttling
 * core cannot hide, and a sanitizer build genuinely cannot answer that.
 * The other three assertions are exact counter checks and still run. */
#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer) \
   || __has_feature(thread_sanitizer)
#    define VJ_SANITIZER_BUILD 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(VJ_SANITIZER_BUILD)
#  define VJ_SANITIZER_BUILD 1
#endif
/* gcov needs a runtime signal rather than a macro: `make coverage` builds
 * the *core* at -O0 with --coverage, but the test binaries compile with
 * $(INCFLAGS) only, so no -D reaches them.  The coverage target exports
 * VJ_INSTRUMENTED_BUILD=1 instead. */
static int instrumented_build(void)
{
#ifdef VJ_SANITIZER_BUILD
    return 1;
#else
    const char *e = getenv("VJ_INSTRUMENTED_BUILD");
    return (e && e[0] && strcmp(e, "0") != 0) ? 1 : 0;
#endif
}

typedef struct {
    uint64_t t_prev;
    double   min_frame;      /* smallest usable (positive) interval, seconds */
    double   max_frame;
    double   sum_frame;
    unsigned timed_frames;   /* retro_run() calls timed */
    unsigned usable_frames;  /* of those, ones with a positive interval */
} frame_timer;

/* Called by the harness after every retro_run(). */
static bool on_frame(void *userdata, unsigned frame)
{
    frame_timer *ft = (frame_timer *)userdata;
    uint64_t now = harness_time_now();
    double dt = harness_time_elapsed_sec(ft->t_prev, now);
    (void)frame;
    ft->t_prev = now;
    ft->timed_frames++;
    ft->sum_frame += (dt > 0.0) ? dt : 0.0;
    if (dt > ft->max_frame) ft->max_frame = dt;
    /* Belt and braces on top of the monotonic source: reject a non-positive
     * interval rather than clamping it.  min_frame = 0 would satisfy the
     * throttle assertion unconditionally. */
    if (dt > 0.0) {
        if (dt < ft->min_frame) ft->min_frame = dt;
        ft->usable_frames++;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    void (*p_av_info)(struct retro_system_av_info *);
    struct retro_system_av_info av;
    harness_result results[8];
    frame_timer ft;
    unsigned nres = 0;
    /* 0.75, not 0.5 -- see THRESHOLD CALIBRATION at the top of this file. */
    double max_fastest_fraction = 0.75;
    unsigned max_geometry_calls = 8;
    int force_fail_throttle = 0, force_fail_audio = 0, force_fail_geometry = 0;
    uint64_t t0;
    double elapsed, realtime, frame_period, mean_frame;
    double expected_frames_f, tolerance, sample_err;
    /* 64-bit: --frames N is user-supplied, and total_samples can exceed
     * 32-bit long on LLP64 platforms for large N. */
    int64_t expected_samples, actual_samples;
    char detail_throttle[320], detail_audio[256], detail_batch[160], detail_geom[224];
    int ok_throttle, ok_audio, ok_batch, ok_geom, all_ok;
    int i;

    cfg.frames = 300;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-fastest-frame-fraction") == 0 && i + 1 < argc) {
            /* strtod, not atof: atof returns 0 on a malformed value, which
             * would silently set the threshold to 0 and force a failure. */
            char *end = NULL;
            double v = strtod(argv[++i], &end);
            if (end == argv[i] || *end != '\0' || v <= 0.0) {
                fprintf(stderr, "invalid --max-fastest-frame-fraction: %s\n",
                        argv[i]);
                return 2;
            }
            max_fastest_fraction = v;
        } else if (strcmp(argv[i], "--max-geometry-calls") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long v = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                fprintf(stderr, "invalid --max-geometry-calls: %s\n", argv[i]);
                return 2;
            }
            max_geometry_calls = (unsigned)v;
        }
        else if (strcmp(argv[i], "--force-fail-throttle") == 0)
            force_fail_throttle = 1;
        else if (strcmp(argv[i], "--force-fail-audio") == 0)
            force_fail_audio = 1;
        else if (strcmp(argv[i], "--force-fail-geometry") == 0)
            force_fail_geometry = 1;
    }

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;

    if (!cfg.rom_path) {
        fprintf(stderr, "test_frontend_pacing: no ROM given; nothing to measure\n");
        return 1;
    }

    p_av_info = (void (*)(struct retro_system_av_info *))
                harness_dlsym(&cfg, "retro_get_system_av_info");
    if (!p_av_info) {
        fprintf(stderr, "test_frontend_pacing: retro_get_system_av_info missing\n");
        return 1;
    }

    if (!harness_load_rom(&cfg))
        return 1;

    memset(&av, 0, sizeof(av));
    p_av_info(&av);

    if (!(av.timing.fps > 0.0) || !(av.timing.sample_rate > 0.0)) {
        fprintf(stderr,
                "test_frontend_pacing: bogus av_info (fps=%f sample_rate=%f)\n",
                av.timing.fps, av.timing.sample_rate);
        harness_shutdown(&cfg);
        return 1;
    }

    if (!cfg.quiet)
        printf("av_info: fps=%.6f sample_rate=%.1f base=%ux%u max=%ux%u\n",
               av.timing.fps, av.timing.sample_rate,
               av.geometry.base_width, av.geometry.base_height,
               av.geometry.max_width, av.geometry.max_height);

    /* Reset counters so boot-time SET_GEOMETRY and the boot resolution change
     * from retro_load_game are not counted; we only care about steady-state
     * churn.  harness_reset_video clears total_frames_rendered and
     * last_width/last_height too, so both dimension_changes and the reported
     * "last WxH" below describe the timed window rather than boot state. */
    harness_reset_video(&cfg);
    harness_reset_audio(&cfg);

    memset(&ft, 0, sizeof(ft));
    ft.min_frame = 1.0e9;
    cfg.frame_callback = on_frame;
    cfg.frame_callback_data = &ft;

    t0 = harness_time_now();
    ft.t_prev = t0;
    harness_run(&cfg);
    elapsed = harness_time_elapsed_sec(t0, harness_time_now());

    frame_period = 1.0 / av.timing.fps;
    realtime     = (double)cfg.frames * frame_period;
    mean_frame   = (ft.timed_frames > 0)
                 ? ft.sum_frame / (double)ft.timed_frames : 0.0;
    if (ft.usable_frames == 0)
        ft.min_frame = 0.0;
    if (force_fail_throttle)
        max_fastest_fraction = 0.0;
    if (force_fail_geometry)
        max_geometry_calls = 0;

    /* --- 1. fastest frame must clear the frame period --------------- */
    ok_throttle = (ft.usable_frames > 0)
               && (ft.min_frame < max_fastest_fraction * frame_period);
    snprintf(detail_throttle, sizeof(detail_throttle),
             "fastest frame %.3f ms must be < %.3f ms (%.2f x frame period "
             "%.3f ms); mean %.3f ms, slowest %.3f ms; %u/%u intervals usable; "
             "%u frames in %.3fs (realtime %.3fs, %.2fx, %.1f eff. fps)",
             ft.min_frame * 1000.0, max_fastest_fraction * frame_period * 1000.0,
             max_fastest_fraction, frame_period * 1000.0,
             mean_frame * 1000.0, ft.max_frame * 1000.0,
             ft.usable_frames, ft.timed_frames,
             cfg.frames, elapsed, realtime,
             (elapsed > 0.0) ? realtime / elapsed : 0.0,
             (elapsed > 0.0) ? (double)cfg.frames / elapsed : 0.0);

    /* --- 2. audio-rate contract ------------------------------------- */
    expected_frames_f = (double)cfg.frames * av.timing.sample_rate / av.timing.fps;
    expected_samples  = (int64_t)(expected_frames_f + 0.5);
    actual_samples    = (int64_t)cfg.audio.total_samples;
    if (force_fail_audio)
        actual_samples = 0;
    sample_err = (expected_frames_f > 0.0)
               ? fabs((double)actual_samples - expected_frames_f) / expected_frames_f
               : 1.0;
    tolerance = 0.01;
    ok_audio = (sample_err <= tolerance);
    snprintf(detail_audio, sizeof(detail_audio),
             "submitted %" PRId64 " sample-frames, expected %" PRId64 " "
             "(%u frames x %.1f Hz / %.4f fps); error %.3f%% (limit %.1f%%)",
             actual_samples, expected_samples, cfg.frames,
             av.timing.sample_rate, av.timing.fps,
             sample_err * 100.0, tolerance * 100.0);

    /* --- 3. one batch per frame ------------------------------------- */
    ok_batch = (cfg.audio.total_batch_calls == cfg.frames);
    snprintf(detail_batch, sizeof(detail_batch),
             "%u audio batch calls for %u frames (expected 1:1)",
             cfg.audio.total_batch_calls, cfg.frames);

    /* --- 4. geometry stability -------------------------------------- */
    ok_geom = (cfg.video.set_geometry_calls <= max_geometry_calls);
    snprintf(detail_geom, sizeof(detail_geom),
             "SET_GEOMETRY x%u over %u frames (limit %u); SET_SYSTEM_AV_INFO x%u; "
             "reported-dimension changes %u; last %ux%u",
             cfg.video.set_geometry_calls, cfg.frames, max_geometry_calls,
             cfg.video.set_av_info_calls, cfg.video.dimension_changes,
             cfg.video.last_width, cfg.video.last_height);

    if (instrumented_build()) {
        results[nres].status = "SKIP";
        strncat(detail_throttle,
                "  [SKIP: instrumented build, timing not meaningful]",
                sizeof(detail_throttle) - strlen(detail_throttle) - 1);
    } else {
        results[nres].status = ok_throttle ? "PASS" : "FAIL";
    }
    results[nres].name   = "fastest_frame_beats_realtime";
    results[nres].detail = detail_throttle;
    nres++;

    results[nres].status = ok_audio ? "PASS" : "FAIL";
    results[nres].name   = "audio_rate_contract";
    results[nres].detail = detail_audio;
    nres++;

    results[nres].status = ok_batch ? "PASS" : "FAIL";
    results[nres].name   = "one_batch_per_frame";
    results[nres].detail = detail_batch;
    nres++;

    results[nres].status = ok_geom ? "PASS" : "FAIL";
    results[nres].name   = "geometry_stability";
    results[nres].detail = detail_geom;
    nres++;

    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);

    /* When the throttle result is SKIP it must not gate the exit code --
     * but the three counter checks always do. */
    all_ok = ok_audio && ok_batch && ok_geom
             && (instrumented_build() || ok_throttle);
    return all_ok ? 0 : 1;
}
