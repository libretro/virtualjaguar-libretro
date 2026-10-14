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
 * Part 3: the BIOS queries (VJ_CAP_BIOS): which BIOS file to use, and what a
 *   no-content boot enters.  A frontend that refuses HELLO (RetroArch) is never
 *   asked; one that answers without VJ_CAP_BIOS is never asked either; one that
 *   names a file gets that image staged (and told so with VJ_EVT_BIOS_STAGED);
 *   a file the core cannot accept is refused with VJ_EVT_BIOS_REJECTED and the
 *   core's own search takes over.
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
#include <sys/stat.h>
#include <unistd.h>

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
/* Part 3 */
static const char *g_bios_path[3];       /* by role; NULL = no preference */
static unsigned g_boot_target;
static unsigned g_path_calls[3], g_target_calls;
static unsigned g_staged_n[3], g_staged_src[3], g_staged_crc[3], g_rejected_n[3];
static char g_staged_s[3][512];

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
        h->caps_out = (g_answer_hello == 1) ? VJ_CAP_EVENTS
                    : (g_answer_hello == 3) ? (VJ_CAP_EVENTS | VJ_CAP_BIOS) : 0;
        return true;
    }
    if (cmd == VJ_ENV_GET_BIOS_PATH) {
        struct vj_env_bios_path *q = (struct vj_env_bios_path *)data;
        if (!q || q->role < 1 || q->role > 2)
            return false;
        g_path_calls[q->role]++;
        if (!g_bios_path[q->role])
            return false;
        q->path = g_bios_path[q->role];
        return true;
    }
    if (cmd == VJ_ENV_GET_BOOT_TARGET) {
        g_target_calls++;
        *(unsigned *)data = g_boot_target;
        return true;
    }
    if (cmd == VJ_ENV_EVENT) {
        const struct vj_event *e = (const struct vj_event *)data;
        g_event_calls++;
        if (e && e->type == VJ_EVT_BIOS_STAGED && e->a >= 1 && e->a <= 2) {
            g_staged_n[e->a]++;
            g_staged_src[e->a] = e->c;
            g_staged_crc[e->a] = e->b;
            snprintf(g_staged_s[e->a], sizeof g_staged_s[e->a], "%s", e->s ? e->s : "(null)");
        }
        if (e && e->type == VJ_EVT_BIOS_REJECTED && e->a >= 1 && e->a <= 2)
            g_rejected_n[e->a]++;
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


/* ---- Part 3: BIOS queries --------------------------------------------------- */
static unsigned crc32_of(const unsigned char *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* Write n bytes of a recognisable pattern; the CD variant carries a plausible run address at $404. */
static unsigned write_image(const char *path, size_t n, int cd)
{
    unsigned char *b = (unsigned char *)malloc(n);
    unsigned crc;
    size_t i;
    FILE *f;
    for (i = 0; i < n; i++)
        b[i] = (unsigned char)((i * 7u + 13u) ^ (i >> 8));
    if (cd) {
        b[0x404] = 0x00; b[0x405] = 0x80; b[0x406] = 0x20; b[0x407] = 0x00;
    }
    f = fopen(path, "wb");
    if (f) { fwrite(b, 1, n, f); fclose(f); }
    crc = crc32_of(b, n);
    free(b);
    return crc;
}

static void reset_part3(void)
{
    memset(g_bios_path, 0, sizeof g_bios_path);
    memset(g_path_calls, 0, sizeof g_path_calls);
    memset(g_staged_n, 0, sizeof g_staged_n);
    memset(g_staged_src, 0xFF, sizeof g_staged_src);
    memset(g_staged_crc, 0, sizeof g_staged_crc);
    memset(g_rejected_n, 0, sizeof g_rejected_n);
    memset(g_staged_s, 0, sizeof g_staged_s);
    g_boot_target = 0;
    g_target_calls = 0;
}

static int start_nocontent(harness_config *cfg, int argc, char **argv, int answer)
{
    harness_config fresh = HARNESS_CONFIG_DEFAULT;
    *cfg = fresh;
    cfg->frames = 1;
    cfg->quiet = 1;
    g_answer_hello = answer;
    g_hello_calls = g_event_calls = 0;
    harness_set_env_hook(env_hook);
    if (!harness_init_from_args(cfg, argc, argv))
        return 0;
    return harness_load_no_content(cfg) ? 1 : 0;
}

static void part3(int argc, char **argv)
{
    harness_config cfg;
    char dir[512], cdp[600], cartp[600], badp[600];
    unsigned cdcrc, cartcrc;
    const char *tmp = getenv("TMPDIR");

    snprintf(dir, sizeof dir, "%s/vjbios_%d", (tmp && tmp[0]) ? tmp : "/tmp", (int)getpid());
    mkdir(dir, 0700);
    snprintf(cdp, sizeof cdp, "%s/cd.bin", dir);
    snprintf(cartp, sizeof cartp, "%s/boot.rom", dir);
    snprintf(badp, sizeof badp, "%s/short.bin", dir);
    cdcrc = write_image(cdp, 0x40000, 1);
    cartcrc = write_image(cartp, 0x20000, 0);
    write_image(badp, 1000, 0);

    /* A: refusing frontend (RetroArch): never asked anything, and no event. */
    reset_part3();
    g_bios_path[2] = cdp; g_bios_path[1] = cartp; g_boot_target = 1;
    CHECK(start_nocontent(&cfg, argc, argv, 0));
    run_frames(&cfg, 30);
    CHECK(g_path_calls[1] == 0 && g_path_calls[2] == 0 && g_target_calls == 0);
    CHECK(g_staged_n[1] == 0 && g_staged_n[2] == 0);
    harness_shutdown(&cfg);

    /* B: answers with events but not VJ_CAP_BIOS: still never asked; events say built-in. */
    reset_part3();
    g_bios_path[2] = cdp; g_boot_target = 1;
    CHECK(start_nocontent(&cfg, argc, argv, 1));
    run_frames(&cfg, 30);
    CHECK(g_path_calls[1] == 0 && g_path_calls[2] == 0 && g_target_calls == 0);
    CHECK(g_staged_n[2] == 1 && g_staged_src[2] == VJ_BIOS_SRC_BUILTIN);
    CHECK(g_staged_crc[2] == 0x687068D5u);            /* the built-in retail CD BIOS */
    harness_shutdown(&cfg);

    /* C: BIOS cap, no preference: the default no-content boot is the CD BIOS. */
    reset_part3();
    CHECK(start_nocontent(&cfg, argc, argv, 3));
    run_frames(&cfg, 30);
    CHECK(g_target_calls == 1 && g_path_calls[2] == 1);
    CHECK(g_staged_n[2] == 1 && g_staged_src[2] == VJ_BIOS_SRC_BUILTIN && g_staged_n[1] == 1);
    harness_shutdown(&cfg);

    /* D: a CD BIOS file named by the frontend is the one staged. */
    reset_part3();
    g_bios_path[2] = cdp;
    CHECK(start_nocontent(&cfg, argc, argv, 3));
    run_frames(&cfg, 30);
    CHECK(g_staged_n[2] == 1 && g_staged_src[2] == VJ_BIOS_SRC_FRONTEND);
    CHECK(g_staged_crc[2] == cdcrc);
    CHECK(strcmp(g_staged_s[2], cdp) == 0);
    CHECK(g_rejected_n[2] == 0);
    harness_shutdown(&cfg);

    /* E: a file the core cannot accept is reported and the built-in image takes over. */
    reset_part3();
    g_bios_path[2] = badp;
    CHECK(start_nocontent(&cfg, argc, argv, 3));
    run_frames(&cfg, 30);
    CHECK(g_rejected_n[2] == 1);
    CHECK(g_staged_n[2] == 1 && g_staged_src[2] == VJ_BIOS_SRC_BUILTIN);
    harness_shutdown(&cfg);
    reset_part3();
    g_bios_path[2] = "/nonexistent/cd.bin";
    CHECK(start_nocontent(&cfg, argc, argv, 3));
    run_frames(&cfg, 30);
    CHECK(g_rejected_n[2] == 1 && g_staged_src[2] == VJ_BIOS_SRC_BUILTIN);
    harness_shutdown(&cfg);

    /* F: boot target = cartridge boot ROM: the cart ROM is staged (the named file), the CD BIOS is not. */
    reset_part3();
    g_boot_target = VJ_BOOT_CART_BOOT_ROM;
    g_bios_path[1] = cartp;
    CHECK(start_nocontent(&cfg, argc, argv, 3));
    run_frames(&cfg, 60);
    CHECK(g_staged_n[1] == 1 && g_staged_src[1] == VJ_BIOS_SRC_FRONTEND && g_staged_crc[1] == cartcrc);
    CHECK(g_staged_n[2] == 0 && g_path_calls[2] == 0);
    CHECK(cfg.current_frame >= 60);
    harness_shutdown(&cfg);

    /* G: cart target with no file: the built-in Series K runs. */
    reset_part3();
    g_boot_target = VJ_BOOT_CART_BOOT_ROM;
    CHECK(start_nocontent(&cfg, argc, argv, 3));
    run_frames(&cfg, 60);
    CHECK(g_staged_n[1] == 1 && g_staged_src[1] == VJ_BIOS_SRC_BUILTIN && g_staged_crc[1] == 0xFB731AAAu);
    CHECK(g_staged_n[2] == 0);
    harness_shutdown(&cfg);

    /* H: cart content: the boot target is never asked; the cart boot ROM file is (and wins even in HLE). */
    reset_part3();
    g_bios_path[1] = cartp;
    CHECK(start(&cfg, argc, argv, 3, NULL));
    run_frames(&cfg, 10);
    CHECK(g_target_calls == 0);
    CHECK(g_staged_n[1] == 1 && g_staged_src[1] == VJ_BIOS_SRC_FRONTEND && g_staged_crc[1] == cartcrc);
    harness_shutdown(&cfg);

    harness_set_env_hook(NULL);
    unlink(cdp); unlink(cartp); unlink(badp);
    rmdir(dir);
}

int main(int argc, char **argv)
{
    part1(argc, argv);
    part2(argc, argv);
    part3(argc, argv);
    printf("frontend_ext: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
