/*
 * idle_skip_scale_ab.c -- the RISC idle-loop fast-forward must be exact
 * under any RISC clock scale (issue #569; overclock presets #378).
 *
 * The fast-forward used to be switched off whenever
 * virtualjaguar_risc_clock_scale != 1x, on the grounds that the skipped
 * cycles might be in the wrong currency.  They are not: the scaled budget
 * is handed out in JaguarExecuteNew (SCALE_RISC_CYCLES) and the probe
 * measures cost/opcost from the live budget deltas.  This tool is the
 * regression guard for that claim.  It runs ONE arm; the Makefile runs the
 * same command twice, differing only in virtualjaguar_risc_idle_skip, and
 * `cmp`s the two per-frame digest files.  A skip that mis-scales its cost
 * (double-scaling, or the unscaled stock budget) leaves the idle loop's
 * counter register, the opcode count, or the cycle remainder different.
 *
 * Why a digest of machine state and not just the framebuffer: an idle
 * loop's whole effect is on register contents and counters, which yarc's
 * picture never shows.  Each row hashes
 *     framebuffer, audio, GPU PC + both register banks + opcode count,
 *     DSP PC + both register banks + opcode count
 * and every --state-every frames the full retro_serialize() blob.
 *
 * yarc.j64 (in-tree) spins its GPU in idle loops from boot; its DSP is
 * parked, so at --inject-frame this tool hand-assembles an idle loop
 * (addqt #1,r0 / jr always,-2 / nop) into DSP RAM and starts it, with the
 * DSP interrupt enables masked so nothing vectors it away.  The counter in
 * r0 then records exactly how many iterations the machine believes ran.
 *
 * --require-fires makes the arm FAIL (exit 3) unless both fast-forwards
 * fired: "skip on == skip off" is trivially true if the skip never runs,
 * which is how a silently re-suppressed feature would pass.
 *
 * Build:  make test/tools/idle_skip_scale_ab TEST_EXPORTS=1
 * Run:    ./test/tools/idle_skip_scale_ab ./virtualjaguar_libretro.dylib \
 *            test/roms/yarc.j64 --frames 240 --csv arm.csv \
 *            --option virtualjaguar_risc_clock_scale=2x \
 *            --option virtualjaguar_risc_idle_skip=enabled --require-fires
 *
 * Needs the wide test ABI (make TEST_EXPORTS=1).
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FNV_OFF 1469598103934665603ULL
#define FNV_PRM 1099511628211ULL

#define OP_ADDQT   3
#define OP_JR     53
#define OP_NOP    57
#define CC_ALWAYS  0

#define DSP_RAM_BASE 0x00F1B000u
#define PROG_OFF     0x0100u          /* $F1B100 */

static FILE    *g_csv;
static uint64_t g_vhash = FNV_OFF;
static uint64_t g_ahash = FNV_OFF;
static unsigned g_inject_frame = 5;
static unsigned g_state_every  = 40;

static uint8_t  *dsp_ram;
static uint32_t *p_dsp_pc, *p_dsp_control, *p_dsp_b0, *p_dsp_b1;
static uint32_t *p_dsp_ops, *p_dsp_fires;
static uint32_t *p_gpu_pc, *p_gpu_b0, *p_gpu_b1;
static uint32_t *p_gpu_ops, *p_gpu_fires;
static void    (*p_dsp_setflags)(uint32_t);

static size_t (*p_serialize_size)(void);
static int    (*p_serialize)(void *, size_t);
static void    *g_state_buf;
static size_t   g_state_size;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint64_t)b[i];
        h *= FNV_PRM;
    }
    return h;
}

static void ab_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
    uint64_t hv = FNV_OFF;
    unsigned y;
    (void)ud;
    if (!data)
        return;
    for (y = 0; y < h; y++)
        hv = fnv(hv, (const uint8_t *)data + (size_t)y * pitch, (size_t)w * 4);
    g_vhash = hv;
}

static size_t ab_audio(const int16_t *data, size_t frames)
{
    if (data && frames)
        g_ahash = fnv(g_ahash, data, frames * 2 * sizeof(int16_t));
    return frames;
}

static uint16_t enc(uint32_t idx, uint32_t p1, uint32_t p2)
{
    return (uint16_t)((idx << 10) | ((p1 & 0x1F) << 5) | (p2 & 0x1F));
}

static void put16(uint32_t off, uint16_t w)
{
    dsp_ram[off]     = (uint8_t)(w >> 8);
    dsp_ram[off + 1] = (uint8_t)(w & 0xFF);
}

static void inject_dsp_loop(void)
{
    int i;

    /* addqt #1,r0 ; jr always,-2 ; (ds) nop  -- jr target = its own head. */
    put16(PROG_OFF + 0, enc(OP_ADDQT, 1, 0));
    put16(PROG_OFF + 2, enc(OP_JR, 0x1E, CC_ALWAYS));
    put16(PROG_OFF + 4, enc(OP_NOP, 0, 0));
    for (i = 0; i < 32; i++) {
        p_dsp_b0[i] = 0;
        p_dsp_b1[i] = 0;
    }
    p_dsp_setflags(0);                 /* mask every DSP interrupt enable */
    *p_dsp_pc = DSP_RAM_BASE + PROG_OFF;
    *p_dsp_control |= 0x01;            /* DSPGO */
}

