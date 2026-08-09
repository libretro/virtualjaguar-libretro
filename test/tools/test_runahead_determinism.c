/* test_runahead_determinism.c -- Verify save states are deterministic.
 *
 * This is the test behind the core's `savestate_features = 3`
 * ("deterministic") claim in dist/info/virtualjaguar_libretro.info, and
 * behind reporting zero serialization quirks from retro_load_game.
 *
 * Reporting level 3 tells the frontend that a state is a COMPLETE
 * snapshot: restoring it and re-running must reproduce the same frames
 * bit-for-bit.  RetroArch's run-ahead depends on exactly that — its
 * single-instance mode runs the emulator forward, presents a later
 * frame, then rolls back with retro_unserialize.  Any emulator state
 * that lives outside the state blob (a static that never got
 * serialized, a scheduled event, an audio resampler phase) shows up as
 * divergence after the rollback, which run-ahead turns into visible
 * glitching.
 *
 * The test mirrors that loop directly:
 *
 *   1. run WARMUP frames to get past boot into steady-state emulation
 *   2. serialize  -> state A
 *   3. run WINDOW frames, digesting each frame's video + audio  -> pass 1
 *   4. serialize  -> state B   (the "after" state, for reference)
 *   5. unserialize state A
 *   6. run WINDOW frames again, same (empty) input               -> pass 2
 *   7. serialize  -> state C
 *   8. unserialize state A again, run WINDOW frames              -> pass 3
 *
 * Assertions:
 *   - video_replay_identical    pass 1 == pass 2, per-frame video digests
 *   - audio_replay_identical    pass 1 == pass 2, per-frame audio digests
 *   - state_reconverges         state C == state B, byte for byte
 *   - repeated_rollback_agrees  pass 2 == pass 3, video and audio
 *
 * A state that round-trips but silently drops a field usually passes a
 * naive "save, load, does it still run?" check and fails here, because
 * the dropped field perturbs the next few frames.
 *
 * Input is held empty across the measured window on purpose: run-ahead
 * replays frames with the SAME input it already used, so equal input is
 * the condition under test.  Divergence here is core state, not input.
 *
 * Why pass 3 exists, and why it is the load-bearing check for run-ahead:
 * pass 1 is the only pass reached WITHOUT an intervening
 * retro_unserialize, so anything the load path fails to restore shows up
 * as a pass-1-vs-pass-2 difference and nowhere else.  Run-ahead rolls
 * back on every single frame, so it lives entirely in the post-rollback
 * regime that pass 2 vs pass 3 measures.
 *
 * ROOT CAUSE THIS TEST FOUND (fixed; kept here as the map of the area):
 * every one of these assertions passed EXCEPT audio_replay_identical,
 * which failed on exactly one frame — the first after the first rollback —
 * by ~0.05% RMS with an identical sample count, on three commercial
 * titles.  Instrumenting DACPrepareFrame and diffing the two passes field
 * by field showed every DSP field identical (pc, control, flags, acc,
 * pipeline pointers, scoreboard, registers) and only LTXD/RTXD differing.
 *
 * Those are the DAC output registers, and they live in jagMemSpace at
 * $F1A148/$F1A14C — which no STATE_SAVE_BUF covered.  retro_serialize
 * saves jaguarMainRAM (the low 2 MB of jagMemSpace), tomRam8 and
 * jerry_ram_8, and jerry_ram_8 is a SEPARATE array in jerry.c, not the
 * $F10000 window of jagMemSpace.  So the whole DAC register file survived
 * a load only by accident, as whatever the previous run left behind.
 * DACPrepareFrame seeds the resampler's interpolation endpoints from
 * LTXD/RTXD, so the first frame after a rollback started from the wrong
 * endpoints and then converged.  Fixed by serializing the registers in
 * DACStateSave/Load behind STATE_VERSION_DAC_REGISTERS.
 *
 * Things that looked guilty and were NOT the cause — do not re-spend the
 * time:
 *
 *  - The I2S ring buffer (i2sRingL/R), despite being absent from
 *    DACStateSave.  DACPrepareFrame reseeds writePos/writeCount to 2 every
 *    frame and DSPSampleCallback clamps its read index to i2sWriteCount, so
 *    no sample written before this frame can ever be read.
 *  - dspFlagsRetireDelay / dspPreStoreBank, which DSPStateLoad explicitly
 *    retires rather than restoring — audio-path state, reset on load,
 *    producing exactly the observed shape.  Implemented behind a state
 *    version gate and measured: NO CHANGE on any title.  The retire window
 *    is DSP_FLAGS_RETIRE_DELAY == 2 instruction slots, so a frame boundary
 *    practically never lands inside it.  Reverted.
 *  - The event queue.  All nine scheduled callbacks are present in
 *    event.c's callback_registry, so none is dropped by the
 *    pointer->id->pointer round trip.
 *  - jaguar_prng_state (JaguarRand() runs only from reset/init paths),
 *    dspgo_poll_count (pinned to 0 while audio is non-silent), and
 *    m68kInLongWrite / m68kBusNoCharge / m68kScaleAccum (balanced nesting
 *    counters, or inert at stock 1x clock scale).
 *
 * Build (see the `runahead-determinism` Makefile target):
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/test_runahead_determinism \
 *      test/tools/test_runahead_determinism.c test/harness/harness.c -ldl -lm
 *
 * Usage: ./test/tools/test_runahead_determinism <core> <rom> [--frames N]
 *                                               [--warmup N] [--json]
 *
 * Exit:  0 PASS, 1 FAIL, 2 SKIP (ROM missing)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include "../harness/harness.h"

#define DEFAULT_WARMUP 240   /* 4s — past BIOS/boot into steady state */
#define DEFAULT_WINDOW 120   /* 2s of rollback-and-replay comparison  */
#define MAX_WINDOW     600

