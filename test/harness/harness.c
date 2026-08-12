/*
 * test/harness/harness.c — Shared libretro core test harness implementation.
 */

/* clock_gettime()/CLOCK_MONOTONIC (used by harness_time_now below) are
 * POSIX.1-2001, and glibc hides them from a strictly-conforming translation
 * unit — which is what every harness-linked test is, since they all build
 * with -std=c99.  Must precede the first system header include. */
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <math.h>
#include "../../libretro-common/include/libretro.h"

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#ifdef __APPLE__
#define DEFAULT_CORE "virtualjaguar_libretro.dylib"
#elif defined(_WIN32)
#define DEFAULT_CORE "virtualjaguar_libretro.dll"
#else
#define DEFAULT_CORE "virtualjaguar_libretro.so"
#endif

#define SILENCE_THRESHOLD 32

/* ----------------------------------------------------------------
 * Libretro function pointers
 * ---------------------------------------------------------------- */

static void (*lr_init)(void);
static void (*lr_deinit)(void);
static void (*lr_set_environment)(retro_environment_t);
static void (*lr_set_video_refresh)(retro_video_refresh_t);
static void (*lr_set_audio_sample)(retro_audio_sample_t);
static void (*lr_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*lr_set_input_poll)(retro_input_poll_t);
static void (*lr_set_input_state)(retro_input_state_t);
static bool (*lr_load_game)(const struct retro_game_info *);
static void (*lr_unload_game)(void);
static void (*lr_run)(void);
static size_t (*lr_serialize_size)(void);
static bool (*lr_unserialize)(const void *, size_t);
static bool (*lr_serialize)(void *, size_t);

/* Active config pointer (needed by callbacks which have no userdata) */
static harness_config *active_cfg;
/* ROM image handed to retro_load_game.  The core copies it, but keep the
 * pointer so harness_shutdown can release it after retro_unload_game --
 * otherwise every ROM-loading harness leaks the whole image, which
 * LeakSanitizer reports (1 MB per run for test/roms/yarc.j64). */
static void *active_rom_data;

/* ----------------------------------------------------------------
 * Libretro callbacks
 * ---------------------------------------------------------------- */

static void cb_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!active_cfg) return;
    if (active_cfg->video.total_frames_rendered > 0 &&
        (w != active_cfg->video.last_width || h != active_cfg->video.last_height))
        active_cfg->video.dimension_changes++;
    active_cfg->video.total_frames_rendered++;
    active_cfg->video.last_width = w;
    active_cfg->video.last_height = h;
    /* Framebuffer hash, only for tools that asked for one (trace_probe's
     * --field-csv).  Hashed HERE, while the core's buffer is provably
     * live, rather than by retaining the pointer for the frame hook to
     * read after retro_run has returned.  data == NULL is a duped frame:
     * the previous image is re-presented, so the previous hash stands. */
    if (active_cfg->want_fb_hash && data) {
        const uint8_t *base = (const uint8_t *)data;
        uint32_t hash = 2166136261u;   /* FNV-1a 32-bit offset basis */
        size_t row_bytes = (size_t)w * 4;   /* XRGB8888 */
        unsigned y;
        size_t x;
        for (y = 0; y < h; y++) {
            const uint8_t *row = base + (size_t)y * pitch;
            for (x = 0; x < row_bytes; x++) {
                hash ^= row[x];
                hash *= 16777619u;
            }
        }
        active_cfg->last_fb_hash = hash;
    }
    if (active_cfg->video_callback)
        active_cfg->video_callback(active_cfg->video_callback_data,
                                   data, w, h, pitch);
}

static void cb_audio_sample(int16_t l, int16_t r)
{
    (void)l; (void)r;
}

