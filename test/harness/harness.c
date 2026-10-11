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

/* ---- Synthetic microphone (#485) ------------------------------------ */

struct retro_microphone {
    int active;
    unsigned phase;   /* free-running phase for a ~440 Hz tone at 8 kHz */
};

static struct retro_microphone g_synth_mic;

static retro_microphone_t *synth_open_mic(const retro_microphone_params_t *params)
{
    (void)params;
    memset(&g_synth_mic, 0, sizeof(g_synth_mic));
    g_synth_mic.active = 0;
    return &g_synth_mic;
}

static void synth_close_mic(retro_microphone_t *microphone)
{
    if (microphone)
        microphone->active = 0;
}

static bool synth_get_params(const retro_microphone_t *microphone,
                             retro_microphone_params_t *params)
{
    if (!microphone || !params)
        return false;
    params->rate = 8000;
    return true;
}

static bool synth_set_mic_state(retro_microphone_t *microphone, bool state)
{
    if (!microphone)
        return false;
    microphone->active = state ? 1 : 0;
    return true;
}

static bool synth_get_mic_state(const retro_microphone_t *microphone)
{
    return microphone && microphone->active;
}

static int synth_read_mic(retro_microphone_t *microphone,
                          int16_t *samples, size_t num_samples)
{
    size_t i;
    if (!microphone || !samples)
        return -1;
    if (!microphone->active) {
        /* Contract: disabled mic returns -1. */
        return -1;
    }
    /* Soft 440 Hz-ish square-ish tone via a saw phase — enough energy for
     * VAD and presence checks without needing a real sine table. */
    for (i = 0; i < num_samples; i++) {
        int16_t s = (int16_t)(((microphone->phase & 0x1F) < 16) ? 4000 : -4000);
        samples[i] = s;
        microphone->phase++;
    }
    return (int)num_samples;
}

static struct retro_microphone_interface g_synth_mic_iface = {
    RETRO_MICROPHONE_INTERFACE_VERSION,
    synth_open_mic,
    synth_close_mic,
    synth_get_params,
    synth_set_mic_state,
    synth_get_mic_state,
    synth_read_mic
};

/* Option keys the core registered (#742).  An --option key the core never
 * registered used to be silently ignored, so a typo, a renamed option or an
 * option missing on this branch measured the defaults and reported a
 * confident null result (the --bios / risc_pc_histogram casualties). */
#define HARNESS_MAX_REG_KEYS 512
#define HARNESS_REG_KEY_LEN  64
static char     reg_keys[HARNESS_MAX_REG_KEYS][HARNESS_REG_KEY_LEN];
static unsigned reg_num_keys;

static void reg_add_key(const char *key)
{
    unsigned i;
    if (!key || !*key) return;
    for (i = 0; i < reg_num_keys; i++)
        if (strcmp(reg_keys[i], key) == 0) return;
    if (reg_num_keys >= HARNESS_MAX_REG_KEYS) return;
    snprintf(reg_keys[reg_num_keys], HARNESS_REG_KEY_LEN, "%s", key);
    reg_num_keys++;
}

/* Environment capture (#841): OSD notices (SET_MESSAGE_EXT), the core
 * option definitions of the first and the latest SET_CORE_OPTIONS_V2[_INTL]
 * push, and any SET_VARIABLE (which the core must never call).  File-static
 * rather than in harness_config because the first option push happens inside
 * retro_set_environment, before a config is attached.  Every string is a
 * private copy: the core's amended-definition buffers are reused. */
#define HARNESS_MAX_OSD 32
static char    *osd_texts[HARNESS_MAX_OSD];
static unsigned osd_n;
static unsigned opt_pushes;
static unsigned set_variable_n;
static int      variable_update_pending;
static unsigned opt_pushes_before_load;
typedef struct {
    char   **keys;
    char   **descs;
    char   **infos;
    unsigned n;
} opt_snapshot;
static opt_snapshot opt_first, opt_latest;

/* SET_CORE_OPTIONS_DISPLAY (#850): the frontend-side visibility of each
 * key.  Unmentioned = visible (the libretro default), and a definitions
 * push rebuilds the frontend's option manager with every row visible
 * again (RetroArch's behaviour), so a push clears the table -- the core
 * must then re-push visibility, which is exactly what is worth testing.
 * `refuse` makes the environment call answer false, like a frontend
 * without option-visibility support. */
#define HARNESS_MAX_VIS 192
static struct { char key[64]; int visible; } vis_tab[HARNESS_MAX_VIS];
static unsigned vis_n;
static unsigned vis_calls;
static int      vis_refuse;

