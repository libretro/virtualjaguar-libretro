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
 *     --snapshot-interval N   Probe snapshot every N frames (default: 1)
 *     --load-state F   Restore a RetroArch .state after the game loads,
 *                      to start deep inside a title without scripting
 *                      the whole way in with --press
 *     --press F:BTN[:HOLD]    Hold button BTN from frame F for HOLD frames
 *                      (default 10).  BTN: up down left right a b c pause
 *                      option 0-6, or raw retropad id.  Repeatable.
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
/* Maximum number of scripted input events */
#define HARNESS_MAX_INPUT_EVENTS 128

/* ----------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------- */

typedef struct {
    const char *key;
    const char *value;
} harness_option;

/* Scripted input: hold RETRO_DEVICE_ID_JOYPAD_<button> on <port> from
 * first_frame through last_frame (inclusive).  Populated from repeated
 *   --press FRAME:BUTTON[:HOLD_FRAMES]      (HOLD_FRAMES default 10)
 * flags, where BUTTON is one of: up down left right a b c pause option
 * 0 1 2 3 4 5 6 (Jaguar numpad -> retropad x/l/r/l2/r2/l3/r3), or a raw
 * retropad id number.  Port 0 only via CLI; tests can fill events for
 * other ports programmatically. */
typedef struct {
    unsigned first_frame;
    unsigned last_frame;
    unsigned port;
    unsigned button;   /* RETRO_DEVICE_ID_JOYPAD_* */
} harness_input_event;

/* Optional programmatic input hook: return the input_state value for
 * (port, device, index, id); takes precedence over the event table. */
typedef int16_t (*harness_input_cb)(void *userdata, unsigned port,
                                    unsigned device, unsigned index,
                                    unsigned id);

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

/* Video frame callback: called from the core's video refresh with the raw
 * XRGB8888 framebuffer (data may be NULL on duped frames).  Lets tests
 * inspect pixels (motion detection, screenshots) without reimplementing
 * the libretro plumbing. */
typedef void (*harness_video_cb)(void *userdata, const void *data,
                                 unsigned width, unsigned height, size_t pitch);

typedef struct {
    /* Configuration (set before init) */
    const char   *core_path;
    const char   *rom_path;
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

    /* Video pixel hook (optional) */
    harness_video_cb video_callback;
    void            *video_callback_data;

    /* Scripted input (optional) */
    harness_input_event input_events[HARNESS_MAX_INPUT_EVENTS];
    unsigned            num_input_events;
    harness_input_cb    input_callback;
    void               *input_callback_data;

    /* Directory returned for GET_SYSTEM_DIRECTORY (CD BIOS discovery).
     * Default "/tmp"; set to "test/roms/private" for CD titles, or use
     * --system-dir. */
    const char   *system_dir;

    /* Optional RetroArch .state to restore right after the game loads,
     * so a test can start deep inside a title instead of scripting its
     * way in.  Set via --load-state. */
    const char   *load_state_path;

    /* Optional path to write the core state to when harness_run()
     * finishes, for capturing a repro point.  Set via --save-state. */
    const char   *save_state_path;

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
    .frames = 300, \
    .use_bios = 0, \
    .json_output = 0, \
    .quiet = 0, \
    .snapshot_interval = 1, \
    .options = {{0}}, \
    .num_options = 0, \
    .frame_callback = NULL, \
    .frame_callback_data = NULL, \
    .video_callback = NULL, \
    .video_callback_data = NULL, \
    .input_events = {{0}}, \
    .num_input_events = 0, \
    .input_callback = NULL, \
    .input_callback_data = NULL, \
    .system_dir = "/tmp", \
    .load_state_path = NULL, \
    .save_state_path = NULL, \
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

/* Load a ROM into the core.  Requires core loaded + rom_path set.
 * If cfg->load_state_path is set, the state is restored afterwards. */
bool harness_load_rom(harness_config *cfg);

/* Write the core's current state to `path` (raw core blob). */
bool harness_save_state(harness_config *cfg, const char *path);

/* Restore a RetroArch .state blob.  Call after harness_load_rom(). */
bool harness_load_state(harness_config *cfg, const char *path);

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

/* Convenience: schedule a scripted button press (see harness_input_event). */
void harness_press(harness_config *cfg, unsigned port, unsigned button,
                   unsigned first_frame, unsigned hold_frames);

/* Reset audio stats (useful between test phases). */
void harness_reset_audio(harness_config *cfg);

#endif /* HARNESS_H */
