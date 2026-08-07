/*
 * hires_box_check.c -- Stage 1 ON-inertness gate for the hi-res
 * (internal resolution) option.  See docs/hires-upscaling-design.md
 * section 7.6: with everything nearest-neighbour, the Nx frame must be
 * EXACTLY an Nx box replication of the 1x frame, assertable mechanically.
 *
 * Two-step protocol (runs are deterministic, so two processes suffice):
 *
 *   1x reference:  ./test/tools/frame_hash_ab core rom --csv ref.csv \
 *                     --frames 600 [--option virtualjaguar_true_color=...]
 *   2x check:      ./test/tools/hires_box_check core rom --ref ref.csv \
 *                     --frames 600 [--option ...] \
 *                     (the tool forces virtualjaguar_internal_resolution=2x)
 *
 * Per frame this tool asserts:
 *   (a) the 2x frame's dimensions are exactly 2x the reference row's;
 *   (b) every 2x2 block of the 2x frame is uniform (all 4 subpixels equal);
 *   (c) the FNV-1a hash of the box-downsampled frame equals the reference
 *       row's hash (same hash algorithm as frame_hash_ab).
 *
 * Any violation is a Stage 1 bug by definition.  Exit 0 = all frames pass.
 *
 * Known, bounded exclusion: frames adjacent to a presented-dimension
 * change are skipped (reported as dim_transitions).  On the frame where
 * a game reprograms TOM's width MID-frame, the scanline renderer writes
 * tomWidth pixels per row while screenPitch is still the previous
 * frame's (the stale-pitch scramble the retro_run geometry latch
 * documents and heals on the next frame); rows overlap in linear memory
 * and overlapping writes do not commute with box replication, so that
 * one already-scrambled-at-1x frame cannot be box-exact.  Steady-state
 * frames -- everything else -- must pass without exception.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/hires_box_check test/tools/hires_box_check.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Honors VJ_EXPECT_BUILD (build-identity guard, see scripts/build-id.sh).
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HB_N 2          /* Stage 1 scope fence: N=2 only */
#define HB_MAX_REF 65536

typedef struct {
    unsigned w, h;
    uint64_t hash;
    int      transition;   /* adjacent to a dimension change: skip */
} hb_ref_row;

typedef struct {
    hb_ref_row *rows;
    unsigned  nrows;
    unsigned  frame;
    unsigned  bad_dims;
    unsigned  bad_blocks;
    unsigned  bad_hash;
    unsigned  checked;
    unsigned  skipped_transitions;
    unsigned  duped;
} hb_state;

static void hb_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    hb_state *st = (hb_state *)ud;
    const uint8_t *base = (const uint8_t *)data;
    const hb_ref_row *ref;
    unsigned ref_w, ref_h;
    uint64_t h;
    unsigned y, x, k, nonuniform;

    if (!data) {
        st->duped++;
        return;
    }
    if (st->frame >= st->nrows)
        return;
    ref = &st->rows[st->frame];
    ref_w = ref->w;
    ref_h = ref->h;

    if (ref->transition) {
        /* Stale-pitch geometry-transition frame: excluded, see header. */
        st->skipped_transitions++;
        st->frame++;
        return;
    }

    /* (a) dimensions */
    if (width != ref_w * HB_N || height != ref_h * HB_N) {
        st->bad_dims++;
        fprintf(stderr, "frame %u: dims %ux%u, expected %ux%u\n",
                st->frame, width, height, ref_w * HB_N, ref_h * HB_N);
        st->frame++;
        return;
    }

    /* (b) block uniformity + (c) downsampled hash, in one pass.  The hash
     * must byte-match frame_hash_ab's: FNV-1a over the downsampled rows in
     * raster order, 4 bytes per pixel. */
    h = 1469598103934665603ULL;
    nonuniform = 0;
    for (y = 0; y < ref_h; y++) {
        const uint32_t *row0 =
            (const uint32_t *)(base + (size_t)(y * HB_N) * pitch);
        const uint32_t *row1 =
            (const uint32_t *)(base + (size_t)(y * HB_N + 1) * pitch);
        for (x = 0; x < ref_w; x++) {
            uint32_t p = row0[x * HB_N];
            const uint8_t *b = (const uint8_t *)&row0[x * HB_N];
            if (row0[x * HB_N + 1] != p || row1[x * HB_N] != p
                || row1[x * HB_N + 1] != p)
                nonuniform++;
            for (k = 0; k < 4; k++) {
                h ^= (uint64_t)b[k];
                h *= 1099511628211ULL;
            }
        }
    }

    if (nonuniform) {
        st->bad_blocks++;
        fprintf(stderr, "frame %u: %u non-uniform 2x2 blocks\n",
                st->frame, nonuniform);
    }
    if (h != ref->hash) {
        st->bad_hash++;
        fprintf(stderr,
                "frame %u: downsampled hash %016llx != ref %016llx\n",
                st->frame, (unsigned long long)h,
                (unsigned long long)ref->hash);
    }
    st->checked++;
    st->frame++;
}

