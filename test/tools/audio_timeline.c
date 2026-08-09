/* test/tools/audio_timeline.c -- per-second audio RMS timeline of arbitrary
 * length, with DSP state alongside.
 *
 * The harness's built-in audio capture stops at HARNESS_MAX_AUDIO_FRAMES
 * (1200 callbacks ~= 20s), which is exactly where issue #355 (Power Drive
 * Rally "sound gets muted after 20 seconds or so") lives -- every existing
 * tool goes blind at the moment of interest.  This one resets the harness's
 * audio stats every window and prints the window's own mean RMS, so a run
 * can be arbitrarily long.
 *
 * Each line reports, for one window of frames:
 *   rms      mean per-callback RMS magnitude (sqrt of mean L^2+R^2)
 *   peak     max absolute sample in the window
 *   nonsil   fraction of samples that are non-zero
 *   dsp      DSP pc / running flag / LTXD non-zero ratio
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/audio_timeline test/tools/audio_timeline.c \
 *      test/harness/harness.c test/harness/dsp_probe.c -ldl -lm
 *
 * Run:
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/audio_timeline \
 *      ./virtualjaguar_libretro.dylib "rom.jag" \
 *      --frames 3600 --window 60 [--load-state F] [--press F:BTN[:HOLD]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../harness/harness.h"
#include "../harness/dsp_probe.h"

typedef struct {
    harness_config *cfg;
    dsp_probe      *dsp;
    unsigned        window;
    int             have_dsp;
} timeline_state;

/* Summarise the audio captured since the last reset, then clear it so the
 * next window starts from zero and we never hit HARNESS_MAX_AUDIO_FRAMES. */
static void emit_window(timeline_state *st, unsigned frame)
{
    harness_audio_stats *a = &st->cfg->audio;
    double   sumsq = 0.0;
    unsigned peak = 0, i;
    double   rms, nonsil;

    for (i = 0; i < a->frame_count; i++) {
        harness_audio_frame *f = &a->frames[i];
        int pl, pr;
        sumsq += f->rms_l * f->rms_l + f->rms_r * f->rms_r;
        pl = f->peak_l < 0 ? -f->peak_l : f->peak_l;
        pr = f->peak_r < 0 ? -f->peak_r : f->peak_r;
        if ((unsigned)pl > peak) peak = (unsigned)pl;
        if ((unsigned)pr > peak) peak = (unsigned)pr;
    }

    rms    = a->frame_count ? sqrt(sumsq / (double)a->frame_count) : 0.0;
    nonsil = a->total_samples
           ? (double)a->total_nonsilent / (double)a->total_samples : 0.0;

    printf("t=%6.2fs frame %5u  rms %8.1f  peak %6u  nonsil %5.1f%%",
           (double)frame / 60.0, frame, rms, peak, nonsil * 100.0);

    if (st->have_dsp) {
        dsp_probe_snapshot(st->dsp);
        printf("  dsp pc=$%06X run=%d ltxd=%4.1f%% r31=$%08X/$%08X",
               st->dsp->snap.pc, st->dsp->snap.running,
               dsp_probe_ltxd_ratio(st->dsp) * 100.0,
               st->dsp->snap.bank0[31], st->dsp->snap.bank1[31]);
    }
    printf("\n");
    fflush(stdout);

    harness_reset_audio(st->cfg);
}

static bool on_frame(void *userdata, unsigned frame)
{
    timeline_state *st = (timeline_state *)userdata;
    if (st->have_dsp) dsp_probe_per_frame(st->dsp);
    if (st->window && (frame % st->window) == 0) emit_window(st, frame);
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    dsp_probe      dsp;
    timeline_state st;
    harness_result res;
    const char *dump_path = NULL;
    int i;

    memset(&st, 0, sizeof(st));
    st.window = 60;
    cfg.frames = 3600;
    cfg.quiet  = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--window") == 0 && i + 1 < argc)
            st.window = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--dump-dsp-ram") == 0 && i + 1 < argc)
            dump_path = argv[++i];
    }

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path) { fprintf(stderr, "audio_timeline: need a ROM\n"); return 1; }
    if (!harness_load_rom(&cfg)) return 1;

    st.cfg      = &cfg;
    st.dsp      = &dsp;
    st.have_dsp = dsp_probe_init(&dsp, &cfg) ? 1 : 0;
    if (!st.have_dsp)
        fprintf(stderr, "audio_timeline: DSP probe unavailable "
                        "(build the core with TEST_EXPORTS=1 for DSP columns)\n");

    cfg.frame_callback      = on_frame;
    cfg.frame_callback_data = &st;

    printf("audio_timeline: %u frames, %u-frame windows\n", cfg.frames, st.window);
    harness_run(&cfg);

    if (dump_path && st.have_dsp && dsp.sym.get_ram) {
        FILE *f = fopen(dump_path, "wb");
        if (f) {
            fwrite(dsp.sym.get_ram(), 1, DSP_RAM_SIZE, f);
            fclose(f);
            printf("audio_timeline: wrote %d bytes of DSP RAM to %s\n",
                   DSP_RAM_SIZE, dump_path);
        } else
            fprintf(stderr, "audio_timeline: cannot write %s\n", dump_path);
    }

    {
        uint16_t *ee = (uint16_t *)harness_dlsym(&cfg, "eeprom_ram");
        if (ee) {
            unsigned k, ff = 0;
            printf("EEPROM (64 words):");
            for (k = 0; k < 64; k++) {
                if ((k % 8) == 0) printf("\n  %02u:", k);
                printf(" %04X", ee[k]);
                if (ee[k] == 0xFFFF) ff++;
            }
            printf("\n  erased ($FFFF) words: %u/64\n", ff);
        }
    }

    res.status = "INFO";
    res.name   = "audio_timeline";
    res.detail = "per-window RMS printed above";
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return 0;
}