static size_t cb_audio_batch(const int16_t *data, size_t frames)
{
    harness_audio_stats *a;
    size_t i;
    unsigned nonsilent = 0;
    int peak_l = 0, peak_r = 0;
    double sum_sq_l = 0, sum_sq_r = 0;

    if (!active_cfg) return frames;
    a = &active_cfg->audio;

    a->total_batch_calls++;
    a->total_samples += frames;

    if (a->first_batch_frame < 0)
        a->first_batch_frame = (int)active_cfg->current_frame;

    for (i = 0; i < frames; i++) {
        int16_t l = data[i * 2];
        int16_t r = data[i * 2 + 1];
        int abs_l = (l < 0) ? -((int)l) : (int)l;
        int abs_r = (r < 0) ? -((int)r) : (int)r;

        if (abs_l > SILENCE_THRESHOLD || abs_r > SILENCE_THRESHOLD)
            nonsilent++;

        if (abs_l > peak_l) peak_l = abs_l;
        if (abs_r > peak_r) peak_r = abs_r;
        sum_sq_l += (double)l * l;
        sum_sq_r += (double)r * r;
    }

    a->total_nonsilent += nonsilent;

    if (nonsilent > 0 && a->first_audio_frame < 0)
        a->first_audio_frame = (int)active_cfg->current_frame;

    /* Dropout detection */
    if (nonsilent > 0) {
        if (!a->was_playing && a->first_audio_frame >= 0 &&
            (int)active_cfg->current_frame > a->first_audio_frame + 5)
            a->dropout_count++;
        a->was_playing = 1;
    } else {
        if (a->was_playing)
            a->silent_after_onset++;
        a->was_playing = 0;
    }

    /* Per-frame stats */
    if (a->frame_count < HARNESS_MAX_AUDIO_FRAMES) {
        harness_audio_frame *af = &a->frames[a->frame_count];
        af->frame = active_cfg->current_frame;
        af->samples = frames;
        af->nonsilent = nonsilent;
        af->peak_l = peak_l;
        af->peak_r = peak_r;
        af->rms_l = (frames > 0) ? sqrt(sum_sq_l / (double)frames) : 0;
        af->rms_r = (frames > 0) ? sqrt(sum_sq_r / (double)frames) : 0;
        a->frame_count++;
    }

    return frames;
}

static void cb_input_poll(void) {}

static int16_t cb_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{
    unsigned e;
    if (!active_cfg) return 0;
    if (active_cfg->input_callback)
        return active_cfg->input_callback(active_cfg->input_callback_data,
                                          p, d, i, id);
    if (d != RETRO_DEVICE_JOYPAD) return 0;
    for (e = 0; e < active_cfg->num_input_events; e++) {
        const harness_input_event *ev = &active_cfg->input_events[e];
        if (ev->port == p && ev->button == id &&
            active_cfg->current_frame >= ev->first_frame &&
            active_cfg->current_frame <= ev->last_frame)
            return 1;
    }
    return 0;
}

uint32_t harness_input_mask(harness_config *cfg, unsigned port)
{
    uint32_t mask = 0;
    unsigned id;

    if (!cfg) return 0;
    for (id = 0; id < 16; id++) {
        int16_t v = 0;
        if (cfg->input_callback) {
            v = cfg->input_callback(cfg->input_callback_data, port,
                                    RETRO_DEVICE_JOYPAD, 0, id);
        } else {
            unsigned e;
            for (e = 0; e < cfg->num_input_events; e++) {
                const harness_input_event *ev = &cfg->input_events[e];
                if (ev->port == port && ev->button == id &&
                    cfg->current_frame >= ev->first_frame &&
                    cfg->current_frame <= ev->last_frame) {
                    v = 1;
                    break;
                }
            }
        }
        if (v) mask |= (1u << id);
    }
    return mask;
}

/* Map a --press button token to a RETRO_DEVICE_ID_JOYPAD_* id, following
 * the core's default (non-custom) retropad layout in libretro.c:
 * Jaguar A/B/C = retropad A/B/Y, Pause = Select, Option = Start,
 * numpad 0-6 = X/L/R/L2/R2/L3/R3. */
