/*
 * xenowings_sfx_probe.c -- issue #895: Xenowings plays its music but not the
 * shot / explosion sound effects.
 *
 * Drives the game from observed state (see xenowings_ingame_video.c: menu ->
 * B -> loading screen -> first lit gameplay frame), then holds a fire button
 * and records the audio stream to a 48 kHz stereo 16-bit WAV, printing a
 * per-window RMS line so music-only and fire-held windows can be compared.
 *
 * Usage:
 *   ./test/tools/xenowings_sfx_probe <core> <xenowings.rom> --out F.wav
 *       [--fire a|b|c|none] [--fire-at N] [--game-frames N] [--window N]
 *       [--option K=V] [--press F:BTN[:HOLD]]
 *       [--dump-prefix P --dump-at N] [--seq FROM TO --seq-out F]
 *       [--shot F.ppm --shot-at N]
 *   --fire-at   frames after the first lit gameplay frame (default 300)
 *   --game-frames  frames recorded after the first lit frame (default 1500)
 *   --dump-*    write DSP RAM (P.dsp, 8 KB at $F1B000) and main RAM (P.ram)
 *               at +N; --seq appends one DSP RAM image per frame FROM..TO
 *   --shot      write the frame at +N as a binary PPM
 * At the end it prints a DSP PC histogram (longword buckets in DSP RAM).
 * Issue #895 measurement (B held from +200): the fire windows' RMS/peak
 * rise from ~3500/12.9k (sound effects cut off) to ~4500/20k+ once the
 * effect voices get their step quotient (docs/jtrm-errata.md B25).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/xenowings_sfx_probe test/tools/xenowings_sfx_probe.c \
 *      test/harness/harness.c -ldl -lm
 */
#include "../harness/harness.h"
#include "libretro.h"
#include "../harness/dsp_probe.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_MIN_WIDTH   600u
#define MENU_LIT_PCT     20.0
#define MENU_HOLD_FRAMES 60u
#define MENU_TIMEOUT     4000u
#define PRESS_HOLD       10u
#define LOADING_MAX_PCT  10.0
#define LOADING_TIMEOUT  900u
#define GAME_LIT_PCT     50.0
#define WAV_RATE         48000

enum { PH_MENU, PH_LOADING, PH_GAME, PH_DONE };

static harness_config *g_cfg;
static int       g_phase = PH_MENU;
static unsigned  g_w;
static double    g_pct;
static unsigned  g_menu_run, g_press_frame, g_first_lit, g_fire_id = 99;
static unsigned  g_fire_at = 300, g_game_frames = 1500, g_window = 30;
static int       g_have_lit;
static FILE     *g_fp;
static uint64_t  g_written;
static double    g_win_sq;
static uint64_t  g_win_n;
static int       g_win_peak;
static unsigned  g_win_idx;
static uint64_t  g_vh;
static unsigned  g_seq_from = 0xFFFFFFFFu, g_seq_to = 0;
static FILE     *g_seq_fp;
static unsigned  g_shot_at = 0xFFFFFFFFu, g_cur_f;
static const char *g_shot_path;
static void shot(const void *data, unsigned w, unsigned h, size_t pitch)
{
    FILE *f = fopen(g_shot_path, "wb"); unsigned x, y;
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y * pitch);
        for (x = 0; x < w; x++) { fputc((row[x] >> 16) & 255, f); fputc((row[x] >> 8) & 255, f); fputc(row[x] & 255, f); }
    }
    fclose(f);
}
static unsigned  g_dump_at = 0;
static const char *g_dump_prefix = NULL;
static uint8_t **g_mainram;
static uint8_t *(*g_dspram)(void);
static uint32_t *g_dsp_pc;
static unsigned  g_pch[2048];   /* DSP PC histogram, longword index in DSP RAM */
static void dump_file(const char *suffix, const void *p, size_t n)
{
    char path[1024]; FILE *f;
    snprintf(path, sizeof path, "%s.%s", g_dump_prefix, suffix);
    f = fopen(path, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}

static void put32(FILE *f, uint32_t v)
{
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}
static void put16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }
static void wav_header(FILE *f, uint64_t n)
{
    uint32_t b = (uint32_t)(n * 4);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + b);
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16);
    put16(f, 1); put16(f, 2); put32(f, WAV_RATE); put32(f, WAV_RATE * 4);
    put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, b);
}

