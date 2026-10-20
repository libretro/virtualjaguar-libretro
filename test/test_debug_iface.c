/*
 * test/test_debug_iface.c -- the core side of the app's debug interface
 * (src/debug/vj_debug_iface.h, src/debug/vjdebug.c).
 *
 * Plays the frontend: answers the private VJ_ENV_HELLO, receives the
 * interface through VJ_ENV_SET_DEBUG_IFACE, attaches a host with a scripted
 * halt_pump, and drives the real core on the public test ROM yarc.j64.
 *
 *   A) a frontend that refuses HELLO (RetroArch), and one that answers
 *      without VJ_CAP_DEBUG, never gets the interface; one that asks for it
 *      gets it exactly once per session, size/version filled in.
 *   B) peek: the DRAM image equals jaguarMainRAM byte for byte; the cartridge
 *      window equals jaguarMainROM; a read off the end of the bus is refused.
 *   C) poke policy: DRAM is written; ROM and the CD registers are refused; a
 *      register write needs VJ_DBG_POKE_REGS, and B_CMD / G_CTRL / D_CTRL /
 *      OLP / LTXD / RTXD need VJ_DBG_POKE_DANGEROUS on top; a refused write
 *      changes nothing.
 *   D) halting: halt_next stops the 68K at an instruction boundary, the pump
 *      is called until it answers; STEP stops again on the next instruction;
 *      CONTINUE runs on; a breakpoint at a PC the game revisits halts there
 *      again; DETACH resumes and leaves nothing armed; with no host attached
 *      halt_next is ignored (never blocks).
 *   E) registers / disassembly agree with the core's own accessors.
 *   F) OP snapshot: an armed capture records at least one object of the real
 *      display list, with the raw phrases; the blitter log sees a blit
 *      launched through a (dangerous) register poke.
 *   H) texreplace_reload: a pack installed AFTER the game loaded is found by the
 *      reload (the base path and the option survive it), removing it again is
 *      seen too, and with the option off the reload never activates anything.
 *   G) zero effect on the machine: 150 frames with everything on -- host
 *      attached, a halt/step/continue, peeks every frame, the OP snapshot
 *      armed, the blit log on -- end in the same frame hash AND the same
 *      main-RAM hash as 150 frames with no debug interface at all.
 *
 * Needs a TEST_EXPORTS=1 core (jaguarMainRAM, jaguarMainROM, VJDebug*).
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_debug_iface \
 *           test/test_debug_iface.c test/harness/harness.c -ldl -lm
 * Run:    ./test/test_debug_iface ./virtualjaguar_libretro.dylib test/roms/yarc.j64
 */
#include "harness/harness.h"
#include "vj_frontend_ext.h"
#include "vj_debug_iface.h"

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

/* ---- the fake frontend -------------------------------------------------- */
static unsigned g_caps;                       /* what HELLO answers (0 = refuse) */
static unsigned g_hello_calls, g_set_calls;
static const struct vj_debug_iface *g_if;

static bool env_hook(unsigned cmd, void *data)
{
    if (cmd == VJ_ENV_HELLO) {
        struct vj_env_hello *h = (struct vj_env_hello *)data;
        g_hello_calls++;
        if (!g_caps || !h)
            return false;
        h->caps_out = g_caps;
        return true;
    }
    if (cmd == VJ_ENV_SET_DEBUG_IFACE) {
        g_set_calls++;
        g_if = (const struct vj_debug_iface *)data;
        return true;
    }
    if (cmd == VJ_ENV_EVENT)
        return true;
    return false;
}

static int start(harness_config *cfg, int argc, char **argv, unsigned caps)
{
    harness_config fresh = HARNESS_CONFIG_DEFAULT;
    *cfg = fresh;
    cfg->frames = 1;
    cfg->quiet = 1;
    cfg->want_fb_hash = 1;
    g_caps = caps;
    g_hello_calls = g_set_calls = 0;
    g_if = NULL;
    harness_set_env_hook(env_hook);
    if (!harness_init_from_args(cfg, argc, argv))
        return 0;
    return harness_load_rom(cfg) ? 1 : 0;
}

static void run_frames(harness_config *cfg, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        harness_step(cfg);
}

/* ---- the scripted host --------------------------------------------------- */
#define MAX_CALLS 64
static struct {
    int target;
    unsigned pc;
    int reason;
    int halted_target;
    int pc_matches_regs;
} g_calls[MAX_CALLS];
static unsigned g_ncalls;
static int g_script[MAX_CALLS];     /* verdict per call; the last one repeats */
static unsigned g_script_len;
static unsigned g_pump_total;       /* every pump call since attach, past MAX_CALLS too */
static unsigned g_break_on_first;   /* arm a breakpoint at the first halted PC */
static unsigned g_break_pc;
static unsigned g_break_hits;       /* halts at g_break_pc with reason BREAKPOINT */