static int harness_button_id(const char *name)
{
    static const struct { const char *name; unsigned id; } map[] = {
        { "up",     RETRO_DEVICE_ID_JOYPAD_UP },
        { "down",   RETRO_DEVICE_ID_JOYPAD_DOWN },
        { "left",   RETRO_DEVICE_ID_JOYPAD_LEFT },
        { "right",  RETRO_DEVICE_ID_JOYPAD_RIGHT },
        { "a",      RETRO_DEVICE_ID_JOYPAD_A },
        { "b",      RETRO_DEVICE_ID_JOYPAD_B },
        { "c",      RETRO_DEVICE_ID_JOYPAD_Y },
        { "pause",  RETRO_DEVICE_ID_JOYPAD_SELECT },
        { "option", RETRO_DEVICE_ID_JOYPAD_START },
        { "0",      RETRO_DEVICE_ID_JOYPAD_X },
        { "1",      RETRO_DEVICE_ID_JOYPAD_L },
        { "2",      RETRO_DEVICE_ID_JOYPAD_R },
        { "3",      RETRO_DEVICE_ID_JOYPAD_L2 },
        { "4",      RETRO_DEVICE_ID_JOYPAD_R2 },
        { "5",      RETRO_DEVICE_ID_JOYPAD_L3 },
        { "6",      RETRO_DEVICE_ID_JOYPAD_R3 },
    };
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(name, map[i].name) == 0)
            return (int)map[i].id;
    /* Multi-digit tokens fall through the table (0-6 are single chars),
     * allowing raw retropad ids like "10". */
    if (name[0] >= '0' && name[0] <= '9' && name[1] != '\0')
        return atoi(name);
    return -1;
}

/* Parse "FRAME:BUTTON[:HOLD]" into an input event on port 0. */
static bool harness_parse_press(harness_config *cfg, const char *spec)
{
    char buf[64];
    char *btn, *hold_s;
    unsigned frame, hold = 10;
    int id;

    if (cfg->num_input_events >= HARNESS_MAX_INPUT_EVENTS) {
        fprintf(stderr, "harness: too many --press events (max %d)\n",
                HARNESS_MAX_INPUT_EVENTS);
        return false;
    }
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    btn = strchr(buf, ':');
    if (!btn) {
        fprintf(stderr, "harness: bad --press '%s' (want FRAME:BUTTON[:HOLD])\n", spec);
        return false;
    }
    *btn++ = '\0';
    frame = (unsigned)atoi(buf);

    hold_s = strchr(btn, ':');
    if (hold_s) {
        *hold_s++ = '\0';
        hold = (unsigned)atoi(hold_s);
        if (hold == 0) hold = 1;
    }

    id = harness_button_id(btn);
    if (id < 0) {
        fprintf(stderr, "harness: unknown button '%s' in --press '%s'\n", btn, spec);
        return false;
    }

    cfg->input_events[cfg->num_input_events].first_frame = frame;
    cfg->input_events[cfg->num_input_events].last_frame  = frame + hold - 1;
    cfg->input_events[cfg->num_input_events].port        = 0;
    cfg->input_events[cfg->num_input_events].button      = (unsigned)id;
    cfg->num_input_events++;
    return true;
}

void harness_press(harness_config *cfg, unsigned port, unsigned button,
                   unsigned first_frame, unsigned hold_frames)
{
    if (cfg->num_input_events >= HARNESS_MAX_INPUT_EVENTS) return;
    if (hold_frames == 0) hold_frames = 1;
    cfg->input_events[cfg->num_input_events].first_frame = first_frame;
    cfg->input_events[cfg->num_input_events].last_frame  = first_frame + hold_frames - 1;
    cfg->input_events[cfg->num_input_events].port        = port;
    cfg->input_events[cfg->num_input_events].button      = button;
    cfg->num_input_events++;
}