static bool ab_frame(void *ud, unsigned frame)
{
    uint64_t d = FNV_OFF, sh = 0;
    uint32_t v;
    (void)ud;

    if (frame == g_inject_frame)
        inject_dsp_loop();

    d = fnv(d, &g_vhash, sizeof(g_vhash));
    d = fnv(d, &g_ahash, sizeof(g_ahash));
    v = *p_gpu_pc;  d = fnv(d, &v, sizeof(v));
    d = fnv(d, p_gpu_b0, 32 * sizeof(uint32_t));
    d = fnv(d, p_gpu_b1, 32 * sizeof(uint32_t));
    v = *p_gpu_ops; d = fnv(d, &v, sizeof(v));
    v = *p_dsp_pc;  d = fnv(d, &v, sizeof(v));
    d = fnv(d, p_dsp_b0, 32 * sizeof(uint32_t));
    d = fnv(d, p_dsp_b1, 32 * sizeof(uint32_t));
    v = *p_dsp_ops; d = fnv(d, &v, sizeof(v));

    if (g_state_every && p_serialize && g_state_buf
        && (frame % g_state_every) == 0) {
        memset(g_state_buf, 0, g_state_size);
        if (p_serialize(g_state_buf, g_state_size))
            sh = fnv(FNV_OFF, g_state_buf, g_state_size);
    }
    fprintf(g_csv, "%u,%016llx,%016llx,%u,%u\n", frame,
            (unsigned long long)d, (unsigned long long)sh,
            (unsigned)p_dsp_b0[0], (unsigned)*p_dsp_ops);
    g_ahash = FNV_OFF;
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    const char *csv = "idle_skip_scale_ab.csv";
    void (*set_batch)(size_t (*)(const int16_t *, size_t));
    int i, require_fires = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--csv") && i + 1 < argc)
            csv = argv[++i];
        else if (!strcmp(argv[i], "--inject-frame") && i + 1 < argc)
            g_inject_frame = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--state-every") && i + 1 < argc)
            g_state_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--require-fires"))
            require_fires = 1;
    }

    cfg.frames         = 240;
    cfg.quiet          = 1;
    cfg.video_callback = ab_video;
    cfg.frame_callback = ab_frame;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!harness_load_rom(&cfg))
        return 1;

    g_csv = fopen(csv, "w");
    if (!g_csv) {
        fprintf(stderr, "cannot open %s\n", csv);
        return 1;
    }

    set_batch = (void (*)(size_t (*)(const int16_t *, size_t)))
                harness_dlsym(&cfg, "retro_set_audio_sample_batch");
    if (set_batch)
        set_batch(ab_audio);

    dsp_ram       = (uint8_t *)((uint8_t *(*)(void))harness_dlsym(&cfg, "DSPGetRAM"))();
    p_dsp_pc      = (uint32_t *)harness_dlsym(&cfg, "dsp_pc");
    p_dsp_control = (uint32_t *)harness_dlsym(&cfg, "dsp_control");
    p_dsp_b0      = (uint32_t *)harness_dlsym(&cfg, "dsp_reg_bank_0");
    p_dsp_b1      = (uint32_t *)harness_dlsym(&cfg, "dsp_reg_bank_1");
    p_dsp_ops     = (uint32_t *)harness_dlsym(&cfg, "dsp_exec_opcode_count");
    p_dsp_fires   = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_fires");
    p_dsp_setflags = (void (*)(uint32_t))harness_dlsym(&cfg, "DSPSetFlags");
    p_gpu_pc      = (uint32_t *)harness_dlsym(&cfg, "gpu_pc");
    p_gpu_b0      = (uint32_t *)harness_dlsym(&cfg, "gpu_reg_bank_0");
    p_gpu_b1      = (uint32_t *)harness_dlsym(&cfg, "gpu_reg_bank_1");
    p_gpu_ops     = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");
    p_gpu_fires   = (uint32_t *)harness_dlsym(&cfg, "gpu_idle_skip_fires");
    if (!dsp_ram || !p_dsp_pc || !p_dsp_control || !p_dsp_b0 || !p_dsp_b1
        || !p_dsp_ops || !p_dsp_fires || !p_dsp_setflags || !p_gpu_pc
        || !p_gpu_b0 || !p_gpu_b1 || !p_gpu_ops || !p_gpu_fires) {
        fprintf(stderr, "idle_skip_scale_ab: missing test ABI symbols "
                        "(build with TEST_EXPORTS=1)\n");
        return 1;
    }

    p_serialize_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
    p_serialize      = (int (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
    if (g_state_every && p_serialize_size) {
        g_state_size = p_serialize_size();
        g_state_buf  = calloc(1, g_state_size);
    }

    harness_run(&cfg);
    fclose(g_csv);

    fprintf(stderr, "TRAILER frames=%u gpu_fires=%u dsp_fires=%u "
                    "gpu_opcodes=%u dsp_opcodes=%u\n",
            cfg.frames, *p_gpu_fires, *p_dsp_fires, *p_gpu_ops, *p_dsp_ops);
    i = 0;
    if (require_fires && (*p_gpu_fires == 0 || *p_dsp_fires == 0)) {
        fprintf(stderr, "idle_skip_scale_ab: FAIL -- the fast-forward never "
                        "fired (gpu=%u dsp=%u); an unfired skip makes "
                        "on == off vacuous\n", *p_gpu_fires, *p_dsp_fires);
        i = 3;
    }
    harness_shutdown(&cfg);
    return i;
}
