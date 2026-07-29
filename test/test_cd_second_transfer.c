/*
 * test_cd_second_transfer.c -- CD second-transfer liveness contract (Task 8).
 *
 * test_cd_fifo_stream proves the FIRST boot-stub load delivers byte-exact
 * payload.  This test covers the next failure class: the SECOND (and every
 * later) transfer wedging because a GPU interrupt dispatch was clobbered by
 * a branch delay slot.
 *
 * Mechanism (diagnosed on Primal Rage bios + BrainDead 13 bios, plus a
 * user-captured iOS device trace): the CD streaming ISR epilogue idiom is
 * `JUMP T,(Rret)` with `STORE Rflags,(G_FLAGS)` in the DELAY SLOT -- the
 * store clears IMASK and acks INT_CLR1.  gpu.c dispatches pending IRQs
 * synchronously from that store (IMASKCleared -> GPUHandleIRQs); the
 * dispatch ran INSIDE gpu_opcode_jump's inline delay slot, and the jump
 * then overwrote gpu_pc with the branch target -- clobbering the vector.
 * IMASK stayed set forever, the CD ISR never ran again, FIFO drains froze,
 * and both the GPU foreground mailbox spin and the 68K CD_read service
 * loop starved (video_stall / cd_seek_wedge).
 *
 * Contract asserted here: while a CD transfer is in flight, the core never
 * enters the "IMASK stuck" state -- G_FLAGS.IMASK observed set on many
 * consecutive frame boundaries while the GPU is running and FIFO drain
 * progress is frozen.  A real ISR lasts microseconds; IMASK visible at
 * WEDGE_WINDOW successive frame boundaries with zero drain progress means
 * interrupt delivery is dead.
 *
 * SKIPs cleanly when no disc / CD BIOS is available (mirrors the other
 * test_cd_* harnesses).
 *
 * Build:
 *   make -j8 TEST_EXPORTS=1 && cc -O2 -Wall -std=c99 \
 *       -I./libretro-common/include \
 *       -o test/test_cd_second_transfer test/test_cd_second_transfer.c -ldl
 * Run (local, needs disc + CD BIOS under test/roms/private):
 *   VJ_FIFO_DISC="test/roms/private/Primal Rage (USA)/Primal Rage (USA).cue" \
 *       test/test_cd_second_transfer
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
static void     (*p_CDROMDiagGetSeekWedgeState)(uint32_t *, uint32_t *, uint32_t *);
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);
static bool     (*p_GPUIsRunning)(void);

#define G_FLAGS_ADDR  0x00F02100u
#define G_FLAGS_IMASK 0x00000008u

/* Frames of continuous (IMASK set + GPU running + drains frozen) that count
 * as a dead interrupt path.  The real wedge holds this state forever; a
 * healthy ISR clears IMASK within microseconds, so IMASK is essentially
 * never visible at a frame boundary twice in a row -- 120 frames (~2 s) is
 * decisively past any legitimate pause between transfers. */
#define WEDGE_WINDOW 120u

TEST(second_transfer_liveness)
{
    const char *disc = getenv("VJ_FIFO_DISC");
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    void (*p_retro_run)(void);
    void (*p_retro_unload_game)(void);
    uint32_t f, frames;
    uint32_t starts, dones, drains;
    uint32_t lastDrains, stuckFrames, worstStuck;
    uint32_t stuckStarts, stuckDrains, stuckFlags;

    if (!disc || !disc[0]) {
        SKIP_TEST(second_transfer_liveness, "set VJ_FIFO_DISC to a CD image path");
        return;
    }

    p_retro_load_game   = dlsym(C.handle, "retro_load_game");
    p_retro_run         = dlsym(C.handle, "retro_run");
    p_retro_unload_game = dlsym(C.handle, "retro_unload_game");
    p_CDROMDiagGetSeekWedgeState = dlsym(C.handle, "CDROMDiagGetSeekWedgeState");
    p_GPUReadLong                = dlsym(C.handle, "GPUReadLong");
    p_GPUIsRunning               = dlsym(C.handle, "GPUIsRunning");
    if (!p_retro_load_game || !p_retro_run
        || !p_CDROMDiagGetSeekWedgeState || !p_GPUReadLong || !p_GPUIsRunning) {
        SKIP_TEST(second_transfer_liveness,
                  "core missing required exports (build with TEST_EXPORTS=1)");
        return;
    }

    memset(&info, 0, sizeof(info));
    info.path = disc;
    if (!p_retro_load_game(&info)) {
        SKIP_TEST(second_transfer_liveness,
                  "retro_load_game failed (CD BIOS missing or disc parse failed)");
        return;
    }

    frames = 1500;
    {
        const char *fe = getenv("VJ_FIFO_FRAMES");
        if (fe && fe[0]) frames = (uint32_t)atoi(fe);
    }

    starts = dones = drains = 0;
    lastDrains = 0;
    stuckFrames = 0;
    worstStuck = 0;
    stuckStarts = stuckDrains = stuckFlags = 0;

    for (f = 0; f < frames; f++) {
        uint32_t flags;
        p_retro_run();
        p_CDROMDiagGetSeekWedgeState(&starts, &dones, &drains);
        flags = p_GPUReadLong(G_FLAGS_ADDR, 0);

        if (starts > 0 && (flags & G_FLAGS_IMASK)
            && p_GPUIsRunning() && drains == lastDrains) {
            stuckFrames++;
            if (stuckFrames > worstStuck) {
                worstStuck  = stuckFrames;
                stuckStarts = starts;
                stuckDrains = drains;
                stuckFlags  = flags;
            }
        } else {
            stuckFrames = 0;
        }
        lastDrains = drains;

        if (worstStuck >= WEDGE_WINDOW)
            break;  /* decisively wedged -- no need to run out the clock */
    }

    fprintf(stderr,
            "    frames=%u seek_starts=%u seek_dones=%u fifo_drains=%u "
            "worst IMASK-stuck window=%u frames\n",
            f, starts, dones, drains, worstStuck);

    if (p_retro_unload_game)
        p_retro_unload_game();

    if (worstStuck >= WEDGE_WINDOW)
        FAIL("GPU interrupt delivery died mid-transfer: IMASK stuck set for "
             "%u consecutive frames while the GPU ran and FIFO drains were "
             "frozen (seek_starts=%u drains=%u G_FLAGS=$%08X) -- the CD ISR "
             "dispatch was clobbered (delay-slot IRQ hazard)",
             worstStuck, stuckStarts, stuckDrains, stuckFlags);
}

int main(int argc, char *argv[])
{
    void (*p_retro_deinit)(void);
    (void)argc; (void)argv;

    TEST_INIT("CD second-transfer liveness contract");

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

    RUN_TEST(second_transfer_liveness);

    p_retro_deinit = dlsym(C.handle, "retro_deinit");
    if (p_retro_deinit) p_retro_deinit();
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