static size_t audio_batch(const int16_t *d, size_t frames)
{
    size_t i;
    if (!g_have_lit || !frames)
        return frames;               /* record gameplay only */
    if (g_fp) {
        fwrite(d, 4, frames, g_fp);
        g_written += frames;
    }
    for (i = 0; i < frames * 2; i++) {
        int v = d[i];
        g_win_sq += (double)v * v;
        if (abs(v) > g_win_peak) g_win_peak = abs(v);
    }
    g_win_n += frames * 2;
    return frames;
}
static void audio_sample(int16_t l, int16_t r)
{
    int16_t p[2]; p[0] = l; p[1] = r; audio_batch(p, 1);
}

static void video(void *ud, const void *data, unsigned width, unsigned height, size_t pitch)
{
    unsigned x, y, lit = 0;
    uint64_t h = 1469598103934665603ULL;
    (void)ud;
    if (!data) return;
    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y * pitch);
        for (x = 0; x < width; x++)
            { if (row[x] & 0x00FFFFFFu) lit++; h = (h ^ (row[x] & 0xFFFFFFu)) * 1099511628211ULL; }
    }
    g_vh = h;
    if (g_shot_path && g_have_lit && g_cur_f - g_first_lit == g_shot_at) shot(data, width, height, pitch);
    g_w = width;
    g_pct = (width && height) ? 100.0 * (double)lit / ((double)width * height) : 0.0;
}

static bool frame_cb(void *ud, unsigned f)
{
    (void)ud; g_cur_f = f;
    switch (g_phase) {
    case PH_MENU:
        if (g_w >= MENU_MIN_WIDTH && g_pct >= MENU_LIT_PCT) g_menu_run++; else g_menu_run = 0;
        if (g_menu_run >= MENU_HOLD_FRAMES) {
            harness_press(g_cfg, 0, RETRO_DEVICE_ID_JOYPAD_B, f + 1, PRESS_HOLD);
            g_press_frame = f + 1; printf("menu B pressed at frame %u\n", f + 1);
            g_phase = PH_LOADING;
        } else if (f >= MENU_TIMEOUT) {
            fprintf(stderr, "menu never appeared\n"); g_phase = PH_DONE;
        }
        break;
    case PH_LOADING:
        if (f > g_press_frame + PRESS_HOLD && g_w < MENU_MIN_WIDTH && g_pct > 0.0 && g_pct < LOADING_MAX_PCT)
            g_phase = PH_GAME;
        else if (f >= g_press_frame + LOADING_TIMEOUT) {
            fprintf(stderr, "loading screen never appeared\n"); g_phase = PH_DONE;
        }
        break;
    case PH_GAME:
        if (!g_have_lit) {
            if (g_w < MENU_MIN_WIDTH && g_pct >= GAME_LIT_PCT) {
                g_have_lit = 1; g_first_lit = f;
                printf("first lit gameplay frame %u\n", f);
                if (g_fire_id != 99)
                    harness_press(g_cfg, 0, g_fire_id, f + g_fire_at, g_game_frames);
            }
            break;
        }
        if ((f - g_first_lit) % g_window == 0 && g_win_n) {
            printf("win %3u  frame +%4u  rms %8.1f  peak %5d vid %08x %s\n", g_win_idx++, f - g_first_lit,
                   sqrt(g_win_sq / (double)g_win_n), g_win_peak, (unsigned)(g_vh & 0xFFFFFFFFu),
                   (f - g_first_lit >= g_fire_at && g_fire_id != 99) ? "FIRE" : "");
            g_win_sq = 0; g_win_n = 0; g_win_peak = 0;
        }
        if (g_dsp_pc) { uint32_t pc = *g_dsp_pc; if (pc >= 0xF1B000 && pc < 0xF1D000) g_pch[(pc - 0xF1B000) >> 2]++; }
        if (g_seq_fp && f - g_first_lit >= g_seq_from && f - g_first_lit <= g_seq_to && g_dspram)
            fwrite(g_dspram(), 1, 8192, g_seq_fp);
        if (g_dump_prefix && f - g_first_lit == g_dump_at) {
            if (g_dspram) dump_file("dsp", g_dspram(), 8192);
            if (g_mainram && *g_mainram) dump_file("ram", *g_mainram, 0x200000);
            fprintf(stderr, "dumped state at +%u\n", g_dump_at);
        }
        if (f - g_first_lit >= g_game_frames) {
            unsigned k; printf("DSP PC hist (top):\n");
            for (k = 0; k < 2048; k++) if (g_pch[k] > 20) printf("  %06X %u\n", 0xF1B000 + k * 4, g_pch[k]);
            g_phase = PH_DONE;
        }
        break;
    default: break;
    }
    return g_phase != PH_DONE;
}

