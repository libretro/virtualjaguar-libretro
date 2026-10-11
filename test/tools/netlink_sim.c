/* netlink_sim.c -- deterministic two-instance RetroArch-netpacket simulator
 * for the netlink reply wait (#250 JLinkAwaitReply).  Not part of `make test`.
 *
 * Why: the reply wait is wall-clock behaviour, so measuring it with two real
 * processes on a loaded host measures the host.  Here two copies of the core
 * (separate dlopen images => separate statics) run in two threads under a
 * cooperative virtual-time scheduler (only the thread with the smallest
 * virtual clock runs), the core's clock/sleep are the virtual ones, and the
 * "network" is the netpacket send/poll_receive callback pair with RetroArch's
 * semantics (netplay_frontend.c: send flushes immediately with FLUSH_HINT;
 * poll_receive is a non-blocking read that delivers reentrantly; delivery is
 * a TCP FIFO, so a delayed packet holds back everything behind it).  Results
 * depend only on the emulated machine and the link model, not on host load.
 *
 * Needs a core built with the measurement-only clock hook (NOT committed to
 * src/): python3 test/tools/netlink_sim_clockpatch.py <worktree> adds
 * JLinkSimSetClock() to src/jerry/jlink.c; then build with TEST_EXPORTS=1,
 * and copy the dylib to two names (a.dylib b.dylib) so each instance gets
 * its own statics.
 *
 *   cc -O2 -std=gnu99 -Ilibretro-common/include -o netlink_sim \
 *      test/tools/netlink_sim.c -ldl -lpthread
 *
 * Model knobs: --emu-ms E is the virtual emulation cost per frame (charged
 * 20% before retro_run, the rest inside it so the core's own frame timing
 * sees it); --phase-b-us is B's vsync offset; a frame that overruns its
 * vsync slips to the next one, like a vsync-locked frontend; --audio-block-ms
 * makes audio_batch_cb block (audio sync).  Trace columns:
 *   inst frame start_us end_us wait_us tx rx fbhash missed_vsyncs
 *
 * usage: netlink_sim --core-a A.dylib --core-b B.dylib --rom ROM
 *        --frames N [--delay-ms D] [--jitter-ms J] [--spike-pct P]
 *        [--spike-ms LO:HI] [--seed S] [--emu-ms E] [--emu-split F]
 *        [--pump-us U] [--phase-b-us X] [--audio-block-ms A] [--opt K=V]...
 *        [--press F:BTN[:HOLD] | --press0 .. | --press1 ..]...
 *        [--trace FILE] [--ev LO:HI] [--shots DIR --shot-every N]
 * Doom 2P deathmatch: press A@160, right@220, right@232, A@270 (host) and
 * A@340 (guest, ~70 frames later -- a same-frame start livelocks the
 * "attempting to connect" handshake).
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <pthread.h>
#include <libretro.h>

#define FRAME_US 16667
#define MAXPK 4096
#define MAXOPT 32
#define MAXPRESS 64

typedef struct { long long due; uint16_t from; uint16_t len; uint8_t d[1100]; } pkt_t;
typedef struct { pkt_t q[MAXPK]; int head, count; long long last_due; } box_t;

typedef struct { unsigned frame, hold; int id; } press_t;

typedef struct inst {
    int id;
    pthread_t th;
    pthread_cond_t cv;
    long long clock;
    int done;
    const char *core;
    void *h;
    /* libretro entry points */
    void (*set_env)(retro_environment_t);
    void (*set_video)(retro_video_refresh_t);
    void (*set_as)(retro_audio_sample_t);
    void (*set_asb)(retro_audio_sample_batch_t);
    void (*set_ip)(retro_input_poll_t);
    void (*set_is)(retro_input_state_t);
    void (*init)(void);
    bool (*load)(const struct retro_game_info *);
    void (*run)(void);
    void (*simclock)(long long (*)(void), void (*)(int));
    uint32_t (*txtotal)(void);
    uint32_t (*rxtotal)(void);
    struct retro_netpacket_callback np;
    int np_ok;
    box_t inbox;
    long long wait_us;
    long long emu_in;      /* emulation cost still to charge inside retro_run */
    int now_calls;
    unsigned frame;
    uint64_t fbhash;
    long long phase;
    press_t press[MAXPRESS];
    int npress;
} inst_t;

