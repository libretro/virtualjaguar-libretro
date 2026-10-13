/*
 * test/test_frontend_ext.c -- the core's private frontend channel
 * (src/core/vj_frontend_ext.h) and the free-form clock-scale values.
 *
 * Part 1: the event channel's opt-in contract.
 *   A) a frontend that refuses VJ_ENV_HELLO (RetroArch and every other
 *      frontend): the core asks exactly once per session, then never sends
 *      VJ_ENV_EVENT -- not for a link coming up, not at all -- and runs fine.
 *   B) a frontend that answers: ABI 1, a version string, and a LINK_UP event
 *      (loopback) with the device and mode in it.
 *   C) answers true but reports no capability bits: still silent.
 *   D) refuse again after B in the same process (the iOS static-state rule:
 *      retro_deinit() drops the latch): silent again.
 *
 * Part 2: m68k/risc clock-scale parsing.  The listed values map exactly as
 * before; "175" / "175%" are an extra accepted form clamped to 25..400;
 * everything else the old ladder did not know stays 100.
 *
 * Needs a TEST_EXPORTS=1 core (m68kClockScalePct / riscClockScalePct).
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_frontend_ext \
 *           test/test_frontend_ext.c test/harness/harness.c -ldl -lm
 * Run:    ./test/test_frontend_ext ./virtualjaguar_libretro.dylib test/roms/yarc.j64
 */
#include "harness/harness.h"
#include "vj_frontend_ext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fail++;                                                          \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

/* ---- the fake frontend ------------------------------------------------- */
static int g_answer_hello;       /* 0 refuse, 1 answer with caps, 2 answer with no caps */
static unsigned g_hello_calls;
static unsigned g_event_calls;
static unsigned g_hello_abi;
static char g_hello_version[64];
static unsigned g_link_up_events;
static unsigned g_link_up_device = 99;
static char g_link_up_desc[64];

static bool env_hook(unsigned cmd, void *data)
{
    if (cmd == VJ_ENV_HELLO) {
        struct vj_env_hello *h = (struct vj_env_hello *)data;
        g_hello_calls++;
        if (h) {
            g_hello_abi = h->abi;
            snprintf(g_hello_version, sizeof g_hello_version, "%s",
                     h->core_version ? h->core_version : "(null)");
        }
        if (!g_answer_hello || !h)
            return false;
        h->caps_out = (g_answer_hello == 1) ? VJ_CAP_EVENTS : 0;
        return true;
    }
    if (cmd == VJ_ENV_EVENT) {
        const struct vj_event *e = (const struct vj_event *)data;
        g_event_calls++;
        if (e && e->type == VJ_EVT_LINK_UP) {
            g_link_up_events++;
            g_link_up_device = e->a;
            snprintf(g_link_up_desc, sizeof g_link_up_desc, "%s", e->s ? e->s : "(null)");
        }
        return true;
    }
    return false;
}

static void set_cfg_option(harness_config *cfg, const char *key, const char *value)
{
    unsigned i;
    for (i = 0; i < cfg->num_options; i++)
        if (strcmp(cfg->options[i].key, key) == 0) {
            cfg->options[i].value = value;
            return;
        }
    harness_set_option(cfg, key, value);
}

static int start(harness_config *cfg, int argc, char **argv, int answer, const char *netlink)
{
    harness_config fresh = HARNESS_CONFIG_DEFAULT;
    *cfg = fresh;
    cfg->frames = 1;
    cfg->quiet = 1;
    g_answer_hello = answer;
    g_hello_calls = g_event_calls = g_link_up_events = 0;
    g_link_up_device = 99;
    g_link_up_desc[0] = '\0';
    harness_set_env_hook(env_hook);
    if (!harness_init_from_args(cfg, argc, argv))
        return 0;
    if (netlink)
        harness_set_option(cfg, "virtualjaguar_netlink", netlink);
    return harness_load_rom(cfg) ? 1 : 0;
}

static void run_frames(harness_config *cfg, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        harness_step(cfg);
}