/* Load the whole reference CSV and mark rows adjacent to a dimension
 * change (see the header comment for why those frames are excluded). */
static int hb_load_ref(hb_state *st, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    unsigned rf, rw, rh, rn, i;
    int rd;
    unsigned long long rhash;

    if (!f) {
        fprintf(stderr, "hires_box_check: cannot read '%s'\n", path);
        return 0;
    }
    if (!fgets(line, sizeof(line), f)) {   /* header */
        fprintf(stderr, "hires_box_check: empty reference CSV\n");
        fclose(f);
        return 0;
    }
    st->rows = (hb_ref_row *)calloc(HB_MAX_REF, sizeof(hb_ref_row));
    if (!st->rows) {
        fclose(f);
        return 0;
    }
    st->nrows = 0;
    while (st->nrows < HB_MAX_REF && fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%u,%u,%u,%d,%u,%llx", &rf, &rw, &rh, &rd, &rn,
                   &rhash) != 6)
            break;
        st->rows[st->nrows].w    = rw;
        st->rows[st->nrows].h    = rh;
        st->rows[st->nrows].hash = (uint64_t)rhash;
        st->nrows++;
    }
    fclose(f);
    for (i = 0; i < st->nrows; i++) {
        if (i > 0 && (st->rows[i].w != st->rows[i - 1].w
                      || st->rows[i].h != st->rows[i - 1].h))
            st->rows[i].transition = st->rows[i - 1].transition = 1;
        if (i + 1 < st->nrows && (st->rows[i].w != st->rows[i + 1].w
                                  || st->rows[i].h != st->rows[i + 1].h))
            st->rows[i].transition = 1;
    }
    return st->nrows > 0;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    hb_state st;
    const char *ref_path = NULL;
    int i, ok;

    memset(&st, 0, sizeof(st));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ref") == 0 && i + 1 < argc) {
            ref_path = argv[i + 1];
            argv[i] = argv[i + 1] = (char *)"--quiet";
        }
    }
    if (!ref_path) {
        fprintf(stderr, "usage: hires_box_check [core] <rom> --ref REF.csv "
                        "[--frames N] [--option K=V] [--system-dir DIR]\n"
                        "REF.csv comes from a 1x frame_hash_ab run of the "
                        "same scenario.\n");
        return 1;
    }

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "hires_box_check: no ROM/disc given\n");
        return 1;
    }

    if (!hb_load_ref(&st, ref_path))
        return 1;

    harness_set_option(&cfg, "virtualjaguar_internal_resolution", "2x");

    cfg.video_callback      = hb_video;
    cfg.video_callback_data = &st;

    if (!harness_load_rom(&cfg))
        return 1;
    harness_run(&cfg);

    ok = (st.checked > 0 && st.bad_dims == 0 && st.bad_blocks == 0
          && st.bad_hash == 0);
    printf("HIRES_BOX_CHECK frames_checked=%u dim_transitions_skipped=%u "
           "bad_dims=%u bad_blocks=%u bad_hash=%u duped=%u -> %s\n",
           st.checked, st.skipped_transitions,
           st.bad_dims, st.bad_blocks, st.bad_hash, st.duped,
           ok ? "PASS" : "FAIL");
    free(st.rows);

    harness_shutdown(&cfg);
    return ok ? 0 : 1;
}
