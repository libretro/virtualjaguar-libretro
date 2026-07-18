/*
 * cd_wedge_probe.c — catch intermittent CD-game lockups and dump 68K/CD
 * state at the moment of the freeze.
 *
 * Motivation: Hover Strike (CD) intermittently freezes when confirming a
 * menu selection that triggers a CD load (also reported as "lockup when
 * skipping cutscenes" / "load menu fails").  crash_detect's video_stall
 * fires but tells us nothing about the 68K.  This tool watches the
 * framebuffer itself; when it freezes for --freeze-frames after --arm,
 * it dumps:
 *   - 68K PC/registers (m68k_get_reg)
 *   - the last N unique PCs from the core's pcQueue traceback ring
 *   - CD subsystem counters (CDROMDiagSummary) + trace ring (CDTraceDump,
 *     enable with VJ_CD_TRACE=1)
 * then exits with code 42 (wedge caught) so a driver loop can tell
 * "caught" from "ran clean" (0).
 *
 * Requires a TEST_EXPORTS=1 core build.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/cd_wedge_probe test/tools/cd_wedge_probe.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Run (script the repro with --press, see harness.h):
 *   VJ_CD_TRACE=1 VJ_HARNESS_LOG_INFO=1 ./test/tools/cd_wedge_probe \
 *      core.dylib disc.cue --frames 4800 --system-dir <dir> \
 *      --press 600:a ... --arm 1500 --freeze-frames 240
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PX (1024u * 576u)

/* m68kinterface.h register indices (enum m68k_register_t) */
enum { REG_D0 = 0, REG_A0 = 8, REG_A7 = 15, REG_PC = 16, REG_SR = 17 };

typedef struct {
    uint32_t hash;
    int      have_hash;
    unsigned same_frames;
    unsigned arm_frame;
    unsigned freeze_frames;
    int      wedged;
    const char *ram_dump_path;
    unsigned snap_frames[8];
    unsigned n_snaps;
    const char *snap_prefix;
} wp_state;

/* Dump main RAM (2MB) and GPU local RAM ($F03000-$F03FFF) to files. */
static void wp_snapshot(harness_config *cfg, const char *base)
{
    /* jaguarMainRAM is a POINTER variable (into jagMemSpace) — dlsym gives
     * the address of the pointer; dereference to get the RAM base. */
    uint8_t **ramp = (uint8_t **)harness_dlsym(cfg, "jaguarMainRAM");
    uint8_t *ram = ramp ? *ramp : NULL;
    uint32_t (*p_gpurl)(uint32_t, uint32_t) =
        (uint32_t (*)(uint32_t, uint32_t))harness_dlsym(cfg, "GPUReadLong");
    uint32_t *p_gpupc = (uint32_t *)harness_dlsym(cfg, "gpu_pc");
    char path[1024];
    FILE *f;

    if (ram) {
        snprintf(path, sizeof(path), "%s.ram", base);
        f = fopen(path, "wb");
        if (f) {
            fwrite(ram, 1, 0x200000, f);
            fclose(f);
            fprintf(stderr, "[WEDGE-PROBE] main RAM -> %s\n", path);
        }
    }
    if (p_gpurl) {
        uint32_t a;
        snprintf(path, sizeof(path), "%s.gpu", base);
        f = fopen(path, "wb");
        if (f) {
            for (a = 0xF03000; a < 0xF04000; a += 4) {
                uint32_t v = p_gpurl(a, 0);
                uint8_t b[4];
                b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
                b[2] = (uint8_t)(v >> 8);  b[3] = (uint8_t)v;
                fwrite(b, 1, 4, f);
            }
            fclose(f);
            fprintf(stderr, "[WEDGE-PROBE] GPU RAM -> %s (gpu_pc=$%06X)\n",
                    path, p_gpupc ? *p_gpupc : 0);
        }
    }
}

static wp_state g_st;

static void wp_video_cb(void *ud, const void *data, unsigned w, unsigned h,
                        size_t pitch)
{
    wp_state *st = (wp_state *)ud;
    const uint8_t *src = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    unsigned x, y;

    if (!data || !w || !h || w * h > MAX_PX) return;

    /* FNV-ish hash over a subsampled grid — cheap but sensitive to any
     * visible change (matches crash_detect's intent, denser sample). */
    for (y = 0; y < h; y += 4) {
        const uint32_t *row = (const uint32_t *)(src + y * pitch);
        for (x = 0; x < w; x += 4) {
            hash ^= row[x] & 0x00FFFFFFu;
            hash *= 16777619u;
        }
    }

    if (st->have_hash && hash == st->hash)
        st->same_frames++;
    else
        st->same_frames = 0;
    st->hash = hash;
    st->have_hash = 1;
}