static pthread_mutex_t M = PTHREAD_MUTEX_INITIALIZER;
static int running = 0;
static inst_t inst[2];
static __thread inst_t *cur;

/* config */
static const char *rom_path;
static unsigned nframes = 600;
static double delay_ms = 0, jitter_ms = 0, spike_pct = 0;
static int spike_lo = 30, spike_hi = 80;
static unsigned rng = 1;
static double emu_ms = 2.0, emu_split = 0.2;
static int pump_us = 25;
static const char *opt_k[MAXOPT], *opt_v[MAXOPT];
static int nopt = 0;
static FILE *trace;
static unsigned ev_lo = 0, ev_hi = 0;
static double audio_block_ms = 0;
#define EV(me, ...) do { if ((me)->frame >= ev_lo && (me)->frame < ev_hi) { fprintf(stderr, "EV i%d f%u t=%.3f ", (me)->id, (me)->frame, (me)->clock/1000.0); fprintf(stderr, __VA_ARGS__); } } while (0)
static const char *shot_dir;
static unsigned shot_every;
static uint8_t *rom_data;
static size_t rom_size;

static unsigned rnd(void)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---- cooperative scheduler (M held by the running thread) ---- */
static void yield_to_min(inst_t *me)
{
    int best = -1, i;
    for (i = 0; i < 2; i++)
    {
        if (inst[i].done) continue;
        if (best < 0 || inst[i].clock < inst[best].clock
            || (inst[i].clock == inst[best].clock && i != me->id))
            best = i;
    }
    if (best < 0 || best == me->id) return;
    running = best;
    pthread_cond_signal(&inst[best].cv);
    while (running != me->id)
        pthread_cond_wait(&me->cv, &M);
}

static void yield_to_min(inst_t *me);
static void charge_in_run(inst_t *me)
{
    if (me->emu_in > 0)
    {
        me->clock += me->emu_in;
        me->emu_in = 0;
        yield_to_min(me);
    }
}
static long long sim_now(void)
{
    inst_t *me = cur;
    if (++me->now_calls >= 2) charge_in_run(me);
    return me->clock;
}
static void sim_sleep(int us)
{
    charge_in_run(cur);
    EV(cur, "SLEEP %d\n", us);
    cur->clock += us;
    cur->wait_us += us;
    yield_to_min(cur);
}

/* ---- network ---- */
static long long extra_us(void)
{
    long long x = (long long)(delay_ms * 1000.0);
    if (jitter_ms > 0)
        x += (long long)(jitter_ms * 1000.0 * (rnd() % 10001) / 10000.0);
    if (spike_pct > 0 && (rnd() % 100000) < (unsigned)(spike_pct * 1000.0))
        x += ((long long)spike_lo + (spike_hi > spike_lo ? rnd() % (unsigned)(spike_hi - spike_lo + 1) : 0)) * 1000LL;
    return x;
}

static void np_send(int flags, const void *buf, size_t len, uint16_t cid)
{
    inst_t *me = cur, *peer = &inst[1 - me->id];
    box_t *b = &peer->inbox;
    pkt_t *p;
    long long due;
    (void)flags; (void)cid;
    charge_in_run(me);
    if (!buf || !len || len > sizeof(p->d) || b->count >= MAXPK) return;
    due = me->clock + extra_us();
    EV(me, "SEND len=%zu due=%.3f\n", len, due/1000.0);
    if (due < b->last_due) due = b->last_due;   /* TCP FIFO / head-of-line */
    b->last_due = due;
    p = &b->q[(b->head + b->count) % MAXPK];
    p->due = due;
    p->from = (uint16_t)(me->id == 1 ? 0 : 1);
    p->len = (uint16_t)len;
    memcpy(p->d, buf, len);
    b->count++;
}

