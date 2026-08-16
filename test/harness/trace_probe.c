/*
 * test/harness/trace_probe.c — shared vjtrace flight-recorder probe.
 * See trace_probe.h for the flag list, the CSV column contract and the
 * flag-name collision warning.
 */

#include "trace_probe.h"
#include "../../src/core/vjtrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* who codes from src/core/vjag_memory.h:
 *   UNKNOWN JAGUAR DSP GPU TOM JERRY M68K BLITTER OP DEBUG
 * DEBUG (9) marks a host-injected event (input edges, marks). */
#define TP_WHO_DEBUG 9

#define TP_MAX_SNAPS  HARNESS_MAX_SNAP_FRAMES
#define TP_MAX_MARKS  HARNESS_MAX_MARK_SPECS

/* IRQ sources, per VJT_EV_IRQ_ASSERT's addr field. */
enum { TP_IRQ_VIDEO = 0, TP_IRQ_GPU, TP_IRQ_OBJ, TP_IRQ_TIMER, TP_IRQ_JERRY,
       TP_IRQ_NSRC };

typedef struct {
    unsigned    frame;
    const char *tag;
} tp_mark;

struct trace_probe {
    int  attached;

    /* Resolved core exports */
    void     (*p_arm)(void);
    void     (*p_emit)(uint8_t, uint8_t, uint32_t, uint32_t);
    int      (*p_watch_add)(uint32_t, uint32_t, unsigned);
    int      (*p_dump)(const char *);
    int      (*p_snapshot)(const char *);
    uint64_t (*p_ring_head)(void);
    int      (*p_ring_read)(uint64_t, vjtrace_ev *);
    vjtrace_counters_t *p_counters;

    /* Field CSV */
    FILE     *csv;
    unsigned  csv_rows;

    /* Ring drain cursor + eviction accounting */
    uint64_t  last_head;
    uint64_t  evicted;

    /* Previous frame's cumulative counters (for deltas) */
    uint64_t  prev_ev[VJT_EV__COUNT];

    /* Input edge tracking */
    uint32_t  prev_pad0;
    int       have_prev_pad0;

    /* Scheduled work */
    unsigned  snap_frames[TP_MAX_SNAPS];
    unsigned  num_snaps;
    unsigned  snaps_written;
    tp_mark   marks[TP_MAX_MARKS];
    unsigned  num_marks;

    unsigned  watches_added;

    /* Chained frame callback the tool had installed before attach */
    harness_frame_cb chain_cb;
    void            *chain_ud;
};

static struct trace_probe g_tp;

/* ----------------------------------------------------------------
 * Flag parsing
 * ---------------------------------------------------------------- */

/* Accepts "$1F000", "0x1f000", "1f000"? -- no: a bare token goes to
 * strtoul base 0, so it is decimal (and a leading 0 is octal).  Hex
 * therefore needs an explicit 0x or $ prefix.  Returns 1 on a fully
 * consumed, non-empty token. */
static int tp_parse_u32(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v;
    if (!s || !*s) return 0;
    if (*s == '$') {
        s++;
        if (!*s) return 0;
        v = strtoul(s, &end, 16);
    } else {
        v = strtoul(s, &end, 0);
    }
    if (*end != '\0') return 0;
    *out = (uint32_t)v;
    return 1;
}

/* "ADDR[:LEN][:r|w|rw]" -> vjtrace_watch_add(lo, hi, rw). */
static int tp_parse_watch(struct trace_probe *tp, const char *spec)
{
    char buf[128];
    char *len_s, *mode_s;
    uint32_t addr = 0, len = 4;
    unsigned rw = 2;   /* default: writes */

    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    len_s = strchr(buf, ':');
    if (len_s) {
        *len_s++ = '\0';
        mode_s = strchr(len_s, ':');
        if (mode_s) *mode_s++ = '\0';
    } else {
        mode_s = NULL;
    }

    if (!tp_parse_u32(buf, &addr)) {
        fprintf(stderr, "trace_probe: bad --watch address in '%s' "
                        "(want ADDR[:LEN][:r|w|rw], hex needs 0x or $)\n", spec);
        return 0;
    }
    if (len_s && *len_s) {
        /* "ADDR::rw" leaves an empty LEN field -- keep the default. */
        if (!tp_parse_u32(len_s, &len) || len == 0) {
            /* A non-numeric second field is a mode written without a
             * length, e.g. "--watch 0x100:w". */
            mode_s = len_s;
            len = 4;
        }
    }
    if (mode_s && *mode_s) {
        if      (strcmp(mode_s, "r")  == 0) rw = 1;
        else if (strcmp(mode_s, "w")  == 0) rw = 2;
        else if (strcmp(mode_s, "rw") == 0) rw = 3;
        else if (strcmp(mode_s, "wr") == 0) rw = 3;
        else {
            fprintf(stderr, "trace_probe: bad --watch mode '%s' in '%s' "
                            "(want r, w or rw)\n", mode_s, spec);
            return 0;
        }
    }

    if (len - 1 > 0xFFFFFFFFu - addr) {
        fprintf(stderr, "trace_probe: --watch '%s' wraps past $FFFFFFFF\n", spec);
        return 0;
    }

    if (tp->p_watch_add(addr, addr + len - 1, rw) < 0) {
        fprintf(stderr, "trace_probe: core refused watch '%s' (max 16)\n", spec);
        return 0;
    }
    tp->watches_added++;
    return 1;
}