static void opt_snapshot_free(opt_snapshot *s)
{
    unsigned i;
    for (i = 0; i < s->n; i++) {
        free(s->keys[i]); free(s->descs[i]); free(s->infos[i]);
    }
    free(s->keys);
    free(s->descs);
    free(s->infos);
    s->keys = NULL;
    s->descs = NULL;
    s->infos = NULL;
    s->n = 0;
}

static void opt_snapshot_take(opt_snapshot *s,
                              const struct retro_core_option_v2_definition *d)
{
    unsigned n = 0, i;
    opt_snapshot_free(s);
    while (d[n].key) n++;
    s->keys  = (char **)calloc(n ? n : 1, sizeof(char *));
    s->descs = (char **)calloc(n ? n : 1, sizeof(char *));
    s->infos = (char **)calloc(n ? n : 1, sizeof(char *));
    if (!s->keys || !s->descs || !s->infos) return;
    for (i = 0; i < n; i++) {
        s->keys[i]  = strdup(d[i].key);
        s->descs[i] = strdup(d[i].desc ? d[i].desc : "");
        s->infos[i] = strdup(d[i].info ? d[i].info : "");
    }
    s->n = n;
}

static void opt_capture(const struct retro_core_option_v2_definition *d)
{
    if (!d) return;
    vis_n = 0;   /* a rebuild shows every row again (see vis_tab) */
    if (opt_pushes == 0) opt_snapshot_take(&opt_first, d);
    opt_snapshot_take(&opt_latest, d);
    opt_pushes++;
}

static const char *opt_snapshot_find(const opt_snapshot *s, const char *key)
{
    unsigned i;
    for (i = 0; i < s->n; i++)
        if (strcmp(s->keys[i], key) == 0) return s->infos[i];
    return NULL;
}

static const char *opt_snapshot_find_desc(const opt_snapshot *s, const char *key)
{
    unsigned i;
    for (i = 0; i < s->n; i++)
        if (strcmp(s->keys[i], key) == 0) return s->descs[i];
    return NULL;
}

static void vis_record(const struct retro_core_option_display *d)
{
    unsigned i;
    if (!d || !d->key) return;
    for (i = 0; i < vis_n; i++)
        if (strcmp(vis_tab[i].key, d->key) == 0) {
            vis_tab[i].visible = d->visible ? 1 : 0;
            return;
        }
    if (vis_n < HARNESS_MAX_VIS) {
        snprintf(vis_tab[vis_n].key, sizeof(vis_tab[vis_n].key), "%s", d->key);
        vis_tab[vis_n].visible = d->visible ? 1 : 0;
        vis_n++;
    }
}

int harness_option_visible(const char *key)
{
    unsigned i;
    for (i = 0; i < vis_n; i++)
        if (strcmp(vis_tab[i].key, key) == 0) return vis_tab[i].visible;
    return 1;
}
unsigned harness_option_display_calls(void) { return vis_calls; }
void harness_option_display_refuse(int refuse) { vis_refuse = refuse; }
const char *harness_options_first_desc(const char *key)
{ return opt_snapshot_find_desc(&opt_first, key); }
const char *harness_options_def_key(unsigned i)
{ return i < opt_first.n ? opt_first.keys[i] : NULL; }

unsigned harness_osd_count(void) { return osd_n; }
const char *harness_osd_text(unsigned i) { return i < osd_n ? osd_texts[i] : NULL; }
unsigned harness_options_push_count(void) { return opt_pushes; }
unsigned harness_options_pushes_before_load(void) { return opt_pushes_before_load; }
unsigned harness_options_def_count_first(void) { return opt_first.n; }
unsigned harness_options_def_count_latest(void) { return opt_latest.n; }
const char *harness_options_info(const char *key)
{ return opt_snapshot_find(&opt_latest, key); }
const char *harness_options_first_info(const char *key)
{ return opt_snapshot_find(&opt_first, key); }
unsigned harness_set_variable_calls(void) { return set_variable_n; }
void harness_notify_variable_update(void) { variable_update_pending = 1; }

