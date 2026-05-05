/*
 * test/harness/harness.c — Shared libretro core test harness implementation.
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <math.h>
#include "../../libretro-common/include/libretro.h"

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

/* Active config pointer (needed by callbacks which have no userdata) */
static harness_config *active_cfg;

/* ----------------------------------------------------------------
 * Libretro callbacks
 * ---------------------------------------------------------------- */

static void cb_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    (void)data; (void)pitch;
    if (!active_cfg) return;
    active_cfg->video.total_frames_rendered++;
    active_cfg->video.last_width = w;
    active_cfg->video.last_height = h;
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
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static void cb_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    if (level < RETRO_LOG_WARN) return;
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
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = "/tmp";
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

    if (!lr_init || !lr_load_game || !lr_run) {
        fprintf(stderr, "harness: missing required libretro symbols\n");
        dlclose(cfg->core_handle);
        cfg->core_handle = NULL;
        return false;
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

    if (!lr_load_game(&game)) {
        fprintf(stderr, "harness: retro_load_game failed for '%s'\n", cfg->rom_path);
        free(rom_data);
        return false;
    }

    /* rom_data ownership: libretro spec says core copies what it needs,
     * but VJ keeps a pointer. We leak intentionally for test lifetime. */

    harness_reset_audio(cfg);
    return true;
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