static void cb_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    /* VJ_HARNESS_LOG_INFO=1 lets INFO-level core logs through -- needed to
     * see CDTraceDump / CDROMDiagSummary output, which print at LOG_INF.
     * VJ_HARNESS_LOG_DEBUG=1 additionally passes DEBUG (e.g. the CD HLE
     * per-call trace, which logs at LOG_DBG). */
    static int checked = 0;
    static enum retro_log_level min_level = RETRO_LOG_WARN;
    if (!checked) {
        const char *ei = getenv("VJ_HARNESS_LOG_INFO");
        const char *ed = getenv("VJ_HARNESS_LOG_DEBUG");
        if (ei && ei[0] == '1') min_level = RETRO_LOG_INFO;
        if (ed && ed[0] == '1') min_level = RETRO_LOG_DEBUG;
        checked = 1;
    }
    if (level < min_level) return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static struct retro_log_callback log_cb_struct = { cb_log };

static bool cb_environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        *(struct retro_log_callback *)data = log_cb_struct;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (active_cfg) active_cfg->video.set_geometry_calls++;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        if (active_cfg) active_cfg->video.set_av_info_calls++;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = (active_cfg && active_cfg->system_dir)
                                   ? active_cfg->system_dir : "/tmp";
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = "/tmp";
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        unsigned i;
        if (!var->key || !active_cfg) { var->value = NULL; return false; }

        /* Check user-specified options */
        for (i = 0; i < active_cfg->num_options; i++) {
            if (strcmp(var->key, active_cfg->options[i].key) == 0) {
                var->value = active_cfg->options[i].value;
                return true;
            }
        }

        /* Built-in defaults */
        if (strcmp(var->key, "virtualjaguar_bios") == 0) {
            var->value = active_cfg->use_bios ? "enabled" : "disabled";
            return true;
        }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0) {
            var->value = "enabled";
            return true;
        }

        var->value = NULL;
        return false;
    }
    default:
        return false;
    }
}

/* ----------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------- */

bool harness_init_from_args(harness_config *cfg, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            cfg->json_output = 1;
        } else if (strcmp(argv[i], "--bios") == 0) {
            cfg->use_bios = 1;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            cfg->quiet = 1;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            cfg->frames = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--snapshot-interval") == 0 && i + 1 < argc) {
            cfg->snapshot_interval = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--system-dir") == 0 && i + 1 < argc) {
            cfg->system_dir = argv[++i];
        } else if (strcmp(argv[i], "--load-state") == 0 && i + 1 < argc) {
            cfg->load_state_path = argv[++i];
        } else if (strcmp(argv[i], "--save-state") == 0 && i + 1 < argc) {
            cfg->save_state_path = argv[++i];
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc) {
            if (!harness_parse_press(cfg, argv[++i]))
                return false;
        } else if (strcmp(argv[i], "--trace-out") == 0 && i + 1 < argc) {
            cfg->trace_out_path = argv[++i];
        } else if (strcmp(argv[i], "--field-csv") == 0 && i + 1 < argc) {
            cfg->field_csv_path = argv[++i];
        } else if (strcmp(argv[i], "--snap-prefix") == 0 && i + 1 < argc) {
            cfg->snap_prefix = argv[++i];
        } else if (strcmp(argv[i], "--watch") == 0 && i + 1 < argc) {
            /* Stored raw; trace_probe parses and validates.  Silently
             * capped rather than fatal: a tool with its own --watch
             * (irq_rate_probe) must not start failing because it passed
             * more than the flight recorder's 16. */
            if (cfg->num_watch_specs < HARNESS_MAX_WATCH_SPECS)
                cfg->watch_specs[cfg->num_watch_specs++] = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--snap") == 0 && i + 1 < argc) {
            if (cfg->num_snap_specs < HARNESS_MAX_SNAP_FRAMES)
                cfg->snap_specs[cfg->num_snap_specs++] = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--mark") == 0 && i + 1 < argc) {
            if (cfg->num_mark_specs < HARNESS_MAX_MARK_SPECS)
                cfg->mark_specs[cfg->num_mark_specs++] = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--option") == 0 && i + 1 < argc) {
            char *eq;
            i++;
            eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                harness_set_option(cfg, argv[i], eq + 1);
            }
        } else if (argv[i][0] == '-') {
            /* Unknown flag — skip. Tools pre-parse their own flags
             * before calling harness_init_from_args. */
            continue;
        } else if (!cfg->core_path) {
            /* First positional: check if it looks like a library */
            const char *ext = strrchr(argv[i], '.');
            if (ext && (strcmp(ext, ".dylib") == 0 ||
                        strcmp(ext, ".so") == 0 ||
                        strcmp(ext, ".dll") == 0)) {
                cfg->core_path = argv[i];
            } else {
                /* Assume it's a ROM if no core set yet */
                if (!cfg->rom_path)
                    cfg->rom_path = argv[i];
                else
                    cfg->core_path = argv[i];
            }
        } else if (!cfg->rom_path) {
            cfg->rom_path = argv[i];
        }
    }

    if (!cfg->core_path)
        cfg->core_path = DEFAULT_CORE;

    return harness_load_core(cfg);
}