static int pump(void *user, int target, unsigned pc)
{
    int v;
    unsigned idx = g_ncalls;
    struct vj_debug_regs r;
    (void)user;

    g_pump_total++;
    if (idx < MAX_CALLS) {
        g_calls[idx].target = target;
        g_calls[idx].pc = pc;
        g_calls[idx].reason = g_if->halt_reason();
        g_calls[idx].halted_target = g_if->halted_target();
        g_calls[idx].pc_matches_regs = 0;
        if (target == VJ_DBG_TARGET_M68K && g_if->get_regs(VJ_DBG_TARGET_M68K, &r))
            g_calls[idx].pc_matches_regs = (r.pc == pc);
        g_ncalls++;
    }
    if (g_break_on_first && !g_break_pc) {
        g_break_pc = pc;
        CHECK(g_if->break_set(VJ_DBG_TARGET_M68K, pc) == 1);
    }
    if (g_break_pc && pc == g_break_pc && g_if->halt_reason() == VJ_DBG_STOP_BREAKPOINT) {
        g_break_hits++;
        g_if->break_clear(VJ_DBG_TARGET_M68K, g_break_pc);   /* hit once is enough */
    }
    v = g_script_len == 0 ? VJ_DBG_RESUME_CONTINUE
      : (idx < g_script_len ? g_script[idx] : g_script[g_script_len - 1]);
    return v;
}

static struct vj_debug_host g_host = { sizeof(struct vj_debug_host), NULL, pump };

static void reset_script(void)
{
    g_ncalls = 0;
    g_script_len = 0;
    g_pump_total = 0;
    g_break_on_first = 0;
    g_break_pc = 0;
    g_break_hits = 0;
}