static void part1(int argc, char **argv)
{
    harness_config cfg;

    /* A: refused. */
    CHECK(start(&cfg, argc, argv, 0, "loopback"));
    run_frames(&cfg, 120);
    CHECK(g_hello_calls == 1);
    CHECK(g_event_calls == 0);
    CHECK(cfg.current_frame >= 120);   /* and it ran */
    harness_shutdown(&cfg);

    /* B: answered. */
    CHECK(start(&cfg, argc, argv, 1, "loopback"));
    run_frames(&cfg, 120);
    CHECK(g_hello_calls == 1);
    CHECK(g_hello_abi == VJ_FRONTEND_EXT_ABI);
    CHECK(g_hello_version[0] != '\0' && strcmp(g_hello_version, "(null)") != 0);
    CHECK(g_link_up_events >= 1);
    CHECK(g_link_up_device == 0);                       /* JagLink */
    CHECK(strstr(g_link_up_desc, "loopback") != NULL);
    CHECK(g_event_calls >= 1);
    harness_shutdown(&cfg);

    /* C: answers but claims no events. */
    CHECK(start(&cfg, argc, argv, 2, "loopback"));
    run_frames(&cfg, 120);
    CHECK(g_hello_calls == 1);
    CHECK(g_event_calls == 0);
    harness_shutdown(&cfg);

    /* D: refused again after having been answered. */
    CHECK(start(&cfg, argc, argv, 0, "loopback"));
    run_frames(&cfg, 120);
    CHECK(g_hello_calls == 1);
    CHECK(g_event_calls == 0);
    harness_shutdown(&cfg);

    /* E: no link at all (the default) -> nothing to say even to a listening frontend. */
    CHECK(start(&cfg, argc, argv, 1, NULL));
    run_frames(&cfg, 60);
    CHECK(g_link_up_events == 0);
    harness_shutdown(&cfg);
    harness_set_env_hook(NULL);
}

struct row {
    const char *value;
    unsigned m68k_want;
    unsigned risc_want;
};

static const struct row rows[] = {
    /* the listed values, exactly as the old ladder had them */
    { "0.5x", 50, 50 },
    { "1x",   100, 100 },
    { "1.5x", 150, 150 },
    { "2x",   200, 200 },
    { "3x",   300, 100 },   /* risc never had a 3x: unknown -> stock */
    /* the new form */
    { "175%", 175, 175 },
    { "175",  175, 175 },
    { "25",   25, 25 },
    { "10",   25, 25 },      /* clamped up */
    { "400%", 400, 400 },
    { "999",  400, 400 },    /* clamped down */
    /* everything else stays stock */
    { "",      100, 100 },
    { "abc",   100, 100 },
    { "175x",  100, 100 },
    { "1.75",  100, 100 },
    { "-50",   100, 100 },
    { "+50",   100, 100 },
    { " 50",   100, 100 },
    { "50 ",   100, 100 },
    { "%",     100, 100 },
    { "12345", 100, 100 },   /* more than 4 digits */
    { "2X",    100, 100 },
};

static void part2(int argc, char **argv)
{
    harness_config cfg;
    volatile uint32_t *m68k, *risc;
    unsigned i;

    CHECK(start(&cfg, argc, argv, 0, NULL));
    m68k = (volatile uint32_t *)harness_dlsym(&cfg, "m68kClockScalePct");
    risc = (volatile uint32_t *)harness_dlsym(&cfg, "riscClockScalePct");
    CHECK(m68k && risc);
    if (!m68k || !risc) {
        harness_shutdown(&cfg);
        return;
    }
    run_frames(&cfg, 2);
    CHECK(*m68k == 100 && *risc == 100);   /* stock by default */

    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        set_cfg_option(&cfg, "virtualjaguar_m68k_clock_scale", rows[i].value);
        set_cfg_option(&cfg, "virtualjaguar_risc_clock_scale", rows[i].value);
        harness_notify_variable_update();
        run_frames(&cfg, 2);
        if (*m68k != rows[i].m68k_want || *risc != rows[i].risc_want) {
            fprintf(stderr, "  value '%s': m68k %u (want %u), risc %u (want %u)\n", rows[i].value,
                    (unsigned)*m68k, rows[i].m68k_want, (unsigned)*risc, rows[i].risc_want);
        }
        CHECK(*m68k == rows[i].m68k_want);
        CHECK(*risc == rows[i].risc_want);
    }
    harness_shutdown(&cfg);
}

int main(int argc, char **argv)
{
    part1(argc, argv);
    part2(argc, argv);
    printf("frontend_ext: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