bool harness_load_core(harness_config *cfg)
{
    cfg->core_handle = dlopen(cfg->core_path, RTLD_NOW);
    if (!cfg->core_handle) {
        fprintf(stderr, "harness: dlopen(%s): %s\n", cfg->core_path, dlerror());
        return false;
    }

    lr_init = dlsym(cfg->core_handle, "retro_init");
    lr_deinit = dlsym(cfg->core_handle, "retro_deinit");
    lr_set_environment = dlsym(cfg->core_handle, "retro_set_environment");
    lr_set_video_refresh = dlsym(cfg->core_handle, "retro_set_video_refresh");
    lr_set_audio_sample = dlsym(cfg->core_handle, "retro_set_audio_sample");
    lr_set_audio_sample_batch = dlsym(cfg->core_handle, "retro_set_audio_sample_batch");
    lr_set_input_poll = dlsym(cfg->core_handle, "retro_set_input_poll");
    lr_set_input_state = dlsym(cfg->core_handle, "retro_set_input_state");
    lr_load_game = dlsym(cfg->core_handle, "retro_load_game");
    lr_unload_game = dlsym(cfg->core_handle, "retro_unload_game");
    lr_run = dlsym(cfg->core_handle, "retro_run");
    lr_serialize_size = dlsym(cfg->core_handle, "retro_serialize_size");
    lr_unserialize = dlsym(cfg->core_handle, "retro_unserialize");
    lr_serialize = dlsym(cfg->core_handle, "retro_serialize");

    if (!lr_init || !lr_load_game || !lr_run) {
        fprintf(stderr, "harness: missing required libretro symbols\n");
        dlclose(cfg->core_handle);
        cfg->core_handle = NULL;
        return false;
    }

    /* Build-identity guard (mirrors test_framework.h): always print which
     * binary is under test; if VJ_EXPECT_BUILD is set, refuse a core whose
     * version string does not contain it (stale/wrong-branch binary).
     * `make` can skip a rebuild when file mtimes are second-identical,
     * which silently tests old code. */
    {
        void (*p_sysinfo)(struct retro_system_info *) =
            (void (*)(struct retro_system_info *))dlsym(cfg->core_handle,
                                                        "retro_get_system_info");
        const char *expect = getenv("VJ_EXPECT_BUILD");
        struct retro_system_info si;
        memset(&si, 0, sizeof(si));
        if (p_sysinfo) {
            p_sysinfo(&si);
            if (!cfg->quiet)
                fprintf(stderr, "harness: core %s %s (%s)\n",
                        si.library_name ? si.library_name : "?",
                        si.library_version ? si.library_version : "?",
                        cfg->core_path);
        }
        if (expect && expect[0]) {
            /* Token-boundary match: "91f0804" must not accept a stale
             * "91f0804-dirty" build (version format: "vX.Y.Z rev[-dirty]"). */
            const char *hit = si.library_version ? strstr(si.library_version, expect) : NULL;
            char tail = hit ? hit[strlen(expect)] : '-';
            if (!hit || (tail != '\0' && tail != ' ')) {
                fprintf(stderr,
                        "harness: FATAL build mismatch -- core reports \"%s\" but "
                        "VJ_EXPECT_BUILD=\"%s\"; rebuild with `make TEST_EXPORTS=1`.\n",
                        si.library_version ? si.library_version : "(none)", expect);
                dlclose(cfg->core_handle);
                cfg->core_handle = NULL;
                return false;
            }
        }
    }

    return true;
}

