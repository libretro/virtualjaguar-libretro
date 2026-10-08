/*
 * wmcj_intro_logo.c -- regression check for issue #736: White Men Can't
 * Jump showed only the apex of its first logo (Trimark Interactive's
 * pyramid), skipped the High Voltage logo, and lost its spinning-ball
 * title animation.
 *
 * Root cause: the game races its own GPU decoder against the blitter.
 * $17BD8 starts the GPU decoding the logo into $0DCF00 and returns
 * without waiting; the fade-in at $0138AE then makes 64 pairs of
 * SRCSHADE copies out of that buffer, each gated only on B_CMD idle.
 * On hardware every copy freezes the 68000 (lowest-priority bus master,
 * JTRM Rev 8 p.8; the blitter holds the bus until the operation
 * completes, p.69), so the fade outlasts the decode.  With zero-time
 * blits the fade finished while 111 of 200 rows were decoded and the
 * last copy kept 49 of them.  The fix is a titledb row turning on the
 * existing blitter bus-time model (virtualjaguar_blitter_timing) for
 * this title.
 *
 * The check is state-driven, not frame-numbered: run with DEFAULT
 * options (so the titledb row is what enables the model), find the first
 * picture the game holds still for PLATEAU_FRAMES consecutive frames --
 * the finished logo, in both the broken and the fixed build -- and
 * require it to cover at least MIN_LIT_PCT of the screen.  Measured on
 * 326x240: the full logo is 43.8% lit, the apex alone 6.4%.
 *
 * Exit status: 0 pass, 1 fail.
 *
 * Usage:
 *   ./test/tools/wmcj_intro_logo [core] <White Men Can't Jump.jag> [--quiet]
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/wmcj_intro_logo test/tools/wmcj_intro_logo.c \
 *      test/harness/harness.c -ldl -lm
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLATEAU_FRAMES  60u    /* the logo holds still for ~180 fields */
#define MIN_LIT_PCT     25.0
#define FRAME_BUDGET    900u   /* the logo settles by ~frame 40 */

typedef struct {
    uint64_t hash;
    double   pct;
    unsigned w, h;
    uint64_t run_hash;
    unsigned run;
    unsigned run_start;
    int      found;
    unsigned found_frame;
    double   found_pct;
} wm_state;

static void wm_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    wm_state *s = (wm_state *)ud;
    unsigned x, y, lit = 0;
    uint64_t h = 1469598103934665603ULL;

    if (!data)
        return;               /* duped frame: keep the previous reading */
    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y * pitch);
        for (x = 0; x < width; x++) {
            if (row[x] & 0x00FFFFFFu)
                lit++;
            h = (h ^ (row[x] & 0x00FFFFFFu)) * 1099511628211ULL;
        }
    }
    s->hash = h;
    s->w = width;
    s->h = height;
    s->pct = (width && height) ? 100.0 * (double)lit / ((double)width * height) : 0.0;
}

static bool wm_frame(void *ud, unsigned f)
{
    wm_state *s = (wm_state *)ud;

    if (s->pct <= 0.0) {          /* black screens between logos */
        s->run = 0;
        return true;
    }
    if (s->run && s->hash == s->run_hash) {
        s->run++;
    } else {
        s->run_hash = s->hash;
        s->run = 1;
        s->run_start = f;
    }
    if (s->run >= PLATEAU_FRAMES) {
        s->found = 1;
        s->found_frame = s->run_start;
        s->found_pct = s->pct;
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    static wm_state s;
    char detail[384];
    harness_result res;
    int pass;

    cfg.frames = FRAME_BUDGET;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.video_callback      = wm_video;
    cfg.video_callback_data = &s;
    cfg.frame_callback      = wm_frame;
    cfg.frame_callback_data = &s;
    if (!harness_load_rom(&cfg))
        return 1;
    harness_run(&cfg);
    harness_shutdown(&cfg);

    pass = s.found && s.found_pct >= MIN_LIT_PCT;
    if (!s.found)
        snprintf(detail, sizeof detail,
                 "no lit picture held for %u frames within %u frames", PLATEAU_FRAMES,
                 FRAME_BUDGET);
    else
        snprintf(detail, sizeof detail,
                 "first held picture at frame %u is %.1f%% lit %ux%u (need >= %.0f%%)%s",
                 s.found_frame, s.found_pct, s.w, s.h, MIN_LIT_PCT,
                 pass ? "" : " -- logo truncated: zero-time blits outran the GPU "
                             "decoder (#736: titledb blitter_timing row missing?)");
    res.status = pass ? "PASS" : "FAIL";
    res.name   = "wmcj_intro_logo";
    res.detail = detail;
    harness_report(&cfg, &res, 1);
    return pass ? 0 : 1;
}