/* ---- helpers --------------------------------------------------------------- */
static uint32_t fnv(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static unsigned char *main_ram(harness_config *cfg)
{
    unsigned char **pp = (unsigned char **)harness_dlsym(cfg, "jaguarMainRAM");
    return pp ? *pp : NULL;
}

static int (*attached_fn)(void);

/* ---- A: who gets the interface ---------------------------------------------------- */
static void partA(int argc, char **argv)
{
    harness_config cfg;

    CHECK(start(&cfg, argc, argv, 0));                       /* RetroArch: HELLO refused */
    run_frames(&cfg, 30);
    CHECK(g_hello_calls == 1 && g_set_calls == 0 && g_if == NULL);
    harness_shutdown(&cfg);

    CHECK(start(&cfg, argc, argv, VJ_CAP_EVENTS | VJ_CAP_BIOS));   /* answers, no debug cap */
    run_frames(&cfg, 30);
    CHECK(g_set_calls == 0 && g_if == NULL);
    harness_shutdown(&cfg);

    CHECK(start(&cfg, argc, argv, VJ_CAP_EVENTS | VJ_CAP_DEBUG));  /* asks for it */
    CHECK(g_set_calls == 1 && g_if != NULL);
    CHECK(g_if->version == VJ_DEBUG_IFACE_VERSION);
    CHECK(g_if->size >= sizeof(struct vj_debug_iface));
    CHECK(g_if->peek && g_if->poke && g_if->attach && g_if->detach && g_if->get_regs && g_if->halt_reason);
    run_frames(&cfg, 30);
    CHECK(g_set_calls == 1);                                       /* once, not per frame */
    harness_shutdown(&cfg);

    /* A refusing session after an answering one (the iOS static-state rule). */
    CHECK(start(&cfg, argc, argv, 0));
    run_frames(&cfg, 10);
    CHECK(g_set_calls == 0 && g_if == NULL);
    harness_shutdown(&cfg);
}

/* ---- B/C: memory ---------------------------------------------------------------------- */
static void partBC(int argc, char **argv)
{
    harness_config cfg;
    static unsigned char buf[0x200000];
    unsigned char b[16], one;
    unsigned char **romp;
    unsigned char *ram;
    unsigned i;

    CHECK(start(&cfg, argc, argv, VJ_CAP_EVENTS | VJ_CAP_DEBUG));
    run_frames(&cfg, 20);
    ram = main_ram(&cfg);
    CHECK(ram != NULL && g_if != NULL);

    /* peek == the core's own RAM, for the whole 2 MB and for odd slices. */
    CHECK(g_if->peek(0, buf, sizeof buf) == (int)sizeof buf);
    CHECK(memcmp(buf, ram, sizeof buf) == 0);
    CHECK(g_if->peek(0x1234, b, 7) == 7 && memcmp(b, ram + 0x1234, 7) == 0);
    CHECK(g_if->peek(0x1FFFFC, b, 4) == 4 && memcmp(b, ram + 0x1FFFFC, 4) == 0);

    /* the cartridge window is the ROM image */
    romp = (unsigned char **)harness_dlsym(&cfg, "jaguarMainROM");
    CHECK(romp && *romp);
    if (romp && *romp) {
        CHECK(g_if->peek(0x800000, b, 16) == 16 && memcmp(b, *romp, 16) == 0);
        CHECK(g_if->peek(0x800000 + 0x1000, b, 16) == 16 && memcmp(b, *romp + 0x1000, 16) == 0);
    }
    /* TOM/JERRY registers answer (the byte path) and do not fault */
    CHECK(g_if->peek(0xF00400, b, 8) == 8);
    CHECK(g_if->peek(0xF02100, b, 4) == 4);
    CHECK(g_if->peek(0xF1A100, b, 4) == 4);

    /* bounds */
    CHECK(g_if->peek(0xFFFFFF, b, 2) == VJ_DBG_ERR_RANGE);
    CHECK(g_if->peek(0x1000000u, b, 1) == VJ_DBG_ERR_RANGE);
    CHECK(g_if->peek(0, b, 0) == VJ_DBG_ERR_RANGE);
    CHECK(g_if->peek(0, NULL, 4) == VJ_DBG_ERR_RANGE);

    /* regions: ordered, non-overlapping, DRAM first, all named */
    {
        struct vj_debug_region r, prev;
        int n = 0;
        unsigned lastEnd = 0;
        while (n < 64 && g_if->region(n, &r)) {
            CHECK(r.name && r.name[0]);
            CHECK(r.len > 0);
            CHECK(r.start >= lastEnd);
            lastEnd = r.start + r.len;
            if (n == 0) { CHECK(strcmp(r.name, "DRAM") == 0 && r.start == 0 && r.len == 0x200000 && (r.flags & VJ_DBG_REGION_RAM)); }
            prev = r;
            n++;
        }
        (void)prev;
        CHECK(n >= 8 && !g_if->region(n, &r) && !g_if->region(-1, &r));
    }

    /* ---- poke policy ---- */
    for (i = 0; i < 16; i++) b[i] = (unsigned char)(0xA0 + i);
    CHECK(g_if->poke(0x7000, b, 16, 0) == 16);
    CHECK(memcmp(ram + 0x7000, b, 16) == 0);
    CHECK(g_if->poke(0x7000, b, 0, 0) == VJ_DBG_ERR_RANGE);
    CHECK(g_if->poke(0x200000, b, 4, 0) == VJ_DBG_ERR_RANGE);          /* unpopulated DRAM space */
    CHECK(g_if->poke(0x1FFFFE, b, 4, 0) == VJ_DBG_ERR_RANGE);          /* runs off the end of DRAM */
    CHECK(g_if->poke(0x800000, b, 4, 0) == VJ_DBG_ERR_READONLY);       /* cartridge */
    CHECK(g_if->poke(0xE00000, b, 4, 0) == VJ_DBG_ERR_READONLY);       /* BIOS */
    {
        unsigned flagsets[3];
        unsigned f;
        flagsets[0] = 0;
        flagsets[1] = VJ_DBG_POKE_REGS;
        flagsets[2] = VJ_DBG_POKE_REGS | VJ_DBG_POKE_DANGEROUS;
        for (f = 0; f < 3; f++) {
            CHECK(g_if->poke(0xDFFF00, b, 4, flagsets[f]) == VJ_DBG_ERR_POLICY);   /* BUTCH: never */
            CHECK(g_if->poke(0xDFFFFF, b, 1, flagsets[f]) == VJ_DBG_ERR_POLICY);
        }
    }
    /* registers: refused without the flag, written with it; nothing partial */
    {
        unsigned char before[4], after[4], w[2], saved[2], memcon[2];
        unsigned char *tom = harness_dlsym(&cfg, "tomRam8");
        CHECK(tom != NULL);
        if (!tom) return;
        memcpy(saved, tom + 0x2A, 2);
        CHECK(g_if->peek(0xF00000, memcon, 2) == 2);
        w[0] = 0x12; w[1] = 0x34;
        CHECK(g_if->peek(0xF0002A, before, 2) == 2);                     /* BORD1 (WO) */
        CHECK(g_if->poke(0xF0002A, w, 2, 0) == VJ_DBG_ERR_POLICY);
        CHECK(g_if->peek(0xF0002A, after, 2) == 2 && memcmp(before, after, 2) == 0);
        CHECK(memcmp(saved, tom + 0x2A, 2) == 0);
        CHECK(g_if->poke(0xF0002A, w, 2, VJ_DBG_POKE_REGS) == 2);
        CHECK(tom[0x2A] == 0x12 && tom[0x2B] == 0x34);
        /* Bus peeks of a WO register expose MEMCON1, not its latch (#1045). */
        CHECK(g_if->peek(0xF0002A, after, 2) == 2 && memcmp(after, memcon, 2) == 0);
    }
    /* the side-effecting set needs the second flag; one byte of it is enough to be refused */
    {
        static const unsigned danger[] = { 0xF02238u, 0xF02114u, 0xF1A114u, 0xF00020u, 0xF1A148u, 0xF1A14Cu };
        unsigned char before[4], after[4], z[4] = { 0, 0, 0, 0 };
        unsigned k;
        for (k = 0; k < sizeof danger / sizeof danger[0]; k++) {
            CHECK(g_if->peek(danger[k], before, 4) == 4);
            CHECK(g_if->poke(danger[k], z, 4, 0) == VJ_DBG_ERR_POLICY);
            CHECK(g_if->poke(danger[k], z, 4, VJ_DBG_POKE_REGS) == VJ_DBG_ERR_POLICY);
            CHECK(g_if->poke(danger[k] + 3, z, 1, VJ_DBG_POKE_REGS) == VJ_DBG_ERR_POLICY);
            CHECK(g_if->peek(danger[k], after, 4) == 4 && memcmp(before, after, 4) == 0);
        }
    }
    /* A write that starts in plain RAM and ends in a refused byte changes nothing. */
    one = ram[0x1FFFFF];
    CHECK(g_if->poke(0x1FFFFF, b, 2, 0) == VJ_DBG_ERR_RANGE);
    CHECK(ram[0x1FFFFF] == one);

    harness_shutdown(&cfg);
}

/* ---- D/E: halting, stepping, breakpoints, registers ---------------------------------------- */
static void partDE(int argc, char **argv)
{
    harness_config cfg;
    struct vj_debug_regs r1, r2;
    char text[128];
    unsigned len = 0;
    unsigned before;
    int rc;

    CHECK(start(&cfg, argc, argv, VJ_CAP_EVENTS | VJ_CAP_DEBUG));
    run_frames(&cfg, 20);
    CHECK(g_if != NULL);
    attached_fn = (int (*)(void))harness_dlsym(&cfg, "VJDebugAttached");
    CHECK(attached_fn != NULL);

    /* No host attached: halt_next must be ignored, never block the frame. */
    reset_script();
    g_if->halt_next(VJ_DBG_TARGET_M68K);
    run_frames(&cfg, 3);
    CHECK(g_pump_total == 0);
    CHECK(attached_fn && attached_fn() == 0);

    /* attach / second attach */
    CHECK(g_if->attach(&g_host) == 1);
    CHECK(attached_fn() == 1);
    CHECK(g_if->attach(&g_host) == 0);
    CHECK(g_if->halted_target() == -1);

    /* halt_next -> NONE, NONE, STEP, STEP, CONTINUE */
    reset_script();
    g_script[0] = VJ_DBG_RESUME_NONE;
    g_script[1] = VJ_DBG_RESUME_NONE;
    g_script[2] = VJ_DBG_RESUME_STEP;
    g_script[3] = VJ_DBG_RESUME_STEP;
    g_script[4] = VJ_DBG_RESUME_CONTINUE;
    g_script_len = 5;
    g_if->halt_next(VJ_DBG_TARGET_M68K);
    before = cfg.current_frame;
    harness_step(&cfg);
    CHECK(cfg.current_frame == before + 1);                  /* the frame finished */
    CHECK(g_ncalls == 5);
    if (g_ncalls >= 5) {
        unsigned i;
        for (i = 0; i < 5; i++) {
            CHECK(g_calls[i].target == VJ_DBG_TARGET_M68K);
            CHECK(g_calls[i].halted_target == VJ_DBG_TARGET_M68K);   /* only meaningful in the pump */
            CHECK(g_calls[i].pc_matches_regs);
            CHECK((g_calls[i].pc & 1u) == 0);
        }
        CHECK(g_calls[0].reason == VJ_DBG_STOP_USER);
        CHECK(g_calls[1].pc == g_calls[0].pc && g_calls[2].pc == g_calls[0].pc);   /* same halt, pumped again */
        CHECK(g_calls[3].reason == VJ_DBG_STOP_STEP);        /* the step stopped on the next instruction */
        CHECK(g_calls[4].reason == VJ_DBG_STOP_STEP);
    }
    CHECK(g_if->halted_target() == -1);                      /* not halted any more */
    run_frames(&cfg, 5);
    CHECK(g_ncalls == 5);                                    /* CONTINUE: nothing armed, runs on */

    /* A breakpoint at a PC the game revisits halts there again. */
    reset_script();
    g_script_len = 1;
    g_script[0] = VJ_DBG_RESUME_CONTINUE;
    g_break_on_first = 1;
    g_if->halt_next(VJ_DBG_TARGET_M68K);
    run_frames(&cfg, 90);
    CHECK(g_break_pc != 0);
    CHECK(g_break_hits >= 1);
    CHECK(g_ncalls >= 2);

    /* Registers: the interface and the core agree, and a write is seen. */
    reset_script();
    g_script_len = 1;
    g_script[0] = VJ_DBG_RESUME_CONTINUE;
    {
        uint32_t (*get_reg)(void *, int) = (uint32_t (*)(void *, int))harness_dlsym(&cfg, "m68k_get_reg");
        CHECK(g_if->get_regs(VJ_DBG_TARGET_M68K, &r1) == 1);
        CHECK(get_reg != NULL);
        if (get_reg) {
            CHECK(r1.d[0] == get_reg(NULL, 0) && r1.d[7] == get_reg(NULL, 7));
            CHECK(r1.a[3] == get_reg(NULL, 8 + 3));
            CHECK(r1.sr == get_reg(NULL, 17));
        }
        CHECK(g_if->set_reg(VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0 + 5, 0xCAFEBABEu) == 1);
        CHECK(g_if->get_regs(VJ_DBG_TARGET_M68K, &r2) == 1 && r2.d[5] == 0xCAFEBABEu);
        CHECK(g_if->set_reg(VJ_DBG_TARGET_M68K, 99, 0) == 0);
        CHECK(g_if->set_reg(VJ_DBG_TARGET_M68K, -1, 0) == 0);
    }
    {
        uint32_t *b0 = (uint32_t *)harness_dlsym(&cfg, "gpu_reg_bank_0");
        uint32_t *b1 = (uint32_t *)harness_dlsym(&cfg, "gpu_reg_bank_1");
        CHECK(g_if->get_regs(VJ_DBG_TARGET_GPU, &r1) == 1);
        CHECK(b0 && b1 && memcmp(r1.bank0, b0, 32 * 4) == 0 && memcmp(r1.bank1, b1, 32 * 4) == 0);
        CHECK(r1.cur_bank <= 1);
        CHECK(g_if->set_reg(VJ_DBG_TARGET_GPU, VJ_DBG_REG_RISC_BANK1 + 3, 0x1234567u) == 1);
        CHECK(b1 && b1[3] == 0x1234567u);
        CHECK(g_if->set_reg(VJ_DBG_TARGET_GPU, VJ_DBG_REG_RISC_BANK0 + 4, 0x7654321u) == 1);
        CHECK(b0 && b0[4] == 0x7654321u);
        CHECK(g_if->get_regs(VJ_DBG_TARGET_DSP, &r1) == 1);
        CHECK(g_if->get_regs(7, &r1) == 0);
    }
    /* RISC disassembly: a real line for GPU/DSP, nothing for the 68K. */
    memset(text, 0, sizeof text);
    rc = g_if->disasm_risc(VJ_DBG_TARGET_GPU, 0xF03000, text, sizeof text, &len);
    CHECK(rc == 1 && (len == 2 || len == 6) && text[0] == '$');
    CHECK(g_if->disasm_risc(VJ_DBG_TARGET_DSP, 0xF1B000, text, sizeof text, &len) == 1);
    CHECK(g_if->disasm_risc(VJ_DBG_TARGET_M68K, 0x4000, text, sizeof text, &len) == 0);
    CHECK(g_if->disasm_risc(VJ_DBG_TARGET_GPU, 0xF03000, text, 4, &len) == 1 && strlen(text) <= 3);   /* small buffer */

    /* The frame counter is a plain emulated-frame count. */
    CHECK(g_if->frame_number() >= 100);

    /* Break table: bad targets / idempotence */
    CHECK(g_if->break_set(9, 0x1000) == 0);
    CHECK(g_if->break_set(VJ_DBG_TARGET_M68K, 0x1000) == 1);
    CHECK(g_if->break_set(VJ_DBG_TARGET_M68K, 0x1000) == 1);
    CHECK(g_if->break_clear(VJ_DBG_TARGET_M68K, 0x1000) == 1);
    CHECK(g_if->break_clear(VJ_DBG_TARGET_M68K, 0x1000) == 1);

    /* DETACH verdict: resumes, drops the host and every breakpoint it set. */
    reset_script();
    g_script_len = 1;
    g_script[0] = VJ_DBG_RESUME_DETACH;
    CHECK(g_if->break_set(VJ_DBG_TARGET_M68K, 0x1234) == 1);
    g_if->halt_next(VJ_DBG_TARGET_M68K);
    run_frames(&cfg, 2);
    CHECK(g_ncalls == 1);
    CHECK(attached_fn() == 0);
    run_frames(&cfg, 60);
    CHECK(g_pump_total == 1);                                /* nothing armed, nothing halts */
    CHECK(g_if->break_set(VJ_DBG_TARGET_M68K, 0x1234) == 0); /* detached: refused */
    g_if->halt_next(VJ_DBG_TARGET_M68K);
    run_frames(&cfg, 3);
    CHECK(g_pump_total == 1);                                /* and the stale one-shot is harmless */

    /* attach again, then an explicit detach() between frames */
    CHECK(g_if->attach(&g_host) == 1 && attached_fn() == 1);
    g_if->detach();
    CHECK(attached_fn() == 0);
    run_frames(&cfg, 3);

    /* Unloading the game drops an attached host (iOS never dlcloses the core). */
    CHECK(g_if->attach(&g_host) == 1);
    harness_shutdown(&cfg);
}

/* ---- F: OP snapshot, blitter log ----------------------------------------------------------------- */
static void partF(int argc, char **argv)
{
    harness_config cfg;
    static struct vj_debug_op_obj objs[VJ_DBG_OP_MAX_OBJECTS];
    unsigned halfline = 0;
    int n, i;
    struct vj_debug_blit bl;
    unsigned char w[4];

    CHECK(start(&cfg, argc, argv, VJ_CAP_EVENTS | VJ_CAP_DEBUG));
    run_frames(&cfg, 30);

    CHECK(g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline) == -1);   /* never armed */
    g_if->op_snapshot_arm();
    CHECK(g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline) == -1);   /* armed, no walk yet */
    run_frames(&cfg, 3);
    n = g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline);
    CHECK(n >= 1);
    {
        int bitmaps = 0, stops = 0;
        for (i = 0; i < n; i++) {
            if (objs[i].type == 0) bitmaps++;
            if (objs[i].type == 4) stops++;
        }
        CHECK(bitmaps >= 1);        /* the whole list, not just the branch + stop of one halfline */
        CHECK(stops >= 1);
        for (i = 0; i < n; i++) {   /* each object once */
            int k;
            for (k = i + 1; k < n; k++) CHECK(objs[i].address != objs[k].address);
        }
    }
    for (i = 0; i < n && i < 8; i++) {
        unsigned type = objs[i].p0_lo & 7u;
        CHECK(objs[i].type == type);
        CHECK((objs[i].address & 7u) == 0);
        CHECK(objs[i].phrases >= 1 && objs[i].phrases <= 3);
        if (type == 0) CHECK(objs[i].phrases == 2);
        if (type == 1) CHECK(objs[i].phrases == 3);
        if (type >= 2) CHECK(objs[i].phrases == 1);
    }
    CHECK(halfline < 2048);
    /* Stays readable; a second get is identical; re-arming makes it pending again. */
    CHECK(g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline) == n);
    CHECK(g_if->op_snapshot_get(NULL, 0, NULL) == 0);
    g_if->op_snapshot_arm();
    CHECK(g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline) == -1);
    run_frames(&cfg, 3);
    CHECK(g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline) >= 1);

    /* Blitter log: off by default; on, a blit launched by register pokes is recorded. */
    CHECK(g_if->blit_log_head() == 0);
    g_if->blit_log_enable(1);
    {
        /* One 1x1 pixel blit, 16bpp, A1 -> DRAM $70000; the launch is the B_CMD write. */
        unsigned f = VJ_DBG_POKE_REGS | VJ_DBG_POKE_DANGEROUS;
        unsigned char base[4], flags[4], count[4], cmd[4], zero[4];
        base[0] = 0; base[1] = 0x07; base[2] = 0; base[3] = 0;           /* A1_BASE = $70000 */
        flags[0] = 0; flags[1] = 0; flags[2] = 0x40; flags[3] = 0x20;    /* A1_FLAGS: PIXEL16, WID */
        count[0] = 0; count[1] = 1; count[2] = 0; count[3] = 1;          /* B_COUNT = 1 line x 1 pixel */
        cmd[0] = 0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0x01;               /* B_CMD: SRCEN */
        zero[0] = zero[1] = zero[2] = zero[3] = 0;
        CHECK(g_if->poke(0xF02200, base, 4, f) == 4);
        CHECK(g_if->poke(0xF02204, flags, 4, f) == 4);
        CHECK(g_if->poke(0xF0220C, zero, 4, f) == 4);                    /* A1_PIXEL */
        CHECK(g_if->poke(0xF0223C, count, 4, f) == 4);
        CHECK(g_if->blit_log_head() == 0);                               /* registers alone launch nothing */
        CHECK(g_if->poke(0xF02238, cmd, 4, f) == 4);
        CHECK(g_if->blit_log_head() == 1);
        CHECK(g_if->blit_log_get(0, &bl) == 1);
        CHECK(bl.seq == 0 && bl.cmd == 0x01u && bl.count == 0x00010001u && bl.a1_base == 0x70000u);
        CHECK(bl.frame == g_if->frame_number());
        CHECK(bl.engine <= 1);
        CHECK(g_if->blit_log_get(1, &bl) == 0);
    }
    g_if->blit_log_enable(0);
    memset(w, 0, sizeof w);
    CHECK(g_if->poke(0xF02238, w, 4, VJ_DBG_POKE_REGS | VJ_DBG_POKE_DANGEROUS) == 4);   /* cmd 0: still a launch... */
    CHECK(g_if->blit_log_head() == 1);                                                  /* ...but nobody is logging */

    /* texture-pack reload with the feature off: harmless, reports "not active" */
    CHECK(g_if->texreplace_reload() == 0);
    CHECK(g_if->cry_to_rgb(0) <= 0xFFFFFFu && g_if->cry_to_rgb(0xFFFF) <= 0xFFFFFFu);

    harness_shutdown(&cfg);
}


