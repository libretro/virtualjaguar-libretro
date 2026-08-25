/* test_voicechat_inertness.c -- Voice chat must leave the emulated
 * machine unobservable (issue #485).  Spec: docs/voice-chat-design.md.
 *
 * Runs yarc.j64 twice (voice off, then voice on with synthetic mic +
 * local monitor) and asserts per-frame framebuffer hashes and savestate
 * digests at N/2 and N are identical.
 *
 * Exit: 0 PASS, 1 FAIL, 2 harness error, 77 SKIP (ROM missing).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../harness/harness.h"

#define MAX_FRAMES 600
#define DEFAULT_FRAMES 120
#define DEFAULT_ROM "test/roms/yarc.j64"

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

typedef struct {
    uint64_t fb[MAX_FRAMES];
    unsigned fb_count;
    uint64_t digest_mid;
    uint64_t digest_end;
} run_result;

static run_result *g_cur;

static uint64_t fnv1a(uint64_t hh, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; i++) {
        hh ^= p[i];
        hh *= FNV_PRIME;
    }
    return hh;
}

static uint64_t hash_file(const char *path)
{
    FILE *fp;
    uint8_t buf[4096];
    size_t n;
    uint64_t hh = FNV_OFFSET;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        hh = fnv1a(hh, buf, n);
    fclose(fp);
    return hh;
}

static void video_cb(void *ud, const void *data,
                     unsigned width, unsigned height, size_t pitch)
{
    const uint8_t *row = (const uint8_t *)data;
    uint64_t hh;
    unsigned y;

    (void)ud;
    if (!g_cur || g_cur->fb_count >= MAX_FRAMES)
        return;
    hh = FNV_OFFSET;
    hh = fnv1a(hh, &width, sizeof(width));
    hh = fnv1a(hh, &height, sizeof(height));
    if (data) {
        for (y = 0; y < height; y++)
            hh = fnv1a(hh, row + (size_t)y * pitch, (size_t)width * 4);
    }
    g_cur->fb[g_cur->fb_count] = hh;
}

static int do_run(const char *core, const char *rom, unsigned frames,
                  int voice_on, run_result *out)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned i;
    char state_path[] = "/tmp/vj_voicechat_inert.state";

    memset(out, 0, sizeof(*out));
    cfg.core_path = core;
    cfg.rom_path = rom;
    cfg.frames = frames;
    cfg.quiet = 1;
    cfg.mic_tone = voice_on ? 1 : 0;
    cfg.video_callback = video_cb;

    cfg.options[cfg.num_options].key = "virtualjaguar_voice_chat";
    cfg.options[cfg.num_options].value = voice_on ? "enabled" : "disabled";
    cfg.num_options++;
    if (voice_on) {
        cfg.options[cfg.num_options].key = "virtualjaguar_voice_chat_monitor";
        cfg.options[cfg.num_options].value = "enabled";
        cfg.num_options++;
        cfg.options[cfg.num_options].key = "virtualjaguar_voice_chat_gate";
        cfg.options[cfg.num_options].value = "open_mic";
        cfg.num_options++;
        /* Loopback so discovery/keepalive do not need a peer; voice still
         * mixes locally via monitor. */
        cfg.options[cfg.num_options].key = "virtualjaguar_netlink";
        cfg.options[cfg.num_options].value = "loopback";
        cfg.num_options++;
    }

    if (!harness_load_core(&cfg))
        return 0;
    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 0;
    }

    g_cur = out;
    for (i = 0; i < frames; i++) {
        harness_step(&cfg);
        out->fb_count++;
        if (i + 1 == frames / 2) {
            if (harness_save_state(&cfg, state_path))
                out->digest_mid = hash_file(state_path);
        }
        if (i + 1 == frames) {
            if (harness_save_state(&cfg, state_path))
                out->digest_end = hash_file(state_path);
        }
    }
    g_cur = NULL;
    harness_shutdown(&cfg);
    unlink(state_path);
    return 1;
}

int main(int argc, char **argv)
{
    const char *core = NULL;
    const char *rom = DEFAULT_ROM;
    unsigned frames = DEFAULT_FRAMES;
    run_result off, on;
    harness_result results[4];
    unsigned nres = 0;
    int failed = 0;
    int i;
    struct stat st;
    harness_config report_cfg = HARNESS_CONFIG_DEFAULT;
    char d_fb[256], d_ss[256];
    int first_diff;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--json"))
            report_cfg.json_output = 1;
        else if (!strcmp(argv[i], "--quiet"))
            report_cfg.quiet = 1;
        else if (argv[i][0] != '-') {
            if (!core)
                core = argv[i];
            else
                rom = argv[i];
        }
    }
    if (!core)
        core = "./virtualjaguar_libretro.dylib";
    if (frames > MAX_FRAMES)
        frames = MAX_FRAMES;

    if (stat(rom, &st) != 0) {
        fprintf(stderr, "SKIP: ROM missing: %s\n", rom);
        return 77;
    }

    if (!do_run(core, rom, frames, 0, &off)) {
        fprintf(stderr, "harness failed (voice off)\n");
        return 2;
    }
    if (!do_run(core, rom, frames, 1, &on)) {
        fprintf(stderr, "harness failed (voice on)\n");
        return 2;
    }

    first_diff = -1;
    for (i = 0; i < (int)frames; i++) {
        if (off.fb[i] != on.fb[i]) {
            first_diff = i;
            break;
        }
    }
    if (first_diff < 0)
        snprintf(d_fb, sizeof(d_fb),
                 "%u frames identical on/off", frames);
    else
        snprintf(d_fb, sizeof(d_fb),
                 "framebuffer diverges at frame %d", first_diff + 1);
    results[nres].status = (first_diff < 0) ? "PASS" : "FAIL";
    results[nres].name = "fb_inertness";
    results[nres].detail = d_fb;
    if (first_diff >= 0)
        failed = 1;
    nres++;

    if (off.digest_mid && off.digest_end
        && off.digest_mid == on.digest_mid
        && off.digest_end == on.digest_end) {
        snprintf(d_ss, sizeof(d_ss),
                 "savestate digests @%u/%u identical",
                 frames / 2, frames);
        results[nres].status = "PASS";
    } else if (!off.digest_mid || !off.digest_end
               || !on.digest_mid || !on.digest_end) {
        snprintf(d_ss, sizeof(d_ss), "savestate capture failed");
        results[nres].status = "FAIL";
        failed = 1;
    } else {
        snprintf(d_ss, sizeof(d_ss),
                 "savestate digests differ (mid %d end %d)",
                 off.digest_mid != on.digest_mid,
                 off.digest_end != on.digest_end);
        results[nres].status = "FAIL";
        failed = 1;
    }
    results[nres].name = "savestate_inertness";
    results[nres].detail = d_ss;
    nres++;

    harness_report(&report_cfg, results, nres);
    return failed ? 1 : 0;
}
