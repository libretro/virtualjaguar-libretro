/*
 * dsp_idle_ab.c -- scratch A/B determinism harness for the DSP idle-loop
 * fast-forward (issue #569).  NOT committed.
 *
 * Emits one CSV row per frame:
 *     frame,vhash,ahash,asamples,statehash
 * where vhash is FNV-1a over the visible XRGB8888 framebuffer, ahash is
 * FNV-1a over every int16 audio sample the core emitted for that frame,
 * and statehash is FNV-1a over retro_serialize()'s blob (only on frames
 * that are a multiple of --state-every, else 0).
 *
 * Two arms are byte-identical iff the two CSVs are byte-identical.
 *
 * A trailer (to stderr) reports the deterministic counters:
 *   dsp_exec_opcode_count total + per frame, and the idle-skip
 *   fire/reject/iteration counts.
 *
 * Build:  make test/tools/dsp_idle_ab TEST_EXPORTS=1
 *
 * Run (one arm of an option sweep):
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/dsp_idle_ab \
 *      ./virtualjaguar_libretro.dylib "<rom>" --frames 3300 --warmup 300 \
 *      --state-every 100 --csv arm.csv --system-dir test/roms/private \
 *      --option virtualjaguar_risc_idle_skip=enabled
 *
 * Then `cmp` the two arms' CSVs: byte-identical means the option changed
 * nothing observable.  Always confirm the comparator can actually SEE a
 * change (perturb one arm deliberately) before trusting a clean result --
 * "we compared and found nothing" is also what a broken comparator says.
 *
 * Needs the wide test ABI (make TEST_EXPORTS=1) for the dlsym'd counters.
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FNV_OFF 1469598103934665603ULL
#define FNV_PRM 1099511628211ULL

static FILE    *g_csv;
static uint64_t g_vhash = FNV_OFF;
static uint64_t g_ahash;
static uint64_t g_asamples;
static unsigned g_warmup;
static unsigned g_state_every;

static uint32_t *p_dsp_opcodes;
static uint32_t *p_fires;
static uint32_t *p_rejects;
static uint32_t *p_iters;
static uint32_t *p_skipops;

static size_t   (*p_serialize_size)(void);
static int      (*p_serialize)(void *, size_t);
static void     *g_state_buf;
static size_t    g_state_size;

static uint64_t  g_op_base;
static uint32_t  g_fires_base, g_rej_base, g_iters_base, g_skip_base;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint64_t)b[i];
        h *= FNV_PRM;
    }
    return h;
}

static void ab_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
    uint64_t hv = FNV_OFF;
    unsigned y;
    (void)ud;
    if (!data)
        return;                     /* duped frame: keep previous hash */
    for (y = 0; y < h; y++)
        hv = fnv(hv, (const uint8_t *)data + (size_t)y * pitch,
                 (size_t)w * 4);
    hv = fnv(hv, &w, sizeof(w));
    hv = fnv(hv, &h, sizeof(h));
    g_vhash = hv;
}

static size_t ab_audio(const int16_t *data, size_t frames)
{
    if (data && frames) {
        g_ahash = fnv(g_ahash, data, frames * 2 * sizeof(int16_t));
        g_asamples += (uint64_t)frames;
    }
    return frames;
}

static bool ab_frame(void *ud, unsigned frame)
{
    uint64_t sh = 0;
    (void)ud;
    if (frame >= g_warmup) {
        if (g_state_every && ((frame - g_warmup) % g_state_every) == 0
            && p_serialize && g_state_buf) {
            memset(g_state_buf, 0, g_state_size);
            if (p_serialize(g_state_buf, g_state_size))
                sh = fnv(FNV_OFF, g_state_buf, g_state_size);
        }
        fprintf(g_csv, "%u,%016llx,%016llx,%llu,%016llx\n", frame,
                (unsigned long long)g_vhash, (unsigned long long)g_ahash,
                (unsigned long long)g_asamples, (unsigned long long)sh);
    }
    if (frame == g_warmup) {
        /* Baseline EVERY counter at the same point -- mixing a windowed
         * opcode delta with a since-boot skip total makes the derived
         * "interpreted" figure nonsense (it can even go negative). */
        if (p_dsp_opcodes) g_op_base    = *p_dsp_opcodes;
        if (p_fires)       g_fires_base = *p_fires;
        if (p_rejects)     g_rej_base   = *p_rejects;
        if (p_iters)       g_iters_base = *p_iters;
        if (p_skipops)     g_skip_base  = *p_skipops;
    }
    g_ahash    = FNV_OFF;
    g_asamples = 0;
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    const char *csv = "ab.csv";
    void (*set_batch)(size_t (*)(const int16_t *, size_t));
    int i;
    unsigned counted;

    g_warmup      = 0;
    g_state_every = 0;
    g_ahash       = FNV_OFF;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--csv") && i + 1 < argc)
            csv = argv[++i];
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc)
            g_warmup = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--state-every") && i + 1 < argc)
            g_state_every = (unsigned)strtoul(argv[++i], NULL, 10);
    }

    cfg.frames             = 3300;
    cfg.quiet              = 1;
    cfg.video_callback     = ab_video;
    cfg.frame_callback     = ab_frame;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!harness_load_rom(&cfg))
        return 1;

    g_csv = fopen(csv, "w");
    if (!g_csv) {
        fprintf(stderr, "cannot open %s\n", csv);
        return 1;
    }

    /* Take over the audio batch callback so we see raw samples. */
    set_batch = (void (*)(size_t (*)(const int16_t *, size_t)))
                harness_dlsym(&cfg, "retro_set_audio_sample_batch");
    if (set_batch)
        set_batch(ab_audio);
    else
        fprintf(stderr, "WARN: no retro_set_audio_sample_batch\n");

    p_dsp_opcodes = (uint32_t *)harness_dlsym(&cfg, "dsp_exec_opcode_count");
    p_fires       = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_fires");
    p_rejects     = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_rejects");
    p_iters       = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_iters");
    p_skipops     = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_opcodes");

    p_serialize_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
    p_serialize      = (int (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
    if (g_state_every && p_serialize_size) {
        g_state_size = p_serialize_size();
        g_state_buf  = calloc(1, g_state_size);
    }

    harness_run(&cfg);

    counted = (cfg.frames > g_warmup) ? (cfg.frames - g_warmup) : 1;
    fprintf(stderr,
            "TRAILER frames=%u warmup=%u dsp_opcodes_total=%llu "
            "dsp_opcodes_per_frame=%.1f fires=%u rejects=%u iters_skipped=%u "
            "opcodes_skipped=%u interpreted_per_frame=%.1f\n",
            cfg.frames, g_warmup,
            p_dsp_opcodes ? (unsigned long long)(*p_dsp_opcodes - g_op_base) : 0ULL,
            p_dsp_opcodes ? (double)(*p_dsp_opcodes - g_op_base) / (double)counted : 0.0,
            p_fires ? (*p_fires - g_fires_base) : 0,
            p_rejects ? (*p_rejects - g_rej_base) : 0,
            p_iters ? (*p_iters - g_iters_base) : 0,
            p_skipops ? (*p_skipops - g_skip_base) : 0,
            p_dsp_opcodes
              ? ((double)(*p_dsp_opcodes - g_op_base)
                 - (double)(p_skipops ? (*p_skipops - g_skip_base) : 0))
                / (double)counted
              : 0.0);

    fclose(g_csv);
    harness_shutdown(&cfg);
    return 0;
}