/* ---- H: texture pack reload ---------------------------------------------------------------------- */
static uint32_t crc32_of(const unsigned char *p, size_t n, uint32_t crc)
{
    size_t i;
    int k;
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void png_chunk(FILE *f, const char *tag, const unsigned char *d, uint32_t len)
{
    unsigned char hdr[8], tail[4];
    uint32_t c;
    be32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(d, 1, len, f);
    c = crc32_of((const unsigned char *)tag, 4, 0);
    c = crc32_of(d, len, c);
    be32(tail, c);
    fwrite(tail, 1, 4, f);
}

/* A w x h RGB PNG, stored (uncompressed) deflate blocks, mid-gray. */
static int write_gray_png(const char *path, unsigned w, unsigned h)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    size_t raw = (size_t)h * (w * 3 + 1), n = 0;
    unsigned char *scan = (unsigned char *)calloc(raw, 1), *z = (unsigned char *)calloc(raw + 64, 1);
    unsigned char ihdr[13], ad[4];
    uint32_t a = 1, b = 0;
    size_t i;
    FILE *f;
    if (!scan || !z) return 0;
    for (i = 0; i < raw; i++) if (i % (w * 3 + 1)) scan[i] = 0x80;
    for (i = 0; i < raw; i++) { a = (a + scan[i]) % 65521u; b = (b + a) % 65521u; }
    z[n++] = 0x78; z[n++] = 0x01;
    z[n++] = 1; z[n++] = (unsigned char)(raw & 0xFF); z[n++] = (unsigned char)(raw >> 8);
    z[n++] = (unsigned char)(~raw & 0xFF); z[n++] = (unsigned char)((~raw >> 8) & 0xFF);
    memcpy(z + n, scan, raw); n += raw;
    be32(ad, (b << 16) | a);
    memcpy(z + n, ad, 4); n += 4;
    f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(sig, 1, 8, f);
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)n);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(scan); free(z);
    return 1;
}

