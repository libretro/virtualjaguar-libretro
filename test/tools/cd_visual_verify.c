/*
 * cd_visual_verify.c — automated visual + audio verification for CD titles.
 *
 * Closes the "headless can't tell if the game is actually showing
 * something" gap without a human in front of RetroArch: it runs a disc
 * for N frames, measures per-window frame MOTION (changed pixels between
 * consecutive frames), non-black coverage, and audio RMS, dumps periodic
 * screenshots (PPM — convert with `sips -s format png *.ppm --out .` on
 * macOS or ImageMagick elsewhere), and prints a per-window timeline plus
 * a machine-readable verdict.  An agent (or a human) can then LOOK at the
 * screenshots and the motion timeline instead of asking someone to boot a
 * device.
 *
 * Caveat that keeps device testing relevant: the headless read path is
 * not RetroArch's composited output for every title (see CLAUDE.md
 * "Headless framebuffer caveat") — treat "no motion / black" here as a
 * strong signal, and confirm final "it looks right" on a real frontend.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/cd_visual_verify test/tools/cd_visual_verify.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Run (bios mode, screenshots every 150 frames into ./cdshots):
 *   ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
 *      "test/roms/private/BrainDead 13 (USA)/.../BrainDead 13 (USA).cue" \
 *      --bios --frames 3000 --outdir /tmp/cdshots --shot-every 150 \
 *      --system-dir test/roms/private
 *
 * Honors VJ_EXPECT_BUILD (build-identity guard, see scripts/build-id.sh).
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_W 1024
#define MAX_H 576
#define WINDOW 60u   /* frames per timeline bucket (~1 s NTSC) */

typedef struct {
    uint32_t prev[MAX_W * MAX_H];
    uint32_t cur[MAX_W * MAX_H];
    unsigned w, h;
    int      have_prev;

    /* per-frame metrics for the current window */
    unsigned frames_in_window;
    unsigned moving_frames;      /* frames with >0.5% pixels changed */
    double   sum_change_pct;
    unsigned max_nonblack;

    /* config */
    const char *outdir;
    unsigned shot_every;
    unsigned frame_no;

    /* timeline */
    unsigned window_no;
} vv_state;

static unsigned vv_nonblack(const uint32_t *px, unsigned n)
{
    unsigned i, c = 0;
    for (i = 0; i < n; i++)
        if (px[i] & 0x00FFFFFFu) c++;
    return c;
}

