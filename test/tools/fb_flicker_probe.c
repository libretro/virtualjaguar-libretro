/*
 * fb_flicker_probe.c -- per-frame framebuffer flicker metric (#736).
 *
 * Emits one CSV row per emulated frame:
 *
 *     frame,w,h,duped,changed,aba,aba_rows,hash,rows_p,rows_n,splice
 *
 *   changed   pixels differing from the previous frame
 *   aba       pixels with f[n] == f[n-2] and f[n] != f[n-1]: a pixel that
 *             toggled away and came straight back.  Moving sprites almost
 *             never satisfy this, so it isolates flicker (a region drawn
 *             alternately in two states) from ordinary motion.
 *   aba_rows  distinct scanlines containing at least one such pixel
 *   rows_p    rows of the PREVIOUS frame (n-1, judged when frame n arrives)
 *             that equal frame n-2 but not frame n
 *   rows_n    rows of the previous frame that equal frame n but not n-2
 *   p_lo/p_hi, n_lo/n_hi  first/last scanline of the rows_p / rows_n sets
 *   splice    1 when both rows_p and rows_n reach SPLICE_MIN and occupy
 *             disjoint row ranges: the previous frame was a single
 *             horizontal splice of its neighbours, i.e. the scanout caught
 *             a half-built frame (a tear).  Rows above SPLICE_ROW0 (the
 *             score/clock HUD, which updates on its own cadence) are not
 *             examined
 *
 * Optional: --shot-every N --shot-prefix P writes P<frame>.ppm.
 *
 * Gate mode (regression test for #736, White Men Can't Jump): over frames
 * >= --window-start count
 *   big     frames with aba > BIG_ABA (a third or more of the picture went
 *           back to a state it had two frames earlier)
 *   splice  frames flagged `splice`
 * and exit 1 when big > --max-big or splice > --max-splice (--expect-flicker
 * inverts it: pass only when a limit IS exceeded -- the control arm that
 * proves the metric still sees the bug).  Without --csv no per-frame rows
 * are written.
 * Motion that returns to an earlier state (an animation that cycles with
 * period 2) also counts; compare arms, not absolute values.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/fb_flicker_probe test/tools/fb_flicker_probe.c \
 *      test/harness/harness.c -ldl -lm
 * Run:
 *   ./test/tools/fb_flicker_probe ./virtualjaguar_libretro.dylib rom.jag \
 *      --load-state s.state --frames 600 --csv out.csv
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXPIX (1024u * 512u)
#define BIG_ABA 5000u
#define SPLICE_MIN 8u
#define SPLICE_ROW0 36u   /* ignore the score/clock HUD band above this row */

typedef struct {
    FILE    *csv;
    uint32_t *f0, *f1, *f2;      /* f0 = current, f1 = n-1, f2 = n-2 */
    unsigned w, h, have;
    int      duped;
    unsigned changed, aba, aba_rows, rows_p, rows_n, splice;
    unsigned p_lo, p_hi, n_lo, n_hi;
    uint64_t hash;
    unsigned shot_every;
    const char *shot_prefix;
    unsigned vid_calls;
    unsigned window_start, big, splices;
} fl_state;

static void fl_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    fl_state *st = (fl_state *)ud;
    const uint8_t *base = (const uint8_t *)data;
    unsigned y, x;
    uint32_t *t;
    uint64_t hh = 1469598103934665603ULL;

    st->vid_calls++;
    if (!data) { st->duped = 1; st->changed = st->aba = st->aba_rows = 0; st->rows_p = st->rows_n = st->splice = 0; return; }
    if (width * height > MAXPIX) return;

    t = st->f2; st->f2 = st->f1; st->f1 = st->f0; st->f0 = t;
    st->changed = st->aba = st->aba_rows = 0;
    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)(base + (size_t)y * pitch);
        unsigned rowaba = 0;
        for (x = 0; x < width; x++) {
            uint32_t p = row[x] & 0x00FFFFFFu;
            unsigned i = y * width + x;
            st->f0[i] = p;
            hh ^= p; hh *= 1099511628211ULL;
            if (st->have >= 1 && st->w == width && st->h == height) {
                if (p != st->f1[i]) {
                    st->changed++;
                    if (st->have >= 2 && p == st->f2[i]) { st->aba++; rowaba = 1; }
                }
            }
        }
        st->aba_rows += rowaba;
    }
    st->rows_p = st->rows_n = st->splice = 0;
    st->p_lo = st->n_lo = 9999; st->p_hi = st->n_hi = 0;
    if (st->have >= 2 && st->w == width && st->h == height) {
        /* f0 = n, f1 = n-1, f2 = n-2: judge f1 against both neighbours */
        for (y = SPLICE_ROW0; y < height; y++) {
            const uint32_t *r0 = st->f0 + (size_t)y * width;
            const uint32_t *r1 = st->f1 + (size_t)y * width;
            const uint32_t *r2 = st->f2 + (size_t)y * width;
            int eq_prev = memcmp(r1, r2, (size_t)width * 4) == 0;
            int eq_next = memcmp(r1, r0, (size_t)width * 4) == 0;
            if (eq_prev && !eq_next) {
                st->rows_p++;
                if (y < st->p_lo) st->p_lo = y;
                st->p_hi = y;
            }
            if (eq_next && !eq_prev) {
                st->rows_n++;
                if (y < st->n_lo) st->n_lo = y;
                st->n_hi = y;
            }
        }
        /* one tear boundary: the two row sets must not interleave */
        st->splice = (st->rows_p >= SPLICE_MIN && st->rows_n >= SPLICE_MIN &&
                      (st->p_hi < st->n_lo || st->n_hi < st->p_lo));
    }
    st->w = width; st->h = height; st->hash = hh; st->duped = 0;
    st->have++;
}