static int start_texreplace(harness_config *cfg, int argc, char **argv, const char *sysdir, const char *replace)
{
    harness_config fresh = HARNESS_CONFIG_DEFAULT;
    *cfg = fresh;
    cfg->frames = 1;
    cfg->quiet = 1;
    g_caps = VJ_CAP_EVENTS | VJ_CAP_DEBUG;
    g_if = NULL;
    harness_set_env_hook(env_hook);
    if (!harness_init_from_args(cfg, argc, argv))
        return 0;
    cfg->system_dir = sysdir;
    harness_set_option(cfg, "virtualjaguar_texture_replace", replace);
    harness_set_option(cfg, "virtualjaguar_texture_dump", "disabled");
    return harness_load_rom(cfg) ? 1 : 0;
}

static void partH(int argc, char **argv)
{
    harness_config cfg;
    char sys[256], dir[400], file[500];
    unsigned (*crc_get)(void);
    int *enabled;
    const char *tmp = getenv("TMPDIR");

    snprintf(sys, sizeof sys, "%s/vjtexrel_%d", (tmp && tmp[0]) ? tmp : "/tmp", (int)getpid());
    mkdir(sys, 0700);

    /* Option on, but no pack exists yet when the game loads. */
    CHECK(start_texreplace(&cfg, argc, argv, sys, "enabled"));
    run_frames(&cfg, 5);
    crc_get = (unsigned (*)(void))harness_dlsym(&cfg, "TitleDBContentCRC");
    enabled = (int *)harness_dlsym(&cfg, "texReplaceEnabled");
    CHECK(crc_get != NULL && enabled != NULL && g_if != NULL);
    if (crc_get && enabled && g_if) {
        CHECK(*enabled == 0);
        CHECK(g_if->texreplace_reload() == 0);               /* nothing installed: nothing active */

        snprintf(dir, sizeof dir, "%s/vj_texpacks", sys);
        mkdir(dir, 0700);
        snprintf(dir, sizeof dir, "%s/vj_texpacks/%08x", sys, crc_get());
        mkdir(dir, 0700);
        snprintf(file, sizeof file, "%s/0123456789abcdef.png", dir);
        CHECK(write_gray_png(file, 8, 8));

        CHECK(g_if->texreplace_reload() == 1);               /* found: base path + option survived */
        CHECK(*enabled == 1);
        CHECK(g_if->texreplace_reload() == 1);               /* and again (the reload is repeatable) */
        run_frames(&cfg, 10);                                /* the emulation runs on with a pack loaded */

        unlink(file);
        CHECK(g_if->texreplace_reload() == 0);               /* removed: seen too */
        CHECK(*enabled == 0);
        rmdir(dir);
    }
    harness_shutdown(&cfg);

    /* Option off: even with a pack on disk the reload activates nothing. */
    CHECK(start_texreplace(&cfg, argc, argv, sys, "disabled"));
    run_frames(&cfg, 5);
    crc_get = (unsigned (*)(void))harness_dlsym(&cfg, "TitleDBContentCRC");
    if (crc_get && g_if) {
        snprintf(dir, sizeof dir, "%s/vj_texpacks/%08x", sys, crc_get());
        mkdir(dir, 0700);
        snprintf(file, sizeof file, "%s/0123456789abcdef.png", dir);
        CHECK(write_gray_png(file, 8, 8));
        CHECK(g_if->texreplace_reload() == 0);
        unlink(file);
        rmdir(dir);
    }
    harness_shutdown(&cfg);
    snprintf(dir, sizeof dir, "%s/vj_texpacks", sys);
    rmdir(dir);
    rmdir(sys);
}