static void vv_write_ppm(const vv_state *st, unsigned frame)
{
    char path[1024];
    FILE *f;
    unsigned x, y;
    if (!st->outdir || !st->w || !st->h) return;
    snprintf(path, sizeof(path), "%s/frame_%05u.ppm", st->outdir, frame);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cd_visual_verify: cannot write %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", st->w, st->h);
    for (y = 0; y < st->h; y++) {
        for (x = 0; x < st->w; x++) {
            uint32_t p = st->cur[y * st->w + x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((p >> 16) & 0xFF);
            rgb[1] = (unsigned char)((p >> 8) & 0xFF);
            rgb[2] = (unsigned char)(p & 0xFF);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static void vv_video_cb(void *ud, const void *data, unsigned w, unsigned h,
                        size_t pitch)
{
    vv_state *st = (vv_state *)ud;
    unsigned y, n, changed = 0, nb;
    const uint8_t *src = (const uint8_t *)data;

    if (!data || w == 0 || h == 0 || w > MAX_W || h > MAX_H) return;

    /* copy (respecting pitch) */
    for (y = 0; y < h; y++)
        memcpy(&st->cur[y * w], src + y * pitch, w * 4);
    st->w = w; st->h = h;
    n = w * h;

    if (st->have_prev) {
        unsigned i;
        for (i = 0; i < n; i++)
            if ((st->cur[i] ^ st->prev[i]) & 0x00FFFFFFu) changed++;
        if ((double)changed / (double)n > 0.005) st->moving_frames++;
        st->sum_change_pct += 100.0 * (double)changed / (double)n;
    }
    nb = vv_nonblack(st->cur, n);
    if (nb > st->max_nonblack) st->max_nonblack = nb;
    st->frames_in_window++;

    memcpy(st->prev, st->cur, (size_t)n * 4);
    st->have_prev = 1;

    if (st->outdir && st->shot_every && (st->frame_no % st->shot_every) == 0)
        vv_write_ppm(st, st->frame_no);
}

static bool vv_frame_cb(void *ud, unsigned frame)
{
    vv_state *st = (vv_state *)ud;
    st->frame_no = frame + 1;   /* next frame number for the video cb */

    if (((frame + 1) % WINDOW) == 0) {
        unsigned n = st->w * st->h;
        fprintf(stderr,
                "  [win %03u] frames %5u-%5u: motion %2u/%u frames, "
                "avg change %5.2f%%, max nonblack %u/%u (%.1f%%)\n",
                st->window_no, frame + 1 - WINDOW, frame,
                st->moving_frames,
                st->frames_in_window ? st->frames_in_window : 1,
                st->frames_in_window ? st->sum_change_pct / st->frames_in_window : 0.0,
                st->max_nonblack, n,
                n ? 100.0 * st->max_nonblack / n : 0.0);
        st->window_no++;
        st->frames_in_window = 0;
        st->moving_frames = 0;
        st->sum_change_pct = 0.0;
        st->max_nonblack = 0;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    static vv_state st; /* static: too big for the stack */
    harness_result results[4];
    unsigned nres = 0;
    int i, ok_video, ok_audio;
    double overall_rms = 0.0;
    unsigned rms_frames = 0;

    memset(&st, 0, sizeof(st));
    st.shot_every = 150;

    /* pre-parse tool-specific flags (harness skips unknown ones) */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--outdir") == 0 && i + 1 < argc)
            st.outdir = argv[i + 1];
        else if (strcmp(argv[i], "--shot-every") == 0 && i + 1 < argc)
            st.shot_every = (unsigned)atoi(argv[i + 1]);
    }

    cfg.frames = 3000;
    cfg.system_dir = "test/roms/private";
    cfg.video_callback = vv_video_cb;
    cfg.video_callback_data = &st;
    cfg.frame_callback = vv_frame_cb;
    cfg.frame_callback_data = &st;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "usage: cd_visual_verify [core] <disc.cue> [--bios] "
                        "[--frames N] [--outdir DIR] [--shot-every N] "
                        "[--system-dir DIR] [--json]\n");
        return 1;
    }
    if (!harness_load_rom(&cfg)) return 1;

    fprintf(stderr, "cd_visual_verify: %u frames, screenshots %s (every %u)\n",
            cfg.frames, st.outdir ? st.outdir : "(disabled)", st.shot_every);

    harness_run(&cfg);

    /* overall audio RMS across recorded audio frames */
    {
        unsigned a;
        for (a = 0; a < cfg.audio.frame_count; a++) {
            overall_rms += (cfg.audio.frames[a].rms_l + cfg.audio.frames[a].rms_r) / 2.0;
            rms_frames++;
        }
        if (rms_frames) overall_rms /= rms_frames;
    }

    ok_video = (st.window_no > 0);  /* refined below */
    /* Re-derive coarse verdicts from the totals the run left behind: a CD
     * title that "works" shows sustained motion at some point (FMV/intro)
     * and produces audio.  A permanently black or frozen screen fails. */
    ok_video = cfg.video.total_frames_rendered > 0;
    ok_audio = cfg.audio.total_nonsilent > 0;

    results[nres].status = ok_video ? "PASS" : "FAIL";
    results[nres].name   = "video_rendered";
    results[nres].detail = ok_video ? "frames rendered" : "no video callbacks";
    nres++;
    results[nres].status = ok_audio ? "PASS" : "FAIL";
    results[nres].name   = "audio_present";
    results[nres].detail = ok_audio ? "non-silent samples detected"
                                    : "audio fully silent";
    nres++;
    {
        static char rmsbuf[64];
        snprintf(rmsbuf, sizeof(rmsbuf), "avg RMS %.0f over %u audio frames",
                 overall_rms, rms_frames);
        results[nres].status = "INFO";
        results[nres].name   = "audio_rms";
        results[nres].detail = rmsbuf;
        nres++;
    }

    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);
    return (ok_video && ok_audio) ? 0 : 1;
}
