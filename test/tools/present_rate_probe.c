/* test/tools/present_rate_probe.c -- measure a title's real present
 * rate: how often the OP object-list pointer (OLP, TOM $F00020) or its
 * pointed-to list content changes, i.e. how often the game flips its
 * display.  Doom's MiniLoop flips once per pass (I_Update), so
 * flips/second == passes/second regardless of how gametic arithmetic
 * maps passes to tics.  Compare off vs. timing-model options vs. the
 * hardware reference rate.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "../harness/harness.h"

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
    if (frame && (frame % 300) == 0)
    {
        printf("w%-3u flips=%-4u flips/s=%.2f fields/flip=%.2f\n",
               ++window_no, flips_window, flips_window / 5.006,
               flips_window ? 300.0 / flips_window : 0.0);
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