/* ---- G: the debug interface does not change the machine -------------------------------------------- */
static uint32_t g_hash_fb, g_hash_ram;

static void run_digest(int argc, char **argv, int with_debug)
{
    harness_config cfg;
    static struct vj_debug_op_obj objs[VJ_DBG_OP_MAX_OBJECTS];
    static unsigned char tmp[0x2000];
    unsigned halfline;
    unsigned f;

    CHECK(start(&cfg, argc, argv, with_debug ? (VJ_CAP_EVENTS | VJ_CAP_DEBUG) : 0));
    if (with_debug) {
        reset_script();
        g_script_len = 4;
        g_script[0] = VJ_DBG_RESUME_NONE;
        g_script[1] = VJ_DBG_RESUME_STEP;
        g_script[2] = VJ_DBG_RESUME_STEP;
        g_script[3] = VJ_DBG_RESUME_CONTINUE;
        CHECK(g_if->attach(&g_host) == 1);
        g_if->blit_log_enable(1);
        g_if->op_snapshot_arm();
    }
    for (f = 0; f < 150; f++) {
        harness_step(&cfg);
        if (with_debug) {
            struct vj_debug_regs r;
            g_if->peek(0, tmp, sizeof tmp);
            g_if->peek(0xF00400, tmp, 0x400);
            g_if->get_regs(VJ_DBG_TARGET_M68K, &r);
            g_if->get_regs(VJ_DBG_TARGET_GPU, &r);
            g_if->get_regs(VJ_DBG_TARGET_DSP, &r);
            if (f == 40) { g_ncalls = 0; g_if->halt_next(VJ_DBG_TARGET_M68K); }
            if (f == 60) g_if->op_snapshot_get(objs, VJ_DBG_OP_MAX_OBJECTS, &halfline);
            if (f == 80) g_if->op_snapshot_arm();
        }
    }
    if (with_debug)
        CHECK(g_ncalls == 4);                                 /* the halt really happened */
    g_hash_fb = cfg.last_fb_hash;
    g_hash_ram = fnv(main_ram(&cfg), 0x200000);
    harness_shutdown(&cfg);
}

static void partG(int argc, char **argv)
{
    uint32_t fb_plain, ram_plain, fb_dbg, ram_dbg;

    run_digest(argc, argv, 0);
    fb_plain = g_hash_fb; ram_plain = g_hash_ram;
    run_digest(argc, argv, 1);
    fb_dbg = g_hash_fb; ram_dbg = g_hash_ram;
    CHECK(fb_plain != 0 && ram_plain != 0);
    CHECK(fb_plain == fb_dbg);
    CHECK(ram_plain == ram_dbg);
    if (fb_plain != fb_dbg || ram_plain != ram_dbg)
        fprintf(stderr, "  fb %08x vs %08x, ram %08x vs %08x\n", fb_plain, fb_dbg, ram_plain, ram_dbg);
}

int main(int argc, char **argv)
{
    partA(argc, argv);
    partBC(argc, argv);
    partDE(argc, argv);
    partF(argc, argv);
    partH(argc, argv);
    partG(argc, argv);
    harness_set_env_hook(NULL);
    printf("debug_iface: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