static int tp_parse_mark(struct trace_probe *tp, const char *spec)
{
    const char *colon = strchr(spec, ':');
    uint32_t frame = 0;
    char fbuf[32];
    size_t flen;

    if (!colon || colon == spec || !colon[1]) {
        fprintf(stderr, "trace_probe: bad --mark '%s' (want FRAME:TAG)\n", spec);
        return 0;
    }
    flen = (size_t)(colon - spec);
    if (flen >= sizeof(fbuf)) flen = sizeof(fbuf) - 1;
    memcpy(fbuf, spec, flen);
    fbuf[flen] = '\0';
    if (!tp_parse_u32(fbuf, &frame)) {
        fprintf(stderr, "trace_probe: bad --mark frame in '%s'\n", spec);
        return 0;
    }
    if (tp->num_marks >= TP_MAX_MARKS) {
        fprintf(stderr, "trace_probe: too many --mark (max %d)\n", TP_MAX_MARKS);
        return 0;
    }
    tp->marks[tp->num_marks].frame = frame;
    tp->marks[tp->num_marks].tag   = colon + 1;
    tp->num_marks++;
    return 1;
}

/* ----------------------------------------------------------------
 * Attach
 * ---------------------------------------------------------------- */

static bool tp_frame_cb(void *userdata, unsigned frame)
{
    harness_config *cfg = (harness_config *)userdata;
    trace_probe_frame(cfg, (int)frame);
    if (g_tp.chain_cb)
        return g_tp.chain_cb(g_tp.chain_ud, frame);
    return true;
}

static void *tp_need(harness_config *cfg, const char *name, int *missing)
{
    void *sym = harness_dlsym(cfg, name);
    if (!sym) {
        fprintf(stderr, "trace_probe: FATAL core does not export '%s'.\n", name);
        (*missing)++;
    }
    return sym;
}