/* FNV-1a: cheap, order-sensitive, and good enough to catch a single
 * differing sample or pixel.  We only ever compare digests against
 * digests from the same binary, so portability of the hash is moot. */
#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv1a(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* ---------- per-frame capture ---------- */

typedef struct {
    uint64_t video[MAX_WINDOW];
    uint64_t audio[MAX_WINDOW];
    /* Raw per-frame sample count and RMS, kept alongside the digests so a
     * failure can say WHAT differed, not just that something did. */
    size_t   samples[MAX_WINDOW];
    double   rms[MAX_WINDOW];
    unsigned count;
    /* Accumulates this frame's video hash between video_cb and frame_cb. */
    uint64_t pending_video;
    int      saw_video;
    /* Index into cfg->audio.frames[] already consumed, and where this
     * window started, so a window that saw no new audio entries at all
     * can be reported instead of passing vacuously. */
    unsigned audio_cursor;
    unsigned audio_start;
} capture;

static capture cap;
static unsigned window_frames = DEFAULT_WINDOW;
static int capturing = 0;

static void video_cb(void *ud, const void *data,
                     unsigned width, unsigned height, size_t pitch)
{
    unsigned y;
    const uint8_t *row = (const uint8_t *)data;

    (void)ud;
    if (!capturing)
        return;

    /* A duped frame (data == NULL) is itself a meaningful signal: it must
     * dupe in both passes at the same frame.  Fold the NULL-ness in. */
    if (!data) {
        cap.pending_video = fnv1a(cap.pending_video, "DUPE", 4);
        cap.saw_video = 1;
        return;
    }

    cap.pending_video = fnv1a(cap.pending_video, &width, sizeof(width));
    cap.pending_video = fnv1a(cap.pending_video, &height, sizeof(height));

    /* Hash the live pixels only, not the inter-row padding, so a change
     * in pitch alone doesn't masquerade as a pixel difference. */
    for (y = 0; y < height; y++)
        cap.pending_video = fnv1a(cap.pending_video,
                                  row + (size_t)y * pitch,
                                  (size_t)width * 4);
    cap.saw_video = 1;
}

/* The harness owns the audio callbacks, so digest audio from the
 * per-batch entries it appends to cfg->audio.frames[].  Each entry
 * carries that batch's sample count, peak and RMS, all computed from
 * the real samples — one differing sample moves the RMS.
 *
 * Only the entries appended since the previous frame are hashed, and
 * the entry's `frame` field is deliberately excluded: it is an absolute
 * frame counter that the harness keeps incrementing across the rollback,
 * so it legitimately differs between passes and says nothing about
 * whether the emulator diverged. */