static bool fl_frame(void *ud, unsigned frame)
{
    fl_state *st = (fl_state *)ud;
    if (frame >= st->window_start) {
        if (st->aba > BIG_ABA) st->big++;
        if (st->splice) st->splices++;
    }
    if (st->csv)
        fprintf(st->csv,
                "%u,%u,%u,%d,%u,%u,%u,%016llx,%u,%u,%u,%u,%u,%u,%u\n",
                frame, st->w, st->h, st->duped, st->changed, st->aba,
                st->aba_rows, (unsigned long long)st->hash, st->rows_p,
                st->rows_n, st->splice, st->p_lo, st->p_hi, st->n_lo,
                st->n_hi);
    if (st->shot_every && st->shot_prefix && frame % st->shot_every == 0 &&
        st->have) {
        char path[512];
        FILE *f;
        unsigned i;
        snprintf(path, sizeof path, "%s%05u.ppm", st->shot_prefix, frame);
        f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", st->w, st->h);
            for (i = 0; i < st->w * st->h; i++) {
                uint32_t p = st->f0[i];
                fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f);
                fputc(p & 255, f);
            }
            fclose(f);
        }
    }
    st->duped = 0;
    return true;
}

/* Consume "--flag VALUE" from argv (both tokens neutralised so the
 * harness's own parser does not mistake VALUE for a positional). */
static const char *take_arg(int argc, char **argv, int i)
{
    const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
    argv[i] = (char *)"--quiet";
    if (v)
        argv[i + 1] = (char *)"--quiet";
    return v;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    fl_state st;
    const char *csv_path = NULL;
    int i, gate = 0, expect_flicker = 0, pass = 1;
    unsigned max_big = 0, max_splice = 0;
    char detail[256];
    harness_result res;

    memset(&st, 0, sizeof st);
    for (i = 1; i < argc; i++) {
        const char *v;
        if (strcmp(argv[i], "--expect-flicker") == 0) {
            expect_flicker = 1;
            argv[i] = (char *)"--quiet";
        } else if (i + 1 < argc && strcmp(argv[i], "--csv") == 0) {
            csv_path = take_arg(argc, argv, i);
        } else if (i + 1 < argc && strcmp(argv[i], "--shot-every") == 0) {
            v = take_arg(argc, argv, i);
            st.shot_every = (unsigned)atoi(v);
        } else if (i + 1 < argc && strcmp(argv[i], "--shot-prefix") == 0) {
            st.shot_prefix = take_arg(argc, argv, i);
        } else if (i + 1 < argc && strcmp(argv[i], "--window-start") == 0) {
            v = take_arg(argc, argv, i);
            st.window_start = (unsigned)atoi(v);
        } else if (i + 1 < argc && strcmp(argv[i], "--max-big") == 0) {
            v = take_arg(argc, argv, i);
            max_big = (unsigned)atoi(v);
            gate = 1;
        } else if (i + 1 < argc && strcmp(argv[i], "--max-splice") == 0) {
            v = take_arg(argc, argv, i);
            max_splice = (unsigned)atoi(v);
            gate = 1;
        }
    }
    st.f0 = calloc(MAXPIX, 4);
    st.f1 = calloc(MAXPIX, 4);
    st.f2 = calloc(MAXPIX, 4);
    st.csv = csv_path ? fopen(csv_path, "w") : NULL;
    if (!st.f0 || !st.f1 || !st.f2 || (csv_path && !st.csv)) return 1;
    if (st.csv)
        fprintf(st.csv, "frame,w,h,duped,changed,aba,aba_rows,hash,rows_p,"
                        "rows_n,splice,p_lo,p_hi,n_lo,n_hi\n");

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    cfg.video_callback = fl_video;
    cfg.video_callback_data = &st;
    cfg.frame_callback = fl_frame;
    cfg.frame_callback_data = &st;
    if (!harness_load_rom(&cfg)) return 1;
    harness_run(&cfg);
    harness_shutdown(&cfg);
    if (st.csv) fclose(st.csv);

    if (!gate)
        return 0;
    {
        int over = (st.big > max_big) || (st.splices > max_splice);
        pass = expect_flicker ? over : !over;
    }
    snprintf(detail, sizeof detail,
             "frames %u+: %u frames with a third of the picture reverting "
             "(limit %u), %u spliced frames (limit %u)%s",
             st.window_start, st.big, max_big, st.splices, max_splice,
             expect_flicker ? " [control: must EXCEED a limit]" : "");
    res.status = pass ? "PASS" : "FAIL";
    res.name   = expect_flicker ? "wmcj_gameplay_flicker_control"
                                : "wmcj_gameplay_flicker";
    res.detail = detail;
    harness_report(&cfg, &res, 1);
    return pass ? 0 : 1;
}
