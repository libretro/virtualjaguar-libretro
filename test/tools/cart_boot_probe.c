/*
 * test/tools/cart_boot_probe.c — one-ROM cartridge boot probe for the
 * cartridge compatibility matrix (test/tools/cart_boot_matrix.sh).
 *
 * Runs a cartridge headlessly for N frames and emits one machine-parseable
 * line the sweep script classifies:
 *
 *   CARTPROBE rom="<path>" frames=<rendered> w=<W> h=<H> pc=$<FINALPC> \
 *       nonblack_max_pct=<peak %% of any frame lit> lit_frames=<frames >1%% lit> \
 *       motion=<distinct sampled frame hashes> \
 *       audio_nonsilent=<samples> audio_onset=<frame|-1>
 *
 * Deliberately does NOT classify.  The stage taxonomy, the crash-watchdog
 * signature greps, and the honesty rules ("boots headlessly" is not
 * "completed the game") live in one place: the sweep script.  Watchdog
 * signatures reach the script through this probe's stderr because the
 * harness forwards core WARN/ERR log lines by default.
 *
 * Needs the wide test ABI (make TEST_EXPORTS=1) for m68k_get_reg.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *      -o test/tools/cart_boot_probe test/tools/cart_boot_probe.c \
 *      test/harness/harness.c -ldl -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

/* src/m68000/m68kinterface.h: D0-D7, A0-A7, then PC.  The enum is part of
 * the UAE core's stable interface; 16 is M68K_REG_PC.  (Same pattern as
 * test/tools/cd_wedge_probe.c.) */
#define PROBE_M68K_REG_PC 16

typedef struct {
    unsigned last_nonblack;      /* non-black pixels in most recent frame */
    unsigned max_nonblack;       /* peak across the run — a title mid-fade
                                    on the final frame still counts */
    unsigned lit_frames;         /* frames with >~1%% non-black pixels */
    unsigned last_w, last_h;
    unsigned distinct_hashes;    /* frames whose sampled hash differed from
                                    the previous frame's — motion evidence */
    uint32_t prev_hash;
    int      have_prev;
} probe_video_state;

static void probe_video_cb(void *userdata, const void *data,
                           unsigned width, unsigned height, size_t pitch)
{
    probe_video_state *st = (probe_video_state *)userdata;
    const uint8_t *rows;
    uint32_t hash;
    unsigned nonblack, x, y, step;

    if (!data || !width || !height)
        return;

    rows = (const uint8_t *)data;
    hash = 2166136261u;
    nonblack = 0;
    /* Sample every 4th pixel of every 4th row: cheap, and plenty to tell
     * "black screen" from "rendering" and frame A from frame B. */
    step = 4;
    for (y = 0; y < height; y += step) {
        const uint32_t *px = (const uint32_t *)(rows + y * pitch);
        for (x = 0; x < width; x += step) {
            uint32_t v = px[x] & 0x00FFFFFFu;
            if (v != 0)
                nonblack++;
            hash ^= v;
            hash *= 16777619u;
        }
    }
    /* Scale the sampled count back up to an approximate full-frame count. */
    st->last_nonblack = nonblack * step * step;
    if (st->last_nonblack > st->max_nonblack)
        st->max_nonblack = st->last_nonblack;
    if (st->last_nonblack * 100u > width * height)
        st->lit_frames++;
    st->last_w = width;
    st->last_h = height;
    if (st->have_prev && hash != st->prev_hash)
        st->distinct_hashes++;
    st->prev_hash = hash;
    st->have_prev = 1;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    probe_video_state vstate;
    unsigned int (*get_reg)(void *, int) = NULL;
    unsigned long final_pc = 0;
    unsigned total_px;
    double nonblack_pct = 0.0;
    harness_result res;

    memset(&vstate, 0, sizeof(vstate));

    cfg.frames = 600;
    cfg.quiet = 1;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 2;
    cfg.video_callback = probe_video_cb;
    cfg.video_callback_data = &vstate;

    if (!harness_load_rom(&cfg)) {
        printf("CARTPROBE rom=\"%s\" load_fail=1\n",
               cfg.rom_path ? cfg.rom_path : "?");
        harness_shutdown(&cfg);
        return 3;
    }

    harness_run(&cfg);

    get_reg = (unsigned int (*)(void *, int))
        harness_dlsym(&cfg, "m68k_get_reg");
    if (get_reg)
        final_pc = get_reg(NULL, PROBE_M68K_REG_PC) & 0xFFFFFFu;

    total_px = vstate.last_w * vstate.last_h;
    if (total_px)
        nonblack_pct = 100.0 * (double)vstate.max_nonblack / (double)total_px;
    if (nonblack_pct > 100.0)
        nonblack_pct = 100.0;

    printf("CARTPROBE rom=\"%s\" frames=%u w=%u h=%u pc=$%06lX "
           "nonblack_max_pct=%.1f lit_frames=%u motion=%u "
           "audio_nonsilent=%u audio_onset=%d\n",
           cfg.rom_path, cfg.video.total_frames_rendered,
           vstate.last_w, vstate.last_h, final_pc,
           nonblack_pct, vstate.lit_frames, vstate.distinct_hashes,
           cfg.audio.total_nonsilent, cfg.audio.first_audio_frame);

    res.status = "INFO";
    res.name = "cart_boot_probe";
    res.detail = "see CARTPROBE line";
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return 0;
}