bool harness_load_rom(harness_config *cfg)
{
    FILE *f;
    long rom_size;
    uint8_t *rom_data;
    struct retro_game_info game;

    if (!cfg->rom_path) {
        fprintf(stderr, "harness: no ROM path specified\n");
        return false;
    }

    f = fopen(cfg->rom_path, "rb");
    if (!f) {
        fprintf(stderr, "harness: cannot open ROM '%s'\n", cfg->rom_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    rom_size = ftell(f);
    if (rom_size <= 0) {
        fprintf(stderr, "harness: ROM '%s' is empty or unreadable\n", cfg->rom_path);
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    rom_data = malloc((size_t)rom_size);
    if (!rom_data) { fclose(f); return false; }
    if (fread(rom_data, 1, (size_t)rom_size, f) != (size_t)rom_size) {
        fprintf(stderr, "harness: short read on ROM '%s'\n", cfg->rom_path);
        free(rom_data);
        fclose(f);
        return false;
    }
    fclose(f);

    active_cfg = cfg;

    lr_set_environment(cb_environment);
    lr_init();
    lr_set_video_refresh(cb_video);
    lr_set_audio_sample(cb_audio_sample);
    lr_set_audio_sample_batch(cb_audio_batch);
    lr_set_input_poll(cb_input_poll);
    lr_set_input_state(cb_input_state);

    memset(&game, 0, sizeof(game));
    game.path = cfg->rom_path;
    game.data = rom_data;
    game.size = (size_t)rom_size;

    active_rom_data = rom_data;

    if (!lr_load_game(&game)) {
        fprintf(stderr, "harness: retro_load_game failed for '%s'\n", cfg->rom_path);
        active_rom_data = NULL;
        free(rom_data);
        return false;
    }

    /* rom_data ownership: libretro spec says core copies what it needs,
     * but VJ keeps a pointer. We leak intentionally for test lifetime. */

    if (cfg->load_state_path && !harness_load_state(cfg, cfg->load_state_path))
        return false;

    harness_reset_audio(cfg);
    return true;
}

/* Write the core's current state to `path` as a raw core blob (no RASTATE
 * container).  Useful for capturing a headless repro point that
 * harness_load_state() can restore later. */
bool harness_save_state(harness_config *cfg, const char *path)
{
    size_t  size;
    void   *buf;
    FILE   *f;
    bool    ok;

    if (!lr_serialize || !lr_serialize_size) {
        fprintf(stderr, "harness: core has no retro_serialize\n");
        return false;
    }
    size = lr_serialize_size();
    buf  = malloc(size);
    if (!buf)
        return false;
    if (!lr_serialize(buf, size)) {
        fprintf(stderr, "harness: retro_serialize failed\n");
        free(buf);
        return false;
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "harness: cannot write save state '%s'\n", path);
        free(buf);
        return false;
    }
    ok = (fwrite(buf, 1, size, f) == size);
    fclose(f);
    free(buf);
    if (ok && !cfg->quiet)
        printf("harness: wrote save state '%s' (%u bytes)\n",
               path, (unsigned)size);
    return ok;
}

/* Restore a RetroArch .state blob.  Must be called after harness_load_rom()
 * -- the core sizes its state against the loaded game.  Lets a test start
 * from a hand-captured point deep inside a title (a mission briefing, a
 * menu) instead of scripting the whole way in with --press. */
bool harness_load_state(harness_config *cfg, const char *path)
{
    FILE   *f;
    long    len;
    size_t  want;
    size_t  plen;
    void   *buf;
    void   *payload;
    bool    ok;

    if (!lr_unserialize || !lr_serialize_size) {
        fprintf(stderr, "harness: core has no retro_unserialize/serialize_size\n");
        return false;
    }

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "harness: cannot open save state '%s'\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fprintf(stderr, "harness: save state '%s' is empty\n", path);
        fclose(f);
        return false;
    }

    want = lr_serialize_size();

    buf = malloc((size_t)len);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "harness: short read on save state '%s'\n", path);
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);

    /* RetroArch wraps the core payload in a "RASTATE" container:
     *   "RASTATE" + u8 version, then blocks of { 4-byte id, u32 LE size,
     *   payload }, terminated by "END " with size 0.  The core's own blob
     *   is the "MEM " block.  Unwrap it so a state saved from RetroArch
     *   (which is what a user can actually hand us) loads directly. */
    payload = buf;
    plen    = (size_t)len;
    if (plen > 16 && memcmp(buf, "RASTATE", 7) == 0) {
        const uint8_t *p   = (const uint8_t *)buf + 8;
        const uint8_t *end = (const uint8_t *)buf + plen;
        bool found = false;
        while (p + 8 <= end) {
            uint32_t bsize = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                             ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            if (memcmp(p, "END ", 4) == 0)
                break;
            if (memcmp(p, "MEM ", 4) == 0) {
                if (p + 8 + bsize > end) {
                    fprintf(stderr, "harness: RASTATE MEM block overruns file\n");
                    free(buf);
                    return false;
                }
                payload = (void *)(p + 8);
                plen    = bsize;
                found   = true;
                break;
            }
            p += 8 + bsize;
        }
        if (!found) {
            fprintf(stderr, "harness: RASTATE container has no MEM block\n");
            free(buf);
            return false;
        }
        if (!cfg->quiet)
            printf("harness: unwrapped RASTATE container (%u byte core state)\n",
                   (unsigned)plen);
    }

    if (plen != want)
        fprintf(stderr,
                "harness: warning: core state is %u bytes, core expects %u\n",
                (unsigned)plen, (unsigned)want);

    ok = lr_unserialize(payload, plen);
    free(buf);

    if (!ok)
        fprintf(stderr, "harness: retro_unserialize rejected '%s'\n", path);
    else if (!cfg->quiet)
        printf("harness: restored save state '%s' (%ld bytes)\n", path, len);

    return ok;
}

