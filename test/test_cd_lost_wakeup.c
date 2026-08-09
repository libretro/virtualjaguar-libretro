/*
 * test_cd_lost_wakeup.c -- 68K/GPU coprocessor-handshake lost-wakeup contract.
 *
 * Covers the failure class diagnosed on BrainDead 13 (bios mode): the game's
 * FMV engine performs the standard Jaguar coprocessor handshake --
 *
 *     68K: clr.w done-flag ; move.l #cmd,(GPU mailbox) ; stop #$2000
 *     GPU: decode command  ; store #3,(G_CTRL)          ; back to idle
 *     68K: level-2 IRQ handler sets done-flag ; rte ; resumes past stop
 *
 * On silicon this is airtight: move.l -> stop is ~6 CPU cycles, the GPU's
 * decode path to the CPUINT store is thousands.  A coarse-slice scheduler
 * can deliver the GPU's one-shot CPUINT *before* the 68K executes its
 * `stop`, so the wakeup is consumed early and the 68K halts forever waiting
 * for an interrupt that already came and went.
 *
 * Contract asserted here: the 68K never sits halted in STOP with ZERO
 * interrupt deliveries for WEDGE_WINDOW consecutive frames while CD I/O has
 * occurred.  A healthy stop-waiter is woken by whatever recurring or
 * one-shot source it enabled within milliseconds; five seconds of "halted,
 * nothing serviced" means the machine is dead.
 *
 * SKIPs cleanly when no disc / CD BIOS is available (mirrors the other
 * test_cd_* harnesses).
 *
 * Build:
 *   make -j8 TEST_EXPORTS=1 && cc -O2 -Wall -std=c99 \
 *       -I./libretro-common/include \
 *       -o test/test_cd_lost_wakeup test/test_cd_lost_wakeup.c -ldl
 * Run (local, needs disc + CD BIOS under test/roms/private):
 *   VJ_FIFO_DISC="test/roms/private/BrainDead 13 (USA)/BrainDead 13 (USA)/BrainDead 13 (USA).cue" \
 *       test/test_cd_lost_wakeup
 */

#include "test_framework.h"
#include "../libretro-common/include/libretro.h"

#include <dlfcn.h>

static struct vj_core C;
static const char *g_system_dir = "test/roms/private";

/* ---- libretro environment: force real-BIOS CD boot mode ---------------- */
static bool st_environment(unsigned cmd, void *data)
{
    switch (cmd & 0xFF) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
        const char *root = getenv("VJ_TEST_CD_ROOT");
        *(const char **)data = (root && root[0]) ? root : g_system_dir;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = ".";
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        if (!var || !var->key) return false;
        if (strcmp(var->key, "virtualjaguar_bios") == 0)           { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0) { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)   { var->value = "retail"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)   { var->value = "bios"; return true; }
        var->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    default:
        return false;
    }
}

