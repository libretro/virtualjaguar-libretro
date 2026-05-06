/*
 * test/harness/harness.h — Shared libretro core test harness.
 *
 * ======================================================================
 * AGENT QUICK-START
 * ======================================================================
 *
 * This header provides the complete API for writing headless tests against
 * the Virtual Jaguar libretro core.  A minimal test looks like:
 *
 *   #include "harness/harness.h"
 *   int main(int argc, char **argv) {
 *       harness_config cfg = HARNESS_CONFIG_DEFAULT;
 *       cfg.frames = 120;
 *       if (!harness_init_from_args(&cfg, argc, argv)) return 1;
 *       if (!harness_load_rom(&cfg))                   return 1;
 *       harness_run(&cfg);
 *       int ok = (cfg.audio.total_nonsilent > 0);
 *       harness_result res = { ok ? "PASS" : "FAIL", "audio_present",
 *                              ok ? "non-silent samples detected" : "silence" };
 *       harness_report(&cfg, &res, 1);
 *       harness_shutdown(&cfg);
 *       return ok ? 0 : 1;
 *   }
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test_foo \
 *           test_foo.c test/harness/harness.c -ldl -lm
 *
 * Run:    ./test_foo [core.dylib] [rom.jag] [options...]
 *
 * ======================================================================
 * CLI CONVENTIONS (all tests share these)
 * ======================================================================
 *
 *   Positional:
 *     arg1 — core library path  (default: ./virtualjaguar_libretro.dylib)
 *     arg2 — ROM path           (optional; some tests need it)
 *
 *   Flags:
 *     --json           Output machine-parseable JSON instead of human text
 *     --frames N       Override frame count to run
 *     --bios           Enable BIOS mode (default: HLE)
 *     --option K=V     Set core option (e.g. --option virtualjaguar_dsp=enabled)
 *     --quiet          Suppress per-frame output, only show final results
 *     --savestate F    Load savestate after ROM init (RetroArch or raw VJSS)
 *     --snapshot-interval N   Probe snapshot every N frames (default: 1)
 *
 * ======================================================================
 * CORE OPTION OVERRIDE TABLE
 * ======================================================================
 *
 * Instead of the callback guessing, you can pre-populate cfg.options[]:
 *
 *   cfg.options[0] = (harness_option){"virtualjaguar_bios", "enabled"};
 *   cfg.options[1] = (harness_option){"virtualjaguar_usefastblitter", "enabled"};
 *   cfg.num_options = 2;
 *
 * The environment callback returns these to the core on GET_VARIABLE.
 *
 * ======================================================================
 * EXTENDING: ADDING A NEW PROBE
 * ======================================================================
 *
 * 1. Create test/harness/foo_probe.h + foo_probe.c
 * 2. In your probe_init(), call harness_dlsym(cfg, "symbol_name") to
 *    resolve internal core symbols.
 * 3. Call your probe from the test's run loop or from a per-frame hook.
 * 4. See dsp_probe.h for the reference implementation.
 *
 * ======================================================================
 */

#ifndef HARNESS_H
#define HARNESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum number of core option overrides */
#define HARNESS_MAX_OPTIONS  32
/* Maximum number of results to report */
#define HARNESS_MAX_RESULTS  64
/* Audio capture frames */
#define HARNESS_MAX_AUDIO_FRAMES 1200

/* ----------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------- */

typedef struct {
    const char *key;
    const char *value;
} harness_option;

typedef struct {
    unsigned frame;
    size_t   samples;
    unsigned nonsilent;
    int16_t  peak_l, peak_r;
    double   rms_l, rms_r;
} harness_audio_frame;

typedef struct {
    size_t   total_samples;
    unsigned total_nonsilent;
    unsigned total_batch_calls;
    int      first_audio_frame;
    int      first_batch_frame;
    unsigned dropout_count;
    unsigned silent_after_onset;
    int      was_playing;
    harness_audio_frame frames[HARNESS_MAX_AUDIO_FRAMES];
    unsigned frame_count;
} harness_audio_stats;

typedef struct {
    unsigned total_frames_rendered;
    unsigned last_width;
    unsigned last_height;
} harness_video_stats;

/* Per-frame callback: called after each retro_run().
 * Return false to stop execution early. */
typedef bool (*harness_frame_cb)(void *userdata, unsigned frame);

typedef struct {
    /* Configuration (set before init) */
    const char   *core_path;
    const char   *rom_path;
    const char   *savestate_path;
    unsigned      frames;
    int           use_bios;
    int           json_output;
    int           quiet;
    unsigned      snapshot_interval;
    harness_option options[HARNESS_MAX_OPTIONS];
    unsigned      num_options;

    /* Per-frame hook */
    harness_frame_cb frame_callback;
    void            *frame_callback_data;

    /* Runtime state (set by harness) */
    void  *core_handle;
    unsigned current_frame;
    harness_audio_stats audio;
    harness_video_stats video;
    int    stop_requested;
} harness_config;

typedef struct {
    const char *status;   /* "PASS", "FAIL", "SKIP", "INFO" */
    const char *name;     /* short test-case name */
    const char *detail;   /* human-readable explanation */
} harness_result;

/* Default config initializer */
#define HARNESS_CONFIG_DEFAULT { \
    .core_path = NULL, \
    .rom_path = NULL, \
    .savestate_path = NULL, \
    .frames = 300, \
    .use_bios = 0, \
    .json_output = 0, \
    .quiet = 0, \
    .snapshot_interval = 1, \
    .options = {{0}}, \
    .num_options = 0, \
    .frame_callback = NULL, \
    .frame_callback_data = NULL, \
    .core_handle = NULL, \
    .current_frame = 0, \
    .audio = {0}, \
    .video = {0}, \
    .stop_requested = 0 \
}

/* ----------------------------------------------------------------
 * API
 * ---------------------------------------------------------------- */

/* Parse argc/argv into cfg.  Returns true on success. */
bool harness_init_from_args(harness_config *cfg, int argc, char **argv);

/* Load the core dynamic library.  Called by init_from_args. */
bool harness_load_core(harness_config *cfg);

/* Load a ROM into the core.  Requires core loaded + rom_path set. */
bool harness_load_rom(harness_config *cfg);

/* Run cfg->frames frames, collecting audio/video stats.
 * Calls cfg->frame_callback after each frame if set. */
void harness_run(harness_config *cfg);

/* Run a single frame. Useful for custom loops. */
void harness_step(harness_config *cfg);

/* Unload game + deinit + dlclose. */
void harness_shutdown(harness_config *cfg);

/* Resolve a symbol from the loaded core. Returns NULL + prints warning on failure. */
void *harness_dlsym(harness_config *cfg, const char *name);

/* Output results in the configured format (json or human). */
void harness_report(harness_config *cfg, const harness_result *results, unsigned count);

/* Convenience: add a core option override. */
void harness_set_option(harness_config *cfg, const char *key, const char *value);

/* Load a savestate file after ROM init.  Handles both RetroArch
 * "RASTATE" wrapper and raw VJSS format automatically. */
bool harness_load_savestate(harness_config *cfg, const char *path);

/* Reset audio stats (useful between test phases). */
void harness_reset_audio(harness_config *cfg);

#endif /* HARNESS_H */