static void deliver_due(inst_t *me)
{
    box_t *b = &me->inbox;
    while (b->count > 0 && b->q[b->head].due <= me->clock)
    {
        pkt_t *p = &b->q[b->head];
        uint8_t tmp[1100];
        uint16_t len = p->len, from = p->from;
        memcpy(tmp, p->d, len);
        b->head = (b->head + 1) % MAXPK;
        b->count--;
        EV(me, "RECV len=%u (due %.3f)\n", len, p->due/1000.0);
        if (me->np.receive) me->np.receive(tmp, len, from);
    }
}

static void np_poll_receive(void)
{
    inst_t *me = cur;
    charge_in_run(me);
    me->clock += pump_us;
    yield_to_min(me);
    deliver_due(me);
}

/* ---- frontend stubs ---- */
static bool env_cb(unsigned cmd, void *data)
{
    inst_t *me = cur;
    int i;
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
            memcpy(&me->np, data, sizeof(me->np)); me->np_ok = 1; return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable *v = (struct retro_variable *)data;
            for (i = 0; i < nopt; i++)
                if (v->key && !strcmp(v->key, opt_k[i])) { v->value = opt_v[i]; return true; }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char **)data = "/tmp"; return true;
        default: return false;
    }
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t pitch)
{
    inst_t *me = cur;
    unsigned y;
    uint64_t hv = 1469598103934665603ULL;
    const uint8_t *s = (const uint8_t *)d;
    if (!d) return;
    for (y = 0; y < h; y++)
    {
        const uint32_t *row = (const uint32_t *)(s + y * pitch);
        unsigned x;
        for (x = 0; x < w; x += 2) { hv ^= row[x]; hv *= 1099511628211ULL; }
    }
    me->fbhash = hv;
    if (shot_dir && shot_every && me->id == 0 && (me->frame % shot_every) == 0)
    {
        char path[1024]; FILE *f;
        snprintf(path, sizeof path, "%s/f%05u.ppm", shot_dir, me->frame);
        f = fopen(path, "wb");
        if (f)
        {
            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (y = 0; y < h; y++)
            {
                unsigned x;
                for (x = 0; x < w; x++)
                {
                    uint32_t px = ((const uint32_t *)(s + y * pitch))[x];
                    fputc((px >> 16) & 255, f); fputc((px >> 8) & 255, f); fputc(px & 255, f);
                }
            }
            fclose(f);
        }
    }
}
static void as_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t asb_cb(const int16_t *d, size_t f)
{
    (void)d;
    if (audio_block_ms > 0)   /* audio sync: batch submit blocks on a full buffer */
    {
        cur->clock += (long long)(audio_block_ms * 1000.0);
        yield_to_min(cur);
    }
    return f;
}
static void ip_cb(void) {}
static int16_t is_cb(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
    inst_t *me = cur;
    int i;
    (void)idx;
    if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
    for (i = 0; i < me->npress; i++)
        if ((int)id == me->press[i].id && me->frame >= me->press[i].frame
            && me->frame < me->press[i].frame + me->press[i].hold)
            return 1;
    return 0;
}

