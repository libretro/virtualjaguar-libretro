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
 *       [--max-fastest-frame-fraction F]   (default 0.5 of one frame period)
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
#include <sys/time.h>

#include "harness/harness.h"
#include <libretro.h>

typedef struct {
    double t_prev;
    double min_frame;
    double max_frame;
    double sum_frame;
    unsigned samples;
} frame_timer;

static double now_seconds(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

/* Called by the harness after every retro_run(). */
static bool on_frame(void *userdata, unsigned frame)
{
    frame_timer *ft = (frame_timer *)userdata;
    double now = now_seconds();
    double dt = now - ft->t_prev;
    (void)frame;
    ft->t_prev = now;
    if (dt < ft->min_frame) ft->min_frame = dt;
    if (dt > ft->max_frame) ft->max_frame = dt;
    ft->sum_frame += dt;
    ft->samples++;
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
    double max_fastest_fraction = 0.5;
    unsigned max_geometry_calls = 8;
    int force_fail_throttle = 0, force_fail_audio = 0, force_fail_geometry = 0;
    double t0, elapsed, realtime, frame_period, mean_frame;
    double expected_frames_f, tolerance, sample_err;
    long expected_samples, actual_samples;
    char detail_throttle[320], detail_audio[256], detail_batch[160], detail_geom[224];
    int ok_throttle, ok_audio, ok_batch, ok_geom, all_ok;
    int i;

    cfg.frames = 300;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-fastest-frame-fraction") == 0 && i + 1 < argc)
            max_fastest_fraction = atof(argv[++i]);
        else if (strcmp(argv[i], "--max-geometry-calls") == 0 && i + 1 < argc)
            max_geometry_calls = (unsigned)atoi(argv[++i]);
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

    /* Reset counters so boot-time SET_GEOMETRY from retro_load_game is not
     * counted; we only care about steady-state churn. */
    cfg.video.set_geometry_calls = 0;
    cfg.video.set_av_info_calls  = 0;
    cfg.video.dimension_changes  = 0;
    harness_reset_audio(&cfg);

    memset(&ft, 0, sizeof(ft));
    ft.min_frame = 1.0e9;
    cfg.frame_callback = on_frame;
    cfg.frame_callback_data = &ft;

    t0 = now_seconds();
    ft.t_prev = t0;
    harness_run(&cfg);
    elapsed = now_seconds() - t0;

    frame_period = 1.0 / av.timing.fps;
    realtime     = (double)cfg.frames * frame_period;
    mean_frame   = (ft.samples > 0) ? ft.sum_frame / (double)ft.samples : 0.0;
    if (ft.samples == 0)
        ft.min_frame = 0.0;
    if (force_fail_throttle)
        max_fastest_fraction = 0.0;
    if (force_fail_geometry)
        max_geometry_calls = 0;

    /* --- 1. fastest frame must clear the frame period --------------- */
    ok_throttle = (ft.samples > 0)
               && (ft.min_frame < max_fastest_fraction * frame_period);
    snprintf(detail_throttle, sizeof(detail_throttle),
             "fastest frame %.3f ms must be < %.3f ms (%.2f x frame period "
             "%.3f ms); mean %.3f ms, slowest %.3f ms; %u frames in %.3fs "
             "(realtime %.3fs, %.2fx, %.1f eff. fps)",
             ft.min_frame * 1000.0, max_fastest_fraction * frame_period * 1000.0,
             max_fastest_fraction, frame_period * 1000.0,
             mean_frame * 1000.0, ft.max_frame * 1000.0,
             cfg.frames, elapsed, realtime,
             (elapsed > 0.0) ? realtime / elapsed : 0.0,
             (elapsed > 0.0) ? (double)cfg.frames / elapsed : 0.0);

    /* --- 2. audio-rate contract ------------------------------------- */
    expected_frames_f = (double)cfg.frames * av.timing.sample_rate / av.timing.fps;
    expected_samples  = (long)(expected_frames_f + 0.5);
    actual_samples    = (long)cfg.audio.total_samples;
    if (force_fail_audio)
        actual_samples = 0;
    sample_err = (expected_frames_f > 0.0)
               ? fabs((double)actual_samples - expected_frames_f) / expected_frames_f
               : 1.0;
    tolerance = 0.01;
    ok_audio = (sample_err <= tolerance);
    snprintf(detail_audio, sizeof(detail_audio),
             "submitted %ld sample-frames, expected %ld "
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

    results[nres].status = ok_throttle ? "PASS" : "FAIL";
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

    all_ok = ok_throttle && ok_audio && ok_batch && ok_geom;
    return all_ok ? 0 : 1;
}