void harness_run(harness_config *cfg)
{
    unsigned i;
    active_cfg = cfg;
    cfg->stop_requested = 0;

    for (i = 0; i < cfg->frames && !cfg->stop_requested; i++) {
        cfg->current_frame = i + 1;
        lr_run();

        if (cfg->frame_callback) {
            if (!cfg->frame_callback(cfg->frame_callback_data, cfg->current_frame))
                break;
        }
    }

    if (cfg->save_state_path)
        harness_save_state(cfg, cfg->save_state_path);
}

void harness_step(harness_config *cfg)
{
    active_cfg = cfg;
    cfg->current_frame++;
    lr_run();
}

void harness_shutdown(harness_config *cfg)
{
    if (lr_unload_game) lr_unload_game();
    /* After unload_game: the core must not reference the image any more. */
    free(active_rom_data);
    active_rom_data = NULL;
    if (lr_deinit) lr_deinit();
    if (cfg->core_handle) {
        dlclose(cfg->core_handle);
        cfg->core_handle = NULL;
    }
    active_cfg = NULL;
}

void *harness_dlsym(harness_config *cfg, const char *name)
{
    void *sym;
    if (!cfg->core_handle) return NULL;
    sym = dlsym(cfg->core_handle, name);
    if (!sym && !cfg->quiet)
        fprintf(stderr, "harness: dlsym('%s') not found\n", name);
    return sym;
}

void harness_set_option(harness_config *cfg, const char *key, const char *value)
{
    if (cfg->num_options >= HARNESS_MAX_OPTIONS) return;
    cfg->options[cfg->num_options].key = key;
    cfg->options[cfg->num_options].value = value;
    cfg->num_options++;
}