static int btn_id(const char *t)
{
    static const struct { const char *n; int id; } m[] = {
        {"up", RETRO_DEVICE_ID_JOYPAD_UP}, {"down", RETRO_DEVICE_ID_JOYPAD_DOWN},
        {"left", RETRO_DEVICE_ID_JOYPAD_LEFT}, {"right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
        {"a", RETRO_DEVICE_ID_JOYPAD_A}, {"b", RETRO_DEVICE_ID_JOYPAD_B},
        {"c", RETRO_DEVICE_ID_JOYPAD_Y}, {"pause", RETRO_DEVICE_ID_JOYPAD_SELECT},
        {"option", RETRO_DEVICE_ID_JOYPAD_START}, {"0", RETRO_DEVICE_ID_JOYPAD_X},
        {"1", RETRO_DEVICE_ID_JOYPAD_L}, {"2", RETRO_DEVICE_ID_JOYPAD_R},
        {"3", RETRO_DEVICE_ID_JOYPAD_L2}, {"4", RETRO_DEVICE_ID_JOYPAD_R2},
        {"5", RETRO_DEVICE_ID_JOYPAD_L3}, {"6", RETRO_DEVICE_ID_JOYPAD_R3}, {0, 0}};
    int i;
    for (i = 0; m[i].n; i++) if (!strcmp(m[i].n, t)) return m[i].id;
    return -1;
}

/* ---- instance thread ---- */
static void *thread_main(void *arg)
{
    inst_t *me = (inst_t *)arg;
    struct retro_game_info game;
    long long prev_vs = -1;
    unsigned k;

    cur = me;
    pthread_mutex_lock(&M);
    while (running != me->id)
        pthread_cond_wait(&me->cv, &M);

    me->h = dlopen(me->core, RTLD_NOW | RTLD_LOCAL);
    if (!me->h) { fprintf(stderr, "dlopen %s: %s\n", me->core, dlerror()); exit(1); }
#define SYM(v, n) do { *(void **)&me->v = dlsym(me->h, n); \
    if (!me->v) { fprintf(stderr, "missing %s\n", n); exit(1); } } while (0)
    SYM(set_env, "retro_set_environment"); SYM(set_video, "retro_set_video_refresh");
    SYM(set_as, "retro_set_audio_sample"); SYM(set_asb, "retro_set_audio_sample_batch");
    SYM(set_ip, "retro_set_input_poll"); SYM(set_is, "retro_set_input_state");
    SYM(init, "retro_init"); SYM(load, "retro_load_game"); SYM(run, "retro_run");
    SYM(simclock, "JLinkSimSetClock"); SYM(txtotal, "JLinkTxTotal"); SYM(rxtotal, "JLinkRxTotal");
#undef SYM
    me->simclock(sim_now, sim_sleep);
    me->set_env(env_cb);
    me->set_video(video_cb); me->set_as(as_cb); me->set_asb(asb_cb);
    me->set_ip(ip_cb); me->set_is(is_cb);
    me->init();
    memset(&game, 0, sizeof game);
    game.path = rom_path; game.data = rom_data; game.size = rom_size;
    if (!me->load(&game)) { fprintf(stderr, "load failed\n"); exit(1); }
    if (!me->np_ok) { fprintf(stderr, "no netpacket iface\n"); exit(1); }
    me->np.start(me->id == 0 ? 0 : 1, np_send, np_poll_receive);
    me->clock = me->phase;
    yield_to_min(me);

    for (k = 0; k < nframes; k++)
    {
        long long vs, t0, t1, w0;
        unsigned missed = 0;
        uint32_t tx, rx;
        /* next vsync at or after "now" */
        vs = me->phase + ((me->clock - me->phase + FRAME_US - 1) / FRAME_US) * FRAME_US;
        if (prev_vs >= 0) missed = (unsigned)((vs - prev_vs) / FRAME_US - 1);
        prev_vs = vs;
        if (vs > me->clock) me->clock = vs;
        yield_to_min(me);
        t0 = me->clock;
        w0 = me->wait_us;
        me->frame = k;
        deliver_due(me);                 /* RA netplay_poll at frame start */
        if (me->np.poll) me->np.poll();
        me->clock += (long long)(emu_ms * 1000.0 * emu_split);
        yield_to_min(me);
        me->emu_in = (long long)(emu_ms * 1000.0 * (1.0 - emu_split));
        me->now_calls = 0;
        me->run();
        charge_in_run(me);   /* frames with no link activity at all */
        t1 = me->clock;
        tx = me->txtotal(); rx = me->rxtotal();
        if (trace)
            fprintf(trace, "%d %u %lld %lld %lld %u %u %llx %u\n", me->id, k, t0, t1,
                    me->wait_us - w0, tx, rx, (unsigned long long)me->fbhash, missed);
    }
    me->done = 1;
    {   /* hand the baton on */
        int o = 1 - me->id;
        if (!inst[o].done) { running = o; pthread_cond_signal(&inst[o].cv); }
    }
    pthread_mutex_unlock(&M);
    return NULL;
}

