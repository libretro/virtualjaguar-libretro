/* Synthetic pictures must not disappear when shifted off a sampling grid. */
#define main cart_probe_main
#include "cart_boot_probe.c"
#undef main
#include <assert.h>

int main(void)
{
    uint32_t pixels[8][12];
    probe_video_pair pair;
    unsigned axis, phase, x, y;

    for (axis = 0; axis < 2; axis++)
    {
        for (phase = 0; phase < 4; phase++)
        {
            memset(&pair, 0, sizeof(pair));
            /* Bright row padding must not count as part of the picture. */
            memset(pixels, 0xFF, sizeof(pixels));
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                    pixels[y][x] = ((axis ? y : x) % 4 == phase)
                        ? 0xFF123456u : 0xFF000000u;
            probe_video_cb(&pair, pixels, 8, 8, sizeof(pixels[0]));
            assert(pair.whole.last_nonblack == 16);
            assert(pair.whole.lit_frames == 1);
            assert(pair.scored.last_nonblack == 16);
            /* A colour change at an otherwise unsampled pixel is motion. */
            pixels[1][1] = 0xFFABCDEFu;
            probe_video_cb(&pair, pixels, 8, 8, sizeof(pixels[0]));
            assert(pair.whole.distinct_hashes == 1);
        }
    }
    memset(&pair, 0, sizeof(pair));
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            pixels[y][x] = 0xFF000000u;
    probe_video_cb(&pair, pixels, 8, 8, sizeof(pixels[0]));
    assert(pair.whole.last_nonblack == 0);
    assert(pair.whole.lit_frames == 0);
    puts("cart probe video: stripe phases, pitch, motion and black passed");
    return 0;
}