static void wp_dump(harness_config *cfg, unsigned frame)
{
    unsigned int (*p_get_reg)(void *, int) =
        (unsigned int (*)(void *, int))harness_dlsym(cfg, "m68k_get_reg");
    uint32_t *p_pcq   = (uint32_t *)harness_dlsym(cfg, "pcQueue");
    uint32_t *p_pcqp  = (uint32_t *)harness_dlsym(cfg, "pcQPtr");
    void (*p_cdsum)(void)   = (void (*)(void))harness_dlsym(cfg, "CDROMDiagSummary");
    void (*p_cddump)(void)  = (void (*)(void))harness_dlsym(cfg, "CDTraceDump");

    fprintf(stderr, "\n[WEDGE-PROBE] framebuffer frozen at frame %u "
                    "(%u identical frames)\n", frame, g_st.same_frames);

    if (p_get_reg) {
        int i;
        fprintf(stderr, "[WEDGE-PROBE] 68K PC=$%06X SR=$%04X A7=$%06X\n",
                p_get_reg(NULL, REG_PC), p_get_reg(NULL, REG_SR),
                p_get_reg(NULL, REG_A7));
        for (i = 0; i < 8; i++)
            fprintf(stderr, "[WEDGE-PROBE]   D%d=$%08X A%d=$%08X\n", i,
                    p_get_reg(NULL, REG_D0 + i), i, p_get_reg(NULL, REG_A0 + i));
    }

    if (p_pcq && p_pcqp) {
        /* Walk the 0x400-deep ring backwards, printing unique PCs in
         * most-recent-first order — shows the wait loop shape. */
        uint32_t seen[48];
        unsigned nseen = 0, idx = (*p_pcqp - 1) & 0x3FF, steps;
        fprintf(stderr, "[WEDGE-PROBE] recent 68K PCs (unique, newest first):");
        for (steps = 0; steps < 0x400 && nseen < 48; steps++) {
            uint32_t pc = p_pcq[idx];
            unsigned k, dup = 0;
            for (k = 0; k < nseen; k++)
                if (seen[k] == pc) { dup = 1; break; }
            if (!dup) {
                seen[nseen++] = pc;
                if ((nseen % 6) == 1) fprintf(stderr, "\n[WEDGE-PROBE]   ");
                fprintf(stderr, "$%06X ", pc);
            }
            idx = (idx - 1) & 0x3FF;
        }
        fprintf(stderr, "\n");
    }

    /* GPU PC history ring (TEMP Myst diag in gpu.c) — newest last. */
    {
        uint32_t *ring = (uint32_t *)harness_dlsym(cfg, "gpu_pc_ring");
        uint32_t *rptr = (uint32_t *)harness_dlsym(cfg, "gpu_pc_ring_ptr");
        uint32_t *rfrz = (uint32_t *)harness_dlsym(cfg, "gpu_pc_ring_frozen");
        if (ring && rptr) {
            unsigned n, idx = (*rptr - 64) & 511;
            fprintf(stderr, "[WEDGE-PROBE] GPU PC trail (frozen=%u, oldest first):",
                    rfrz ? *rfrz : 0);
            for (n = 0; n < 64; n++) {
                if ((n % 6) == 0) fprintf(stderr, "\n[WEDGE-PROBE]   ");
                fprintf(stderr, "$%06X ", ring[idx]);
                idx = (idx + 1) & 511;
            }
            fprintf(stderr, "\n");
        }
    }

    if (p_cdsum)  p_cdsum();
    if (p_cddump) p_cddump();

    /* Hex-dump main RAM around the spinning code so the wait loop can be
     * disassembled offline, plus the HLE DSP done-flag and CDDA mailbox. */
    {
        uint8_t **ramp = (uint8_t **)harness_dlsym(cfg, "jaguarMainRAM");
        uint8_t *ram = ramp ? *ramp : NULL;
        uint32_t (*p_dspr)(uint32_t, uint32_t) =
            (uint32_t (*)(uint32_t, uint32_t))harness_dlsym(cfg, "DSPReadLong");

        if (ram && p_get_reg) {
            uint32_t pc = p_get_reg(NULL, REG_PC);
            unsigned d;
            for (d = 0; d < 2; d++) {
                /* dump around PC, and around the most-called subroutine
                 * target if the caller left it in A2 (common idiom). */
                uint32_t base = d == 0 ? (pc & ~0xFu) - 0x20
                                       : (p_get_reg(NULL, REG_A0 + 2) & ~0xFu) - 0x10;
                unsigned row;
                if (base >= 0x200000) continue;
                fprintf(stderr, "[WEDGE-PROBE] RAM dump @ $%06X:\n", base);
                for (row = 0; row < 6; row++) {
                    unsigned col;
                    fprintf(stderr, "[WEDGE-PROBE]   $%06X:", base + row * 16);
                    for (col = 0; col < 16; col += 2)
                        fprintf(stderr, " %02X%02X",
                                ram[base + row * 16 + col],
                                ram[base + row * 16 + col + 1]);
                    fprintf(stderr, "\n");
                }
            }
        }
        /* Full main-RAM + GPU-RAM snapshot for offline stack walking /
         * disassembly of the code the 68K wedged in. */
        if (g_st.ram_dump_path)
            wp_snapshot(cfg, g_st.ram_dump_path);
        if (p_dspr) {
            fprintf(stderr, "[WEDGE-PROBE] DSP $F1B4C8 (HLE done flag) = $%08X\n",
                    p_dspr(0xF1B4C8, 0));
            fprintf(stderr, "[WEDGE-PROBE] DSP mailbox $F1B270 = $%08X $%08X $%08X $%08X\n",
                    p_dspr(0xF1B270, 0), p_dspr(0xF1B274, 0),
                    p_dspr(0xF1B278, 0), p_dspr(0xF1B27C, 0));
        }
    }
}