static void reg_record(unsigned cmd, const void *data)
{
    unsigned i;
    if (!data) return;
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        const struct retro_variable *v = (const struct retro_variable *)data;
        for (i = 0; v[i].key; i++) reg_add_key(v[i].key);
        break;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        const struct retro_core_option_definition *d =
            (const struct retro_core_option_definition *)data;
        for (i = 0; d[i].key; i++) reg_add_key(d[i].key);
        break;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        const struct retro_core_options_intl *o =
            (const struct retro_core_options_intl *)data;
        if (o->us)
            for (i = 0; o->us[i].key; i++) reg_add_key(o->us[i].key);
        break;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        const struct retro_core_options_v2 *o =
            (const struct retro_core_options_v2 *)data;
        if (o->definitions)
            for (i = 0; o->definitions[i].key; i++)
                reg_add_key(o->definitions[i].key);
        if (o->definitions) opt_capture(o->definitions);
        break;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const struct retro_core_options_v2_intl *o =
            (const struct retro_core_options_v2_intl *)data;
        if (o->us && o->us->definitions)
            for (i = 0; o->us->definitions[i].key; i++)
                reg_add_key(o->us->definitions[i].key);
        if (o->us && o->us->definitions) opt_capture(o->us->definitions);
        break;
    }
    default:
        break;
    }
}

/* Call after retro_set_environment + retro_init, before retro_load_game.
 * An unregistered --option key is fatal (exit 2, naming the key) unless
 * VJ_HARNESS_ALLOW_UNKNOWN_OPTIONS is set -- the escape hatch for an A/B
 * against an older core that predates an option the tool always passes.
 * Prints the resolved option set (stderr) so a log is self-describing. */
static void validate_options(const harness_config *cfg)
{
    unsigned i, j;
    int bad = 0;
    const char *allow = getenv("VJ_HARNESS_ALLOW_UNKNOWN_OPTIONS");

    if (cfg->num_options == 0) return;
    if (reg_num_keys == 0) {
        fprintf(stderr, "harness: %s core registered no option keys, so no "
                        "--option can be validated\n",
                (allow && *allow) ? "WARNING" : "FATAL");
        if (!(allow && *allow)) exit(2);
    } else {
        for (i = 0; i < cfg->num_options; i++) {
            for (j = 0; j < reg_num_keys; j++)
                if (strcmp(cfg->options[i].key, reg_keys[j]) == 0) break;
            if (j == reg_num_keys) {
                fprintf(stderr, "harness: %s --option key '%s' is not an option "
                                "this core registers\n",
                        (allow && *allow) ? "WARNING" : "FATAL",
                        cfg->options[i].key);
                bad = 1;
            }
        }
        if (bad && !(allow && *allow)) {
            fprintf(stderr, "harness: refusing to run with an unknown option -- it "
                            "would be ignored and the run would measure the default. "
                            "Set VJ_HARNESS_ALLOW_UNKNOWN_OPTIONS=1 to override.\n");
            exit(2);
        }
    }
    /* Always, even under --quiet: several tools rewrite their own flags to
     * "--quiet" placeholders, and this goes to stderr, not the data. */
    for (i = 0; i < cfg->num_options; i++)
        fprintf(stderr, "harness: option %s=%s\n",
                cfg->options[i].key, cfg->options[i].value);
}