int main(int argc, char **argv)
{
    const char *ca = NULL, *cb = NULL, *tracef = NULL;
    long long phase_b = 5000;
    int i;
    FILE *f;
    for (i = 1; i < argc; i++)
    {
#define A(n) (!strcmp(argv[i], n) && i + 1 < argc)
        if A("--core-a") ca = argv[++i];
        else if A("--core-b") cb = argv[++i];
        else if A("--rom") rom_path = argv[++i];
        else if A("--frames") nframes = (unsigned)atoi(argv[++i]);
        else if A("--delay-ms") delay_ms = atof(argv[++i]);
        else if A("--jitter-ms") jitter_ms = atof(argv[++i]);
        else if A("--spike-pct") spike_pct = atof(argv[++i]);
        else if A("--spike-ms") { if (sscanf(argv[++i], "%d:%d", &spike_lo, &spike_hi) != 2) return 1; }
        else if A("--seed") { rng = (unsigned)atoi(argv[++i]); if (!rng) rng = 1; }
        else if A("--emu-ms") emu_ms = atof(argv[++i]);
        else if A("--emu-split") emu_split = atof(argv[++i]);
        else if A("--audio-block-ms") audio_block_ms = atof(argv[++i]);
        else if A("--pump-us") pump_us = atoi(argv[++i]);
        else if A("--phase-b-us") phase_b = atoll(argv[++i]);
        else if A("--trace") tracef = argv[++i];
        else if A("--ev") { if (sscanf(argv[++i], "%u:%u", &ev_lo, &ev_hi) != 2) return 1; }
        else if A("--shots") shot_dir = argv[++i];
        else if A("--shot-every") shot_every = (unsigned)atoi(argv[++i]);
        else if A("--opt")
        {
            char *e = strchr(argv[++i], '=');
            if (!e || nopt >= MAXOPT) return 1;
            *e = 0; opt_k[nopt] = argv[i]; opt_v[nopt++] = e + 1;
        }
        else if (A("--press") || A("--press0") || A("--press1"))
        {
            unsigned fr, hold = 10; char b[32]; int id, n, lo = 0, hi = 2;
            if (argv[i][7] == '0') hi = 1;
            if (argv[i][7] == '1') lo = 1;
            n = sscanf(argv[++i], "%u:%31[^:]:%u", &fr, b, &hold);
            if (n < 2 || (id = btn_id(b)) < 0) { fprintf(stderr, "bad press\n"); return 1; }
            for (n = lo; n < hi; n++)
            {
                press_t *p = &inst[n].press[inst[n].npress++];
                p->frame = fr; p->hold = hold; p->id = id;
            }
        }
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 1; }
    }
    if (!ca || !cb || !rom_path) { fprintf(stderr, "need --core-a --core-b --rom\n"); return 1; }
    f = fopen(rom_path, "rb");
    if (!f) { perror("rom"); return 1; }
    fseek(f, 0, SEEK_END); rom_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom_data = (uint8_t *)malloc(rom_size);
    if (fread(rom_data, 1, rom_size, f) != rom_size) return 1;
    fclose(f);
    if (tracef) trace = fopen(tracef, "w");

    for (i = 0; i < 2; i++)
    {
        inst[i].id = i;
        inst[i].core = i ? cb : ca;
        inst[i].phase = i ? phase_b : 0;
        pthread_cond_init(&inst[i].cv, NULL);
    }
    running = 0;
    for (i = 0; i < 2; i++)
        pthread_create(&inst[i].th, NULL, thread_main, &inst[i]);
    for (i = 0; i < 2; i++)
        pthread_join(inst[i].th, NULL);
    if (trace) fclose(trace);
    return 0;
}
