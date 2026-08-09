/*
 * hires_state_digest.c -- Stage 1 savestate-digest identity gate for the
 * hi-res (internal resolution) option.  Design section 7.6: frame hashes
 * cannot match between a 1x and an Nx run (the dimensions differ), so the
 * ON-gate is stronger and different -- savestate digests at frames
 * 300/600/900 must be byte-identical between ON and OFF runs, proving the
 * enhancement is invisible to the emulated machine.
 *
 * Prints one FNV-1a digest line per checkpoint frame; compare the output
 * of two runs (diff'able):
 *
 *   ./test/tools/hires_state_digest core rom --frames 900 > off.txt
 *   ./test/tools/hires_state_digest core rom --frames 900 \
 *       --option virtualjaguar_internal_resolution=2x > on.txt
 *   diff off.txt on.txt
 *
 * Checkpoints are every 300 frames up to --frames.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/hires_state_digest test/tools/hires_state_digest.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Honors VJ_EXPECT_BUILD (build-identity guard, see scripts/build-id.sh).
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef size_t (*serialize_size_fn)(void);
typedef bool (*serialize_fn)(void *, size_t);

typedef struct {
    harness_config *cfg;
    serialize_size_fn ssize;
    serialize_fn      ser;
    unsigned          failures;
    unsigned          digests;
} sd_state;

static bool sd_frame(void *ud, unsigned frame)
{
    sd_state *st = (sd_state *)ud;
    size_t sz;
    uint8_t *buf;
    uint64_t h;
    size_t i;

    if (frame == 0 || (frame % 300) != 0)
        return true;

    sz  = st->ssize();
    if (sz == 0) {
        /* Serialization unavailable: a digest of nothing would compare
         * equal between arms and report a false PASS.  Fail loudly. */
        fprintf(stderr, "frame %u: retro_serialize_size() == 0\n", frame);
        st->failures++;
        return true;
    }
    buf = (uint8_t *)malloc(sz);
    if (!buf || !st->ser(buf, sz)) {
        fprintf(stderr, "frame %u: retro_serialize failed\n", frame);
        st->failures++;
        free(buf);
        return true;
    }
    h = 1469598103934665603ULL;
    for (i = 0; i < sz; i++) {
        h ^= (uint64_t)buf[i];
        h *= 1099511628211ULL;
    }
    printf("STATE_DIGEST frame=%u size=%zu fnv=%016llx\n",
           frame, sz, (unsigned long long)h);
    fflush(stdout);
    st->digests++;
    free(buf);
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    sd_state st;

    memset(&st, 0, sizeof(st));
    cfg.frames = 900;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "hires_state_digest: no ROM/disc given\n");
        return 1;
    }

    st.cfg   = &cfg;
    cfg.frame_callback      = sd_frame;
    cfg.frame_callback_data = &st;

    if (!harness_load_rom(&cfg))
        return 1;

    st.ssize = (serialize_size_fn)harness_dlsym(&cfg, "retro_serialize_size");
    st.ser   = (serialize_fn)harness_dlsym(&cfg, "retro_serialize");
    if (!st.ssize || !st.ser)
        return 1;

    harness_run(&cfg);
    harness_shutdown(&cfg);

    if (st.digests == 0 || st.failures) {
        fprintf(stderr, "hires_state_digest: %u digests, %u failures\n",
                st.digests, st.failures);
        return 1;
    }
    return 0;
}
