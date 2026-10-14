/*
 * frame_hash_ab.c -- per-frame framebuffer-hash CSV emitter for A/B runs.
 *
 * Answers "did this option change when things happen?" without dumping
 * screenshots.  Emits one CSV row per frame:
 *
 *     frame,w,h,duped,nonblack,hash
 *
 * where `hash` is FNV-1a over the whole XRGB8888 framebuffer.  Two arms are
 * then compared by WHEN each distinct hash first appears, which measures
 * pace rather than pixel equality:
 *
 *   - render rate: how many distinct hashes an arm produced in the window
 *     (fewer = the GPU finished fewer renders);
 *   - pace lag: for a state both arms reach, how many frames later the
 *     slower arm reaches it.  A growing lag means the scene is paced
 *     slower, which is what a clock/timing change should look like.
 *
 * Always run the SAME config twice first and diff the CSVs.  If the two
 * baseline runs are not identical, cross-arm divergence is noise and the
 * comparison is uninterpretable -- this matters from a loaded CD state,
 * where DSA delay, FIFO drain pacing and I2S timing are all live.
 *
 * --audio-hash appends an `ahash` column: FNV-1a over the int16 stereo
 * samples the core delivered during that frame (an empty frame hashes to
 * the FNV offset basis).  It replaces the harness's audio callbacks, so
 * the AUDIO summary line then reads zero; corpus_ab_sweep.sh AUDIO=1
 * uses it to A/B the audio stream alongside the video.
 *
 * Duped frames (core passed a NULL framebuffer) repeat the previous hash,
 * which is correct for state-arrival timing; the `duped` column is there so
 * a drop in distinct-state count can be attributed to lost renders rather
 * than to frame duplication.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/frame_hash_ab test/tools/frame_hash_ab.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Run (one arm of a RISC clock sweep, starting from a save state):
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/frame_hash_ab \
 *      ./virtualjaguar_libretro.dylib "<disc.cue>" \
 *      --load-state state.raw --frames 1800 \
 *      --option virtualjaguar_risc_clock_scale=0.5x \
 *      --csv r05x.csv --system-dir test/roms/private
 *
 * Worked example and the analysis it feeds:
 * docs/battle-morph-pace-calibration.md.
 *
 * Honors VJ_EXPECT_BUILD (build-identity guard, see scripts/build-id.sh).
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE    *csv;
    uint64_t hash;
    unsigned nonblack;
    unsigned w, h;
    int      duped;
    int      audio_hash;   /* --audio-hash given */
    uint64_t ahash;        /* FNV-1a of this frame's audio so far */
} ab_state;

static ab_state *g_ab;     /* audio callbacks carry no user data */

static size_t ab_audio_batch(const int16_t *data, size_t frames)
{
    const uint8_t *b = (const uint8_t *)data;
    size_t i, n = frames * 4;

    for (i = 0; i < n; i++) {
        g_ab->ahash ^= (uint64_t)b[i];
        g_ab->ahash *= 1099511628211ULL;
    }
    return frames;
}

static void ab_audio_sample(int16_t l, int16_t r)
{
    int16_t p[2];
    p[0] = l; p[1] = r;
    ab_audio_batch(p, 1);
}

/* FNV-1a over the visible framebuffer, row by row (pitch may exceed w*4). */
static void ab_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    ab_state *st = (ab_state *)ud;
    const uint8_t *base = (const uint8_t *)data;
    uint64_t h = 1469598103934665603ULL;
    unsigned nonblack = 0;
    unsigned y, x, k;

    if (!data) {
        st->duped = 1;          /* keep previous hash / nonblack */
        return;
    }

    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)(base + (size_t)y * pitch);
        for (x = 0; x < width; x++) {
            const uint8_t *b = (const uint8_t *)&row[x];
            for (k = 0; k < 4; k++) {
                h ^= (uint64_t)b[k];
                h *= 1099511628211ULL;
            }
            if (row[x] & 0x00FFFFFFu)
                nonblack++;
        }
    }

    st->hash     = h;
    st->nonblack = nonblack;
    st->w        = width;
    st->h        = height;
    st->duped    = 0;
}