void harness_reset_audio(harness_config *cfg)
{
    memset(&cfg->audio, 0, sizeof(cfg->audio));
    cfg->audio.first_audio_frame = -1;
    cfg->audio.first_batch_frame = -1;
}

void harness_reset_video(harness_config *cfg)
{
    /* Zeroing total_frames_rendered also re-arms the dimension-change
     * detector (see cb_video above): the first frame after a reset
     * establishes the baseline instead of counting as a change. */
    memset(&cfg->video, 0, sizeof(cfg->video));
}

/* ----------------------------------------------------------------
 * Monotonic wall clock
 *
 * Same shape as test/tools/test_benchmark.c and test/harness/timing_probe.c,
 * lifted here so tests that link only harness.c do not need a third copy.
 * ---------------------------------------------------------------- */

#ifdef __APPLE__
static mach_timebase_info_data_t harness_timebase;

uint64_t harness_time_now(void)
{
    return mach_absolute_time();
}

double harness_time_elapsed_sec(uint64_t start, uint64_t end)
{
    uint64_t elapsed = end - start;
    if (harness_timebase.denom == 0)
        mach_timebase_info(&harness_timebase);
    /* Convert to nanoseconds, then seconds */
    return (double)elapsed * (double)harness_timebase.numer /
           (double)harness_timebase.denom / 1e9;
}
#else
uint64_t harness_time_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

double harness_time_elapsed_sec(uint64_t start, uint64_t end)
{
    return (double)(end - start) / 1e9;
}
#endif

void harness_report(harness_config *cfg, const harness_result *results, unsigned count)
{
    unsigned i;
    unsigned passes = 0, fails = 0, skips = 0;

    for (i = 0; i < count; i++) {
        if (strcmp(results[i].status, "PASS") == 0) passes++;
        else if (strcmp(results[i].status, "FAIL") == 0) fails++;
        else if (strcmp(results[i].status, "SKIP") == 0) skips++;
    }

    if (cfg->json_output) {
        printf("{\"summary\":{\"pass\":%u,\"fail\":%u,\"skip\":%u},\"results\":[\n",
               passes, fails, skips);
        for (i = 0; i < count; i++) {
            printf("  {\"status\":\"%s\",\"name\":\"%s\",\"detail\":\"%s\"}%s\n",
                   results[i].status, results[i].name, results[i].detail,
                   (i + 1 < count) ? "," : "");
        }
        printf("],\"audio\":{\"total_samples\":%zu,\"nonsilent\":%u,"
               "\"first_audio_frame\":%d,\"batch_calls\":%u,"
               "\"dropouts\":%u},",
               cfg->audio.total_samples, cfg->audio.total_nonsilent,
               cfg->audio.first_audio_frame, cfg->audio.total_batch_calls,
               cfg->audio.dropout_count);
        printf("\"video\":{\"frames_rendered\":%u,\"width\":%u,\"height\":%u}}\n",
               cfg->video.total_frames_rendered,
               cfg->video.last_width, cfg->video.last_height);
    } else {
        printf("\n=== Results: %u passed, %u failed, %u skipped ===\n",
               passes, fails, skips);
        for (i = 0; i < count; i++) {
            printf("  %s: [%s] %s\n", results[i].status, results[i].name,
                   results[i].detail);
        }
        if (!cfg->quiet) {
            printf("\n  Audio: %zu samples, %u non-silent, onset=frame %d, "
                   "%u batch calls, %u dropouts\n",
                   cfg->audio.total_samples, cfg->audio.total_nonsilent,
                   cfg->audio.first_audio_frame, cfg->audio.total_batch_calls,
                   cfg->audio.dropout_count);
            printf("  Video: %u frames rendered, %ux%u\n",
                   cfg->video.total_frames_rendered,
                   cfg->video.last_width, cfg->video.last_height);
        }
    }
}