static void st_video(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void st_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t st_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void st_input_poll(void) {}
static int16_t st_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* ---- exported internals (TEST_EXPORTS builds) --------------------------- */
static void         (*p_CDROMDiagGetSeekWedgeState)(uint32_t *, uint32_t *, uint32_t *);
static unsigned int (*p_m68k_is_stopped)(void);
static unsigned int (*p_m68k_diag_interrupt_count)(void);
static unsigned int (*p_m68k_get_reg)(void *, int);

#define M68K_REG_PC_IDX 16  /* D0-D7 = 0-7, A0-A7 = 8-15, PC = 16 */

/* Frames of continuous (68K halted in STOP + zero interrupts serviced) that
 * count as a dead machine.  Any healthy stop-waiter is woken within
 * milliseconds by the source it enabled (VBlank at 60 Hz, a GPU command
 * completion within a frame); each wake increments the interrupt count.
 * 300 frames (~5 s) with the CPU halted and NOTHING serviced is decisively
 * a lost wakeup, not a pause. */
#define WEDGE_WINDOW 300u

TEST(lost_wakeup_liveness)
{
    const char *disc = getenv("VJ_FIFO_DISC");
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    void (*p_retro_run)(void);
    void (*p_retro_unload_game)(void);
    uint32_t f, frames;
    uint32_t starts, dones, drains;
    uint32_t lastIrqs, stuckFrames, worstStuck;
    uint32_t stuckPC, stuckIrqs;

    if (!disc || !disc[0]) {
        SKIP_TEST(lost_wakeup_liveness, "set VJ_FIFO_DISC to a CD image path");
        return;
    }

    p_retro_load_game   = dlsym(C.handle, "retro_load_game");
    p_retro_run         = dlsym(C.handle, "retro_run");
    p_retro_unload_game = dlsym(C.handle, "retro_unload_game");
    p_CDROMDiagGetSeekWedgeState = dlsym(C.handle, "CDROMDiagGetSeekWedgeState");
    p_m68k_is_stopped       = dlsym(C.handle, "m68k_is_stopped");
    p_m68k_diag_interrupt_count  = dlsym(C.handle, "m68k_diag_interrupt_count");
    p_m68k_get_reg               = (unsigned int (*)(void *, int))dlsym(C.handle, "m68k_get_reg");
    if (!p_retro_load_game || !p_retro_run
        || !p_CDROMDiagGetSeekWedgeState || !p_m68k_is_stopped
        || !p_m68k_diag_interrupt_count || !p_m68k_get_reg) {
        SKIP_TEST(lost_wakeup_liveness,
                  "core missing required exports (build with TEST_EXPORTS=1)");
        return;
    }

    memset(&info, 0, sizeof(info));
    info.path = disc;
    if (!p_retro_load_game(&info)) {
        SKIP_TEST(lost_wakeup_liveness,
                  "retro_load_game failed (CD BIOS missing or disc parse failed)");
        return;
    }

    frames = 2400;
    {
        const char *fe = getenv("VJ_FIFO_FRAMES");
        if (fe && fe[0]) frames = (uint32_t)atoi(fe);
    }

    starts = dones = drains = 0;
    lastIrqs = 0;
    stuckFrames = 0;
    worstStuck = 0;
    stuckPC = stuckIrqs = 0;

    for (f = 0; f < frames; f++) {
        uint32_t irqs, stopped;
        p_retro_run();
        p_CDROMDiagGetSeekWedgeState(&starts, &dones, &drains);
        irqs    = p_m68k_diag_interrupt_count();
        stopped = p_m68k_is_stopped();

        if (starts > 0 && stopped && irqs == lastIrqs) {
            stuckFrames++;
            if (stuckFrames > worstStuck) {
                worstStuck = stuckFrames;
                stuckPC    = p_m68k_get_reg(NULL, M68K_REG_PC_IDX);
                stuckIrqs  = irqs;
            }
        } else {
            stuckFrames = 0;
        }
        lastIrqs = irqs;

        if (worstStuck >= WEDGE_WINDOW)
            break;  /* decisively dead -- no need to run out the clock */
    }

    fprintf(stderr,
            "    frames=%u seek_starts=%u seek_dones=%u fifo_drains=%u "
            "worst halted-no-IRQ window=%u frames (pc=$%06X irqs=%u)\n",
            f, starts, dones, drains, worstStuck, stuckPC, stuckIrqs);

    if (p_retro_unload_game)
        p_retro_unload_game();

    if (worstStuck >= WEDGE_WINDOW)
        FAIL("lost wakeup: 68K halted in STOP for %u consecutive frames with "
             "zero interrupts serviced (pc=$%06X, %u IRQs total) -- a one-shot "
             "GPU CPUINT was delivered before the 68K executed its stop",
             worstStuck, stuckPC, stuckIrqs);
}

int main(int argc, char *argv[])
{
    void (*p_retro_deinit)(void);
    (void)argc; (void)argv;

    TEST_INIT("CD coprocessor-handshake lost-wakeup contract");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(st_environment);
    C.retro_set_video_refresh(st_video);
    C.retro_set_audio_sample(st_audio);
    C.retro_set_audio_sample_batch(st_audio_batch);
    C.retro_set_input_poll(st_input_poll);
    C.retro_set_input_state(st_input_state);
    C.retro_init();

    RUN_TEST(lost_wakeup_liveness);

    p_retro_deinit = dlsym(C.handle, "retro_deinit");
    if (p_retro_deinit) p_retro_deinit();
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
