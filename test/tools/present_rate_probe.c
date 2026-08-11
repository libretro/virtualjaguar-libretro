/* test/tools/present_rate_probe.c -- measure a title's real present
 * rate: how often the image the host is shown actually changes.
 *
 * Method: FNV hash of a subsample of every presented frame (via the
 * harness video callback).  A changed hash == a new image was
 * presented this field.  This measures what the player sees, so it is
 * independent of how a title's internal counters map to passes --
 * Doom's MiniLoop presents once per pass (I_Update), so flips/field is
 * its loop rate directly.
 *
 * Reports fields-per-flip, which is the unit that matters and is
 * frame-rate-agnostic (NTSC and PAL alike).  Doom's demo measures
 * 2.00 fields/flip in this core vs ~4 on hardware (#401).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "../harness/harness.h"

#define WINDOW 300u

static uint64_t last_hash;
static unsigned flips_window, window_no, cur_frame;

/* FNV over the presented frame: a changed hash == a new image was
 * presented this field.  Doom presents once per MiniLoop pass. */
static void on_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t hv = 1469598103934665603ull;
    unsigned y, x;
    (void)ud;
    if (!p) return;
    for (y = 0; y < h; y += 8)
    {
        const uint8_t *row = p + y * pitch;
        for (x = 0; x < w * 4; x += 16)
        {
            hv ^= row[x];
            hv *= 1099511628211ull;
        }
    }
    if (hv != last_hash)
    {
        last_hash = hv;
        flips_window++;
    }
}

static bool on_frame(void *ud, unsigned frame)
{
    (void)ud;
    cur_frame = frame;
    if (frame && (frame % WINDOW) == 0)
    {
        /* No seconds here on purpose: a hard-coded window length is
         * wrong for PAL and only approximately right for NTSC.
         * fields/flip is exact and directly comparable across both. */
        printf("w%-3u flips=%-4u of %u fields  fields/flip=%.2f\n",
               ++window_no, flips_window, WINDOW,
               flips_window ? (double)WINDOW / flips_window : 0.0);
        flips_window = 0;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    cfg.frames = 1500;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;
    cfg.video_callback = on_video;
    harness_run(&cfg);
    harness_shutdown(&cfg);
    return 0;
}