int trace_probe_attach(harness_config *cfg)
{
    struct trace_probe *tp = &g_tp;
    int missing = 0;
    unsigned i;

    memset(tp, 0, sizeof(*tp));

    if (!cfg->trace_out_path && !cfg->field_csv_path &&
        cfg->num_watch_specs == 0 && cfg->num_snap_specs == 0 &&
        cfg->num_mark_specs == 0)
        return 1;   /* nothing requested -- stay out of the way entirely */

    /* All of these ship together under the vjtrace_* export wildcard, so
     * resolve the whole set and fail on any gap: a partial set means the
     * core under test is not the one this probe was written against.
     * vjtrace_arm is part of this required set -- an older core that
     * predates the armed gate would otherwise leave every hot-path
     * recording site permanently disarmed, silently producing an empty
     * ring instead of the trace this call is being asked for. */
    tp->p_arm       = (void (*)(void))
                      tp_need(cfg, "vjtrace_arm", &missing);
    tp->p_emit      = (void (*)(uint8_t, uint8_t, uint32_t, uint32_t))
                      tp_need(cfg, "vjtrace_emit", &missing);
    tp->p_watch_add = (int (*)(uint32_t, uint32_t, unsigned))
                      tp_need(cfg, "vjtrace_watch_add", &missing);
    tp->p_dump      = (int (*)(const char *))
                      tp_need(cfg, "vjtrace_dump", &missing);
    tp->p_snapshot  = (int (*)(const char *))
                      tp_need(cfg, "vjtrace_snapshot", &missing);
    tp->p_ring_head = (uint64_t (*)(void))
                      tp_need(cfg, "vjtrace_ring_head", &missing);
    tp->p_ring_read = (int (*)(uint64_t, vjtrace_ev *))
                      tp_need(cfg, "vjtrace_ring_read", &missing);
    tp->p_counters  = (vjtrace_counters_t *)
                      tp_need(cfg, "vjtrace_counters", &missing);

    if (missing) {
        fprintf(stderr,
                "trace_probe: %d vjtrace symbol(s) missing from '%s' -- this is "
                "a production build.  Rebuild with `make TEST_EXPORTS=1`.\n",
                missing, cfg->core_path ? cfg->core_path : "(core)");
        exit(2);
    }

    /* Arm before anything else can run: every hot-path recording site
     * defaults to disarmed (see vjtrace.h's PERFORMANCE NOTE), and this
     * call is the ONLY place that arms it for a CLI-driven tool -- so it
     * must happen before the frame loop starts, and before the watch
     * specs below install anything a disarmed vjtrace_emit() would
     * otherwise drop. */
    tp->p_arm();

    for (i = 0; i < cfg->num_watch_specs; i++)
        if (!tp_parse_watch(tp, cfg->watch_specs[i]))
            return 0;

    for (i = 0; i < cfg->num_snap_specs; i++) {
        uint32_t f;
        if (!tp_parse_u32(cfg->snap_specs[i], &f)) {
            fprintf(stderr, "trace_probe: bad --snap frame '%s'\n",
                    cfg->snap_specs[i]);
            return 0;
        }
        if (tp->num_snaps < TP_MAX_SNAPS)
            tp->snap_frames[tp->num_snaps++] = (unsigned)f;
    }

    for (i = 0; i < cfg->num_mark_specs; i++)
        if (!tp_parse_mark(tp, cfg->mark_specs[i]))
            return 0;

    if (cfg->field_csv_path) {
        tp->csv = fopen(cfg->field_csv_path, "w");
        if (!tp->csv) {
            fprintf(stderr, "trace_probe: cannot open --field-csv '%s'\n",
                    cfg->field_csv_path);
            return 0;
        }
        fprintf(tp->csv,
                "frame,pad0,irq_video,irq_gpu,irq_obj,irq_timer,irq_jerry,"
                "irq_dispatch,gpu_go,gpu_stop,op_list_start,op_obj,op_gpu_obj,"
                "op_branch,blit_cmd,watch_rd,watch_wr,fb_hash\n");
        cfg->want_fb_hash = 1;
    }

    /* Baseline the counters and the ring cursor so the first row is a
     * delta over the attach point, not over core boot. */
    memcpy(tp->prev_ev, tp->p_counters->ev, sizeof(tp->prev_ev));
    tp->last_head = tp->p_ring_head();

    tp->chain_cb = cfg->frame_callback;
    tp->chain_ud = cfg->frame_callback_data;
    cfg->frame_callback      = tp_frame_cb;
    cfg->frame_callback_data = cfg;

    tp->attached = 1;
    return 1;
}

/* ----------------------------------------------------------------
 * Per-frame
 * ---------------------------------------------------------------- */