static bool cb_environment(unsigned cmd, void *data)
{
    reg_record(cmd, data);
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        *(struct retro_log_callback *)data = log_cb_struct;
        return true;
    case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE:
        if (!data || !active_cfg || !active_cfg->mic_tone)
            return false;
        {
            struct retro_microphone_interface *iface =
                (struct retro_microphone_interface *)data;
            unsigned want = iface->interface_version;
            *iface = g_synth_mic_iface;
            if (want && want < RETRO_MICROPHONE_INTERFACE_VERSION)
                iface->interface_version = want;
            return true;
        }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        /* Opt-in (cfg->core_options_v2), like the other answers below:
         * tools that predate it keep getting `true` WITHOUT a version
         * written, i.e. version 0, so the core registers its legacy
         * SET_VARIABLES list exactly as before.  With the flag set the
         * core registers through SET_CORE_OPTIONS_V2[_INTL], which the
         * capture above records, and may re-push definitions later. */
        if (data && active_cfg && active_cfg->core_options_v2)
            *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        /* Edge-triggered by harness_notify_variable_update(); otherwise
         * the flag stays false exactly as before this hook existed. */
        if (data) {
            *(bool *)data = variable_update_pending ? true : false;
            variable_update_pending = 0;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        /* Recorded for #841; the notice text is the contract. */
        if (data && osd_n < HARNESS_MAX_OSD) {
            const struct retro_message_ext *m =
                (const struct retro_message_ext *)data;
            osd_texts[osd_n++] = strdup(m->msg ? m->msg : "");
        }
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        set_variable_n++;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        /* Recorded for #850.  Before this the harness fell through to the
         * default (false); the core now reads that as "frontend has no
         * visibility support", so answer like a frontend that has it. */
        vis_calls++;
        if (vis_refuse) return false;
        vis_record((const struct retro_core_option_display *)data);
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
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        /* Opt-in like --av-skip-video below: tools that predate this hook
         * keep getting `false`, so the core treats the frontend as not
         * supporting buffer-status reports (auto frameskip and the
         * enhancement-profile watch then stand down, exactly as before). */
        if (!active_cfg || !active_cfg->accept_audio_buf_cb)
            return false;
        active_cfg->audio_buf_cb = data
            ? ((const struct retro_audio_buffer_status_callback *)data)->callback
            : NULL;
        return true;
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        /* Opt-in like the buffer-status hook above (#651).  Answering 0 --
         * or refusing -- would push the core onto the legacy interface,
         * which has no get_image_path/get_image_label, so a test could not
         * read back what it inserted. */
        if (!active_cfg || !active_cfg->accept_disk_control_cb)
            return false;
        if (data)
            *(unsigned *)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        if (!active_cfg || !active_cfg->accept_disk_control_cb)
            return false;
        if (data) {
            const struct retro_disk_control_ext_callback *dc =
                (const struct retro_disk_control_ext_callback *)data;
            active_cfg->disk_set_eject_state     = dc->set_eject_state;
            active_cfg->disk_get_eject_state     = dc->get_eject_state;
            active_cfg->disk_get_image_index     = dc->get_image_index;
            active_cfg->disk_set_image_index     = dc->set_image_index;
            active_cfg->disk_get_num_images      = dc->get_num_images;
            active_cfg->disk_replace_image_index = dc->replace_image_index;
            active_cfg->disk_add_image_index     = dc->add_image_index;
            active_cfg->disk_get_image_path      = dc->get_image_path;
            active_cfg->disk_get_image_label     = dc->get_image_label;
            active_cfg->disk_cb_registered       = 1;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        /* Only answer when a tool explicitly opted in (--av-skip-video);
         * otherwise fall through to `default: return false`, which is
         * exactly what every harness tool predating this option already
         * gets -- the core then assumes no step may be skipped. */
        if (active_cfg && active_cfg->av_skip_video) {
            if (data)
                *(enum retro_av_enable_flags *)data = RETRO_AV_ENABLE_AUDIO;
            return true;
        }
        return false;
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
        } else if (strcmp(argv[i], "--mic-tone") == 0) {
            cfg->mic_tone = 1;
        } else if (strcmp(argv[i], "--av-skip-video") == 0) {
            cfg->av_skip_video = 1;
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
            } else {
                fprintf(stderr, "harness: FATAL --option '%s' is not KEY=VALUE\n",
                        argv[i]);
                exit(2);
            }
        } else if (strcmp(argv[i], "--option") == 0) {
            fprintf(stderr, "harness: FATAL --option needs a KEY=VALUE argument\n");
            exit(2);
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

    reg_num_keys = 0;   /* this core's keys only, not a previous load's */
    lr_set_environment(cb_environment);
    lr_init();
    validate_options(cfg);
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

    opt_pushes_before_load = opt_pushes;
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

/* No-content boot (#646): retro_load_game(NULL).  Mirrors harness_load_rom's
 * init ordering exactly -- set_environment before retro_init, then the a/v
 * and input callbacks, then the load -- because the core registers its
 * environment-driven interfaces (including disk control, #651) during that
 * sequence, and a load with them unregistered would look like the feature
 * was missing. */
bool harness_load_no_content(harness_config *cfg)
{
    active_cfg = cfg;

    reg_num_keys = 0;   /* this core's keys only, not a previous load's */
    lr_set_environment(cb_environment);
    lr_init();
    validate_options(cfg);
    lr_set_video_refresh(cb_video);
    lr_set_audio_sample(cb_audio_sample);
    lr_set_audio_sample_batch(cb_audio_batch);
    lr_set_input_poll(cb_input_poll);
    lr_set_input_state(cb_input_state);

    opt_pushes_before_load = opt_pushes;
    if (!lr_load_game(NULL)) {
        fprintf(stderr, "harness: retro_load_game(NULL) failed "
                        "(core may not support no-content boot)\n");
        return false;
    }

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
    if (cfg->num_options >= HARNESS_MAX_OPTIONS) {
        fprintf(stderr, "harness: FATAL more than %d options; '%s' would be "
                        "dropped\n", HARNESS_MAX_OPTIONS, key);
        exit(2);
    }
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