static uint64_t audio_fingerprint(const harness_config *cfg, unsigned *cursor,
                                  size_t *samples_out, double *rms_out)
{
    uint64_t h = FNV_OFFSET;
    unsigned i;
    unsigned batches;

    *samples_out = 0;
    *rms_out     = 0;
    for (i = *cursor; i < cfg->audio.frame_count; i++) {
        const harness_audio_frame *af = &cfg->audio.frames[i];

        h = fnv1a(h, &af->samples,   sizeof(af->samples));
        h = fnv1a(h, &af->nonsilent, sizeof(af->nonsilent));
        h = fnv1a(h, &af->peak_l,    sizeof(af->peak_l));
        h = fnv1a(h, &af->peak_r,    sizeof(af->peak_r));
        h = fnv1a(h, &af->rms_l,     sizeof(af->rms_l));
        h = fnv1a(h, &af->rms_r,     sizeof(af->rms_r));
        *samples_out += af->samples;
        *rms_out      = af->rms_l;
    }
    /* Fold in how many batches arrived, so "2 batches" and "1 batch with
     * the same totals" don't collide.  This must be the COUNT, not the
     * absolute cursor: cfg->audio.frames[] keeps growing across the
     * rollback, so hashing the index would make every frame differ
     * between passes by construction and report a false divergence. */
    batches = i - *cursor;
    h = fnv1a(h, &batches, sizeof(batches));
    *cursor = cfg->audio.frame_count;
    return h;
}

/* Called by run_window after each harness_step().
 *
 * Note this is driven explicitly rather than through cfg->frame_callback:
 * harness_run() invokes that hook, but harness_step() does not, and this
 * test needs the manual stepping so it can serialize mid-run. */
static void capture_frame(harness_config *cfg)
{
    if (cap.count < MAX_WINDOW) {
        cap.video[cap.count] = cap.saw_video ? cap.pending_video : 0;
        cap.audio[cap.count] = audio_fingerprint(cfg, &cap.audio_cursor,
                                                 &cap.samples[cap.count],
                                                 &cap.rms[cap.count]);
        cap.count++;
    }
    cap.pending_video = FNV_OFFSET;
    cap.saw_video     = 0;
}

/* ---------- helpers ---------- */

static uint8_t *read_file(const char *path, size_t *len_out)
{
    FILE   *f = fopen(path, "rb");
    long    len;
    uint8_t *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len_out = (size_t)len;
    return buf;
}

/* Runs `n` frames with capture enabled, filling cap.
 *
 * The audio cursor starts at whatever the harness has already recorded,
 * not at 0: cfg->audio.frames[] accumulates across the whole run and is
 * never rewound by retro_unserialize, so pass 2 must only look at the
 * entries its own frames appended. */