void trace_probe_frame(harness_config *cfg, int frame)
{
    struct trace_probe *tp = &g_tp;
    uint64_t irq[TP_IRQ_NSRC];
    uint64_t head, idx;
    uint64_t d[VJT_EV__COUNT];
    uint32_t pad0;
    unsigned i;

    if (!tp->attached) return;

    /* --mark / --snap for the field that just completed. */
    for (i = 0; i < tp->num_marks; i++) {
        if (tp->marks[i].frame == (unsigned)frame) {
            const char *t = tp->marks[i].tag;
            size_t tlen = strlen(t);
            uint32_t packed = 0;
            unsigned c;
            for (c = 0; c < 4; c++)
                packed |= (uint32_t)(c < tlen ? (unsigned char)t[c] : 0)
                          << (24 - 8 * c);
            tp->p_emit((uint8_t)VJT_EV_MARK, (uint8_t)TP_WHO_DEBUG,
                       (uint32_t)tlen, packed);
        }
    }
    for (i = 0; i < tp->num_snaps; i++) {
        if (tp->snap_frames[i] == (unsigned)frame) {
            char path[512];
            snprintf(path, sizeof(path), "%s_f%06u.vjsn",
                     cfg->snap_prefix ? cfg->snap_prefix : "vjt_snap",
                     (unsigned)frame);
            if (tp->p_snapshot(path) != 0)
                fprintf(stderr, "trace_probe: snapshot '%s' failed\n", path);
            else
                tp->snaps_written++;
        }
    }

    /* Host input edge: what the harness injected on port 0 this frame. */
    pad0 = harness_input_mask(cfg, 0);
    if (!tp->have_prev_pad0 || pad0 != tp->prev_pad0) {
        if (tp->have_prev_pad0 || pad0 != 0)
            tp->p_emit((uint8_t)VJT_EV_INPUT_EDGE, (uint8_t)TP_WHO_DEBUG,
                       0, pad0);
        tp->prev_pad0 = pad0;
        tp->have_prev_pad0 = 1;
    }

    /* Per-source IRQ split: only available from the events themselves. */
    memset(irq, 0, sizeof(irq));
    head = tp->p_ring_head();
    for (idx = tp->last_head; idx < head; idx++) {
        vjtrace_ev ev;
        if (!tp->p_ring_read(idx, &ev)) {
            tp->evicted++;
            continue;
        }
        if (ev.type == VJT_EV_IRQ_ASSERT && ev.addr < TP_IRQ_NSRC)
            irq[ev.addr]++;
    }
    tp->last_head = head;

    /* Everything else is a counter delta -- eviction-proof. */
    for (i = 0; i < VJT_EV__COUNT; i++) {
        d[i] = tp->p_counters->ev[i] - tp->prev_ev[i];
        tp->prev_ev[i] = tp->p_counters->ev[i];
    }

    if (tp->csv) {
        fprintf(tp->csv,
                "%d,0x%04X,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
                "%llu,%llu,%llu,%llu,%llu,0x%08X\n",
                frame, (unsigned)pad0,
                (unsigned long long)irq[TP_IRQ_VIDEO],
                (unsigned long long)irq[TP_IRQ_GPU],
                (unsigned long long)irq[TP_IRQ_OBJ],
                (unsigned long long)irq[TP_IRQ_TIMER],
                (unsigned long long)irq[TP_IRQ_JERRY],
                (unsigned long long)d[VJT_EV_IRQ_DISPATCH],
                (unsigned long long)d[VJT_EV_GPU_GO],
                (unsigned long long)d[VJT_EV_GPU_STOP],
                (unsigned long long)d[VJT_EV_OP_LIST_START],
                (unsigned long long)d[VJT_EV_OP_OBJECT],
                (unsigned long long)d[VJT_EV_OP_GPU_OBJ],
                (unsigned long long)d[VJT_EV_OP_BRANCH],
                (unsigned long long)d[VJT_EV_BLIT_CMD],
                (unsigned long long)d[VJT_EV_WATCH_RD],
                (unsigned long long)d[VJT_EV_WATCH_WR],
                (unsigned)cfg->last_fb_hash);
        tp->csv_rows++;
    }
}

/* ----------------------------------------------------------------
 * Finish
 * ---------------------------------------------------------------- */

void trace_probe_finish(harness_config *cfg)
{
    struct trace_probe *tp = &g_tp;

    if (!tp->attached) return;

    if (tp->csv) {
        fclose(tp->csv);
        tp->csv = NULL;
        if (tp->csv_rows == 0)
            fprintf(stderr,
                    "trace_probe: WARNING --field-csv '%s' has no rows -- the "
                    "frame hook never ran.  A tool that sets cfg->frame_callback "
                    "AFTER trace_probe_attach() displaces the probe (set it "
                    "before attaching), and harness_step() does not invoke the "
                    "frame callback at all -- either way, call "
                    "trace_probe_frame() from your own loop.\n",
                    cfg->field_csv_path);
        else if (!cfg->quiet)
            fprintf(stderr, "trace_probe: wrote %u field rows to '%s'\n",
                    tp->csv_rows, cfg->field_csv_path);
    }

    if (cfg->trace_out_path) {
        if (tp->p_dump(cfg->trace_out_path) != 0)
            fprintf(stderr, "trace_probe: ring dump to '%s' failed\n",
                    cfg->trace_out_path);
        else if (!cfg->quiet)
            fprintf(stderr, "trace_probe: wrote event ring to '%s'\n",
                    cfg->trace_out_path);
    }

    if (tp->evicted)
        fprintf(stderr,
                "trace_probe: WARNING %llu ring events were evicted before the "
                "per-frame drain could read them -- the irq_* CSV columns are "
                "undercounted (every other column is a counter delta and is "
                "unaffected).  Raise the ring with VJ_TRACE_RING=<events>.\n",
                (unsigned long long)tp->evicted);

    if (!cfg->quiet && (tp->watches_added || tp->snaps_written))
        fprintf(stderr, "trace_probe: %u watch(es), %u snapshot(s) written\n",
                tp->watches_added, tp->snaps_written);
}
