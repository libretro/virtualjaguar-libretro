/*
 * hires_shot.c -- hi-res Stage 2 evidence tool.
 *
 * Runs a title headlessly (scripted input via the shared harness's
 * --press), dumps requested frames as PPM screenshots, and quantifies
 * the sub-pixel variance of each dumped frame: the percentage of 2x2
 * blocks whose four subpixels are NOT all equal.  At 1x (or on a
 * Stage 1 / non-beneficiary title at 2x) that percentage is 0.000; on a
 * Stage 2 beneficiary at 2x it is nonzero exactly where qualifying
 * fractional-source-walk blits landed -- the box-replication property
 * failing there is the feature (see docs/hires-upscaling-design.md,
 * section 8 Stage 2).
 *
 * Usage:
 *   ./test/tools/hires_shot [core] <rom> [--frames N] [--option K=V]
 *       [--press F:BTN[:HOLD]]... [--shot F]... [--shot-every N]
 *       [--out-prefix PATH]
 *
 * Emits "SHOT frame=... w=... h=... blocks=... nonuniform=... pct=..."
 * per dumped frame and a trailing "VARIANCE total_blocks=... pct=..."
 * summary over all dumped frames.  PPMs are written when --out-prefix
 * is given (PATH_f%05u.ppm).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/hires_shot test/tools/hires_shot.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Honors VJ_EXPECT_BUILD (build-identity guard, see scripts/build-id.sh).
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HS_MAX_SHOTS 512

typedef struct {
    unsigned  shots[HS_MAX_SHOTS];
    unsigned  nshots;
    unsigned  every;
    const char *prefix;
    unsigned  frame;

    uint32_t *fb;
    unsigned  fb_w, fb_h;

    unsigned long long tot_blocks;
    unsigned long long tot_nonuniform;
    unsigned  dumped;
} hs_state;

static int hs_wanted(hs_state *st, unsigned frame)
{
    unsigned i;
    if (st->every && frame && (frame % st->every) == 0)
        return 1;
    for (i = 0; i < st->nshots; i++)
        if (st->shots[i] == frame)
            return 1;
    return 0;
}

static void hs_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    hs_state *st = (hs_state *)ud;
    const uint8_t *base = (const uint8_t *)data;
    unsigned y, x;

    if (!data)
        return;
    if (!st->fb || st->fb_w != width || st->fb_h != height) {
        free(st->fb);
        st->fb = (uint32_t *)malloc((size_t)width * height * 4);
        st->fb_w = width;
        st->fb_h = height;
    }
    if (!st->fb)
        return;
    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)(base + y * pitch);
        for (x = 0; x < width; x++)
            st->fb[(size_t)y * width + x] = row[x] & 0x00FFFFFFu;
    }
}

static void hs_dump(hs_state *st, unsigned frame)
{
    unsigned w = st->fb_w, h = st->fb_h;
    unsigned long long blocks = 0, nonuni = 0;
    unsigned y, x;

    if (!st->fb)
        return;

    /* 2x2 block uniformity (only meaningful when dims are even). */
    if (w >= 2 && h >= 2) {
        for (y = 0; y + 1 < h; y += 2) {
            const uint32_t *r0 = st->fb + (size_t)y * w;
            const uint32_t *r1 = r0 + w;
            for (x = 0; x + 1 < w; x += 2) {
                uint32_t p = r0[x];
                blocks++;
                if (r0[x + 1] != p || r1[x] != p || r1[x + 1] != p)
                    nonuni++;
            }
        }
    }
    st->tot_blocks     += blocks;
    st->tot_nonuniform += nonuni;
    st->dumped++;

    printf("SHOT frame=%u w=%u h=%u blocks=%llu nonuniform=%llu pct=%.4f\n",
           frame, w, h, blocks, nonuni,
           blocks ? 100.0 * (double)nonuni / (double)blocks : 0.0);

    if (st->prefix) {
        char path[1024];
        FILE *f;
        snprintf(path, sizeof(path), "%s_f%05u.ppm", st->prefix, frame);
        f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    uint32_t p = st->fb[(size_t)y * w + x];
                    uint8_t rgb[3];
                    rgb[0] = (uint8_t)(p >> 16);
                    rgb[1] = (uint8_t)(p >> 8);
                    rgb[2] = (uint8_t)p;
                    fwrite(rgb, 1, 3, f);
                }
            }
            fclose(f);
        } else {
            fprintf(stderr, "hires_shot: cannot write '%s'\n", path);
        }
    }
}

static bool hs_frame(void *ud, unsigned frame)
{
    hs_state *st = (hs_state *)ud;
    st->frame = frame;
    if (hs_wanted(st, frame))
        hs_dump(st, frame);
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    hs_state st;
    int i;

    memset(&st, 0, sizeof(st));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            if (st.nshots < HS_MAX_SHOTS)
                st.shots[st.nshots++] = (unsigned)strtoul(argv[i + 1], NULL, 0);
            argv[i] = argv[i + 1] = (char *)"--quiet";
        } else if (strcmp(argv[i], "--shot-every") == 0 && i + 1 < argc) {
            st.every = (unsigned)strtoul(argv[i + 1], NULL, 0);
            argv[i] = argv[i + 1] = (char *)"--quiet";
        } else if (strcmp(argv[i], "--out-prefix") == 0 && i + 1 < argc) {
            st.prefix = argv[i + 1];
            argv[i] = argv[i + 1] = (char *)"--quiet";
        }
    }

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "hires_shot: no ROM given\n");
        return 1;
    }

    cfg.video_callback      = hs_video;
    cfg.video_callback_data = &st;
    cfg.frame_callback      = hs_frame;
    cfg.frame_callback_data = &st;

    if (!harness_load_rom(&cfg))
        return 1;

    harness_run(&cfg);

    printf("VARIANCE shots=%u total_blocks=%llu nonuniform=%llu pct=%.4f\n",
           st.dumped, st.tot_blocks, st.tot_nonuniform,
           st.tot_blocks
               ? 100.0 * (double)st.tot_nonuniform / (double)st.tot_blocks
               : 0.0);

    free(st.fb);
    harness_shutdown(&cfg);
    return 0;
}