static void run_window(harness_config *cfg, unsigned n)
{
    unsigned i;

    memset(&cap, 0, sizeof(cap));
    cap.pending_video = FNV_OFFSET;
    cap.audio_cursor  = cfg->audio.frame_count;
    cap.audio_start   = cfg->audio.frame_count;
    capturing = 1;
    for (i = 0; i < n; i++) {
        harness_step(cfg);
        capture_frame(cfg);
    }
    capturing = 0;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[4];
    unsigned       nres = 0;
    unsigned       warmup = DEFAULT_WARMUP;
    unsigned       i;
    int            first_video_diff = -1;
    int            first_audio_diff = -1;
    unsigned       audio_diff_count = 0;
    size_t         pass1_samples[MAX_WINDOW];
    double         pass1_rms[MAX_WINDOW];
    uint64_t       pass1_video[MAX_WINDOW];
    uint64_t       pass1_audio[MAX_WINDOW];
    uint64_t       pass2_video[MAX_WINDOW];
    uint64_t       pass2_audio[MAX_WINDOW];
    int            first_repeat_diff = -1;
    unsigned       repeat_diff_count = 0;
    unsigned       pass1_count;
    unsigned       pass1_audio_entries = 0;
    unsigned       pass2_audio_entries = 0;
    int            audio_measurable;
    char           state_a[] = "/tmp/vj_runahead_a.state";
    char           state_b[] = "/tmp/vj_runahead_b.state";
    char           state_c[] = "/tmp/vj_runahead_c.state";
    uint8_t       *blob_b = NULL, *blob_c = NULL;
    size_t         len_b = 0, len_c = 0;
    int            state_stable = 0;
    char           detail_v[256], detail_a[256], detail_s[256], detail_r[256];
    int            failed = 0;

    for (i = 1; i < (unsigned)argc; i++) {
        if (!strcmp(argv[i], "--warmup") && i + 1 < (unsigned)argc)
            warmup = (unsigned)atoi(argv[++i]);
    }

    cfg.frames = DEFAULT_WINDOW;
    if (!harness_init_from_args(&cfg, argc, argv)) {
        fprintf(stderr, "usage: %s <core> <rom> [--frames N] [--warmup N] [--json]\n",
                argv[0]);
        return 1;
    }

    window_frames = cfg.frames;
    if (window_frames > MAX_WINDOW)
        window_frames = MAX_WINDOW;

    if (!cfg.rom_path || access(cfg.rom_path, R_OK) != 0) {
        printf("SKIP: ROM not available (%s)\n",
               cfg.rom_path ? cfg.rom_path : "(none)");
        return 2;
    }

    cfg.video_callback      = video_cb;
    cfg.video_callback_data = NULL;

    if (!harness_load_core(&cfg))
        return 1;
    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 1;
    }

    /* 1. Warm up past boot. */
    for (i = 0; i < warmup; i++)
        harness_step(&cfg);

    /* 2. State A — the rollback point. */
    if (!harness_save_state(&cfg, state_a)) {
        fprintf(stderr, "FAIL: retro_serialize failed\n");
        harness_shutdown(&cfg);
        return 1;
    }

    /* 3. Pass 1. */
    run_window(&cfg, window_frames);
    memcpy(pass1_video, cap.video, sizeof(pass1_video));
    memcpy(pass1_audio, cap.audio, sizeof(pass1_audio));
    memcpy(pass1_samples, cap.samples, sizeof(pass1_samples));
    memcpy(pass1_rms, cap.rms, sizeof(pass1_rms));
    pass1_count = cap.count;
    pass1_audio_entries = cap.audio_cursor - cap.audio_start;

    /* 4. State B — where pass 1 ended up. */
    if (!harness_save_state(&cfg, state_b)) {
        fprintf(stderr, "FAIL: retro_serialize failed after pass 1\n");
        harness_shutdown(&cfg);
        return 1;
    }

    /* 5. Roll back. */
    if (!harness_load_state(&cfg, state_a)) {
        fprintf(stderr, "FAIL: retro_unserialize failed\n");
        harness_shutdown(&cfg);
        return 1;
    }

    /* 6. Pass 2 — identical input, so identical output is required. */
    run_window(&cfg, window_frames);
    pass2_audio_entries = cap.audio_cursor - cap.audio_start;

    if (getenv("VJ_RUNAHEAD_DUMP")) {
        unsigned d;
        for (d = 0; d < pass1_count && d < 8; d++)
            fprintf(stderr,
                    "  frame %2u: pass1 samples=%zu rms=%.6f | "
                    "pass2 samples=%zu rms=%.6f  %s\n",
                    d, pass1_samples[d], pass1_rms[d],
                    cap.samples[d], cap.rms[d],
                    (cap.audio[d] == pass1_audio[d]) ? "" : "<-- DIFF");
    }

    /* 7. State C. */
    harness_save_state(&cfg, state_c);

    /* 8. Pass 3 — roll back and replay a SECOND time.
     *
     * Run-ahead rolls back on every frame, not once, so the property it
     * actually depends on is that repeated rollbacks agree with each
     * other.  Pass 1 is the odd one out by construction: it is the only
     * pass reached without an intervening retro_unserialize, so anything
     * the load path doesn't restore differs there and only there.
     * Pass 2 vs pass 3 isolates that. */
    memcpy(pass2_video, cap.video, sizeof(pass2_video));
    memcpy(pass2_audio, cap.audio, sizeof(pass2_audio));

    if (!harness_load_state(&cfg, state_a)) {
        fprintf(stderr, "FAIL: retro_unserialize failed on second rollback\n");
        harness_shutdown(&cfg);
        return 1;
    }
    run_window(&cfg, window_frames);

    for (i = 0; i < pass1_count && i < cap.count; i++) {
        if (cap.video[i] != pass2_video[i] || cap.audio[i] != pass2_audio[i]) {
            if (first_repeat_diff < 0) first_repeat_diff = (int)i;
            repeat_diff_count++;
        }
    }
    /* Compare pass 1 against pass 3's digests below; cap now holds pass 3,
     * and pass 2 and pass 3 are asserted equal separately. */
    memcpy(cap.video, pass2_video, sizeof(pass2_video));
    memcpy(cap.audio, pass2_audio, sizeof(pass2_audio));

    /* ---------- compare ---------- */

    if (cap.count != pass1_count) {
        first_video_diff = 0;
    } else {
        for (i = 0; i < pass1_count; i++) {
            if (cap.video[i] != pass1_video[i] && first_video_diff < 0)
                first_video_diff = (int)i;
            if (cap.audio[i] != pass1_audio[i]) {
                if (first_audio_diff < 0) first_audio_diff = (int)i;
                audio_diff_count++;
            }
        }
    }

    blob_b = read_file(state_b, &len_b);
    blob_c = read_file(state_c, &len_c);
    state_stable = (blob_b && blob_c && len_b == len_c &&
                    memcmp(blob_b, blob_c, len_b) == 0);

    if (first_video_diff < 0)
        snprintf(detail_v, sizeof(detail_v),
                 "%u frames replayed after rollback, all video digests identical",
                 pass1_count);
    else
        snprintf(detail_v, sizeof(detail_v),
                 "video diverged at replay frame %d of %u "
                 "(pass1=%016llx pass2=%016llx)",
                 first_video_diff, pass1_count,
                 (unsigned long long)pass1_video[first_video_diff],
                 (unsigned long long)cap.video[first_video_diff]);

    /* cfg->audio.frames[] is capped at HARNESS_MAX_AUDIO_FRAMES.  Once it
     * fills, both passes hash zero new entries and compare equal — a
     * vacuous pass.  Require that both passes actually saw audio. */
    audio_measurable = (pass1_audio_entries > 0 && pass2_audio_entries > 0);

    if (!audio_measurable)
        snprintf(detail_a, sizeof(detail_a),
                 "no audio batches recorded in the compared windows "
                 "(pass1=%u pass2=%u entries; harness caps at %d, "
                 "reduce --warmup/--frames)",
                 pass1_audio_entries, pass2_audio_entries,
                 HARNESS_MAX_AUDIO_FRAMES);
    else if (first_audio_diff < 0)
        snprintf(detail_a, sizeof(detail_a),
                 "%u frames replayed after rollback, all audio digests "
                 "identical (%u audio batches compared)",
                 pass1_count, pass1_audio_entries);
    else
        snprintf(detail_a, sizeof(detail_a),
                 "audio diverged at replay frame %d of %u (%u frames differ; "
                 "first: pass1 %zu samples, pass2 %zu samples)",
                 first_audio_diff, pass1_count, audio_diff_count,
                 pass1_samples[first_audio_diff],
                 cap.samples[first_audio_diff]);

    if (state_stable)
        snprintf(detail_s, sizeof(detail_s),
                 "state after replay is byte-identical to state after "
                 "original run (%zu bytes)", len_b);
    else
        snprintf(detail_s, sizeof(detail_s),
                 "state after replay differs from state after original run "
                 "(%zu vs %zu bytes)", len_b, len_c);

    results[nres].status = (first_video_diff < 0) ? "PASS" : "FAIL";
    results[nres].name   = "video_replay_identical";
    results[nres].detail = detail_v;
    if (first_video_diff >= 0) failed = 1;
    nres++;

    results[nres].status = !audio_measurable ? "FAIL"
                         : (first_audio_diff < 0) ? "PASS" : "FAIL";
    results[nres].name   = "audio_replay_identical";
    results[nres].detail = detail_a;
    if (!audio_measurable || first_audio_diff >= 0) failed = 1;
    nres++;

    results[nres].status = state_stable ? "PASS" : "FAIL";
    results[nres].name   = "state_reconverges";
    results[nres].detail = detail_s;
    if (!state_stable) failed = 1;
    nres++;

    if (first_repeat_diff < 0)
        snprintf(detail_r, sizeof(detail_r),
                 "two successive rollbacks to the same state replayed "
                 "%u frames identically (video and audio)", pass1_count);
    else
        snprintf(detail_r, sizeof(detail_r),
                 "successive rollbacks disagree at replay frame %d "
                 "(%u frames differ)", first_repeat_diff, repeat_diff_count);

    results[nres].status = (first_repeat_diff < 0) ? "PASS" : "FAIL";
    results[nres].name   = "repeated_rollback_agrees";
    results[nres].detail = detail_r;
    if (first_repeat_diff >= 0) failed = 1;
    nres++;

    harness_report(&cfg, results, nres);

    free(blob_b);
    free(blob_c);
    remove(state_a);
    remove(state_b);
    remove(state_c);
    harness_shutdown(&cfg);

    return failed;
}