static bool wp_frame_cb(void *ud, unsigned frame)
{
    harness_config *cfg = (harness_config *)ud;
    unsigned i;

    for (i = 0; i < g_st.n_snaps; i++) {
        if (g_st.snap_frames[i] == frame && g_st.snap_prefix) {
            char base[900];
            snprintf(base, sizeof(base), "%s_f%u", g_st.snap_prefix, frame);
            wp_snapshot(cfg, base);
        }
    }

    if (frame >= g_st.arm_frame && g_st.same_frames >= g_st.freeze_frames) {
        g_st.wedged = 1;
        wp_dump(cfg, frame);
        return false;   /* stop the run */
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    int i;

    g_st.arm_frame = 600;
    g_st.freeze_frames = 240;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--arm") == 0 && i + 1 < argc)
            g_st.arm_frame = (unsigned)atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--freeze-frames") == 0 && i + 1 < argc)
            g_st.freeze_frames = (unsigned)atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--ram-dump") == 0 && i + 1 < argc)
            g_st.ram_dump_path = argv[i + 1];
        else if (strcmp(argv[i], "--snap") == 0 && i + 1 < argc &&
                 g_st.n_snaps < 8)
            g_st.snap_frames[g_st.n_snaps++] = (unsigned)atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--snap-prefix") == 0 && i + 1 < argc)
            g_st.snap_prefix = argv[i + 1];
    }

    cfg.frames = 4800;
    cfg.system_dir = "test/roms/private";
    cfg.video_callback = wp_video_cb;
    cfg.video_callback_data = &g_st;
    cfg.frame_callback = wp_frame_cb;
    cfg.frame_callback_data = &cfg;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "usage: cd_wedge_probe [core] <disc.cue> [--frames N] "
                        "[--arm N] [--freeze-frames N] [--ram-dump FILE] "
                        "[--press F:BTN[:HOLD]]... "
                        "[--system-dir DIR]\n");
        return 1;
    }
    if (!harness_load_rom(&cfg)) return 1;

    harness_run(&cfg);
    harness_shutdown(&cfg);

    if (g_st.wedged) {
        fprintf(stderr, "[WEDGE-PROBE] RESULT: wedge caught\n");
        return 42;
    }
    fprintf(stderr, "[WEDGE-PROBE] RESULT: ran clean\n");
    return 0;
}