static bool ab_frame(void *ud, unsigned frame)
{
    ab_state *st = (ab_state *)ud;
    fprintf(st->csv, "%u,%u,%u,%d,%u,%016llx",
            frame, st->w, st->h, st->duped, st->nonblack,
            (unsigned long long)st->hash);
    if (st->audio_hash) {
        fprintf(st->csv, ",%016llx", (unsigned long long)st->ahash);
        st->ahash = 1469598103934665603ULL;
    }
    fputc('\n', st->csv);
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    ab_state st;
    const char *csv_path = NULL;
    int i;

    memset(&st, 0, sizeof(st));

    /* Pre-parse our own flag.  harness_init_from_args() skips unknown
     * flags, but its VALUE would fall through to positional handling, so
     * neutralise both tokens rather than leaving them in argv. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[i + 1];
            argv[i] = argv[i + 1] = (char *)"--quiet";
        } else if (strcmp(argv[i], "--audio-hash") == 0) {
            st.audio_hash = 1;
            argv[i] = (char *)"--quiet";
        }
    }

    if (!csv_path) {
        fprintf(stderr, "usage: frame_hash_ab [core] <rom> --csv OUT "
                        "[--load-state S] [--frames N] [--option K=V] "
                        "[--bios] [--system-dir DIR] [--audio-hash]\n");
        return 1;
    }

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "frame_hash_ab: no ROM/disc given\n");
        return 1;
    }

    st.csv = fopen(csv_path, "w");
    if (!st.csv) {
        fprintf(stderr, "frame_hash_ab: cannot write '%s'\n", csv_path);
        return 1;
    }
    fprintf(st.csv, "frame,w,h,duped,nonblack,hash%s\n",
            st.audio_hash ? ",ahash" : "");
    st.ahash = 1469598103934665603ULL;
    g_ab = &st;

    cfg.video_callback      = ab_video;
    cfg.video_callback_data = &st;
    cfg.frame_callback      = ab_frame;
    cfg.frame_callback_data = &st;

    /* harness_load_rom() also applies --load-state and resets audio stats. */
    if (!harness_load_rom(&cfg)) {
        fclose(st.csv);
        return 1;
    }
    if (st.audio_hash) {
        void (*set_batch)(size_t (*)(const int16_t *, size_t)) =
            (void (*)(size_t (*)(const int16_t *, size_t)))
            harness_dlsym(&cfg, "retro_set_audio_sample_batch");
        void (*set_sample)(void (*)(int16_t, int16_t)) =
            (void (*)(void (*)(int16_t, int16_t)))
            harness_dlsym(&cfg, "retro_set_audio_sample");
        if (!set_batch) {
            fprintf(stderr, "frame_hash_ab: no retro_set_audio_sample_batch\n");
            fclose(st.csv);
            return 1;
        }
        set_batch(ab_audio_batch);
        if (set_sample)
            set_sample(ab_audio_sample);
    }

    harness_run(&cfg);
    fclose(st.csv);

    /* Audio totals are a free contract check: risc_clock_scale documents
     * that sample pacing stays at stock, so total_samples / batch_calls
     * must not move across arms. */
    printf("AUDIO total_samples=%llu nonsilent=%u first_audio_frame=%d "
           "batch_calls=%u\n",
           (unsigned long long)cfg.audio.total_samples,
           cfg.audio.total_nonsilent,
           cfg.audio.first_audio_frame,
           cfg.audio.total_batch_calls);
    printf("VIDEO frames=%u last=%ux%u dim_changes=%u\n",
           cfg.video.total_frames_rendered, cfg.video.last_width,
           cfg.video.last_height, cfg.video.dimension_changes);

    harness_shutdown(&cfg);
    return 0;
}