int main(int argc, char **argv)
{
    harness_config cfg;
    const char *out = NULL;
    void (*set_batch)(size_t (*)(const int16_t *, size_t));
    void (*set_sample)(void (*)(int16_t, int16_t));
    int i;

    for (i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--fire")) {
            const char *b = argv[i + 1];
            g_fire_id = !strcmp(b, "a") ? RETRO_DEVICE_ID_JOYPAD_A :
                        !strcmp(b, "b") ? RETRO_DEVICE_ID_JOYPAD_B :
                        !strcmp(b, "c") ? RETRO_DEVICE_ID_JOYPAD_Y : 99;
        }
        else if (!strcmp(argv[i], "--fire-at")) g_fire_at = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--game-frames")) g_game_frames = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--dump-at")) g_dump_at = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--dump-prefix")) g_dump_prefix = argv[i + 1];
        else if (!strcmp(argv[i], "--shot-at")) g_shot_at = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--shot")) g_shot_path = argv[i + 1];
        else if (!strcmp(argv[i], "--seq")) { g_seq_from = (unsigned)atoi(argv[i + 1]); g_seq_to = (unsigned)atoi(argv[i + 2]); }
        else if (!strcmp(argv[i], "--seq-out")) g_seq_fp = fopen(argv[i + 1], "wb");
        else if (!strcmp(argv[i], "--window")) g_window = (unsigned)atoi(argv[i + 1]);
    }
    if (!out) { fprintf(stderr, "need --out\n"); return 2; }
    memset(&cfg, 0, sizeof cfg);
    cfg = (harness_config)HARNESS_CONFIG_DEFAULT;
    cfg.frames = MENU_TIMEOUT + LOADING_TIMEOUT + g_game_frames + 600u;
    if (!harness_init_from_args(&cfg, argc, argv)) return 2;
    g_cfg = &cfg;
    cfg.video_callback = video;
    cfg.frame_callback = frame_cb;
    if (!harness_load_rom(&cfg)) return 1;
    g_mainram = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    g_dspram = (uint8_t *(*)(void))harness_dlsym(&cfg, "DSPGetRAM");
    g_dsp_pc = (uint32_t *)harness_dlsym(&cfg, "dsp_pc");
    g_fp = fopen(out, "wb");
    if (!g_fp) { perror(out); return 1; }
    wav_header(g_fp, 0);
    set_batch = (void (*)(size_t (*)(const int16_t *, size_t)))harness_dlsym(&cfg, "retro_set_audio_sample_batch");
    set_sample = (void (*)(void (*)(int16_t, int16_t)))harness_dlsym(&cfg, "retro_set_audio_sample");
    set_batch(audio_batch);
    if (set_sample) set_sample(audio_sample);
    harness_run(&cfg);
    fseek(g_fp, 0, SEEK_SET);
    wav_header(g_fp, g_written);
    fclose(g_fp);
    printf("WAV %s sample_frames=%llu seconds=%.2f\n", out, (unsigned long long)g_written, (double)g_written / WAV_RATE);
    harness_shutdown(&cfg);
    return g_have_lit ? 0 : 1;
}
