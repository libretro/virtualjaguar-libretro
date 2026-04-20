/*
 * test_framework.h — Minimal unit test framework for Virtual Jaguar.
 *
 * Usage:
 *   #include "test_framework.h"
 *
 *   TEST(my_test) {
 *       ASSERT_EQ(1 + 1, 2);
 *       ASSERT_TRUE(some_condition);
 *   }
 *
 *   int main(int argc, char *argv[]) {
 *       TEST_INIT("My Test Suite");
 *       RUN_TEST(my_test);
 *       return TEST_REPORT();
 *   }
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>

/* ------------------------------------------------------------------ */
/* Test runner state                                                    */
/* ------------------------------------------------------------------ */

static int tf_pass = 0;
static int tf_fail = 0;
static int tf_skip = 0;
static const char *tf_suite_name = "";
static const char *tf_current_test = "";
static bool tf_current_failed = false;

#define TEST_INIT(name) \
    do { tf_suite_name = (name); tf_pass = tf_fail = tf_skip = 0; \
         fprintf(stderr, "\n=== %s ===\n", tf_suite_name); } while(0)

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) \
    do { \
        tf_current_test = #name; \
        tf_current_failed = false; \
        test_##name(); \
        if (tf_current_failed) { tf_fail++; } \
        else { tf_pass++; fprintf(stderr, "  PASS  %s\n", #name); } \
    } while(0)

#define SKIP_TEST(name, reason) \
    do { tf_skip++; fprintf(stderr, "  SKIP  %s (%s)\n", #name, reason); } while(0)

#define TEST_REPORT() \
    (fprintf(stderr, "\n--- %s: %d passed, %d failed, %d skipped ---\n\n", \
             tf_suite_name, tf_pass, tf_fail, tf_skip), tf_fail)

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */

#define FAIL(fmt, ...) \
    do { \
        fprintf(stderr, "  FAIL  %s:%d: " fmt "\n", \
                tf_current_test, __LINE__, ##__VA_ARGS__); \
        tf_current_failed = true; \
        return; \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { if (!(cond)) FAIL("expected true: %s", #cond); } while(0)

#define ASSERT_FALSE(cond) \
    do { if (cond) FAIL("expected false: %s", #cond); } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        if (_a != _b) FAIL("%s == %s: got %lld (0x%llX), expected %lld (0x%llX)", \
                           #a, #b, _a, _a, _b, _b); \
    } while(0)

#define ASSERT_NEQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        if (_a == _b) FAIL("%s != %s: both are %lld (0x%llX)", #a, #b, _a, _a); \
    } while(0)

#define ASSERT_EQ_U32(a, b) \
    do { \
        uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
        if (_a != _b) FAIL("%s == %s: got 0x%08X, expected 0x%08X", #a, #b, _a, _b); \
    } while(0)

#define ASSERT_EQ_U16(a, b) \
    do { \
        uint16_t _a = (uint16_t)(a), _b = (uint16_t)(b); \
        if (_a != _b) FAIL("%s == %s: got 0x%04X, expected 0x%04X", #a, #b, _a, _b); \
    } while(0)

#define ASSERT_EQ_U8(a, b) \
    do { \
        uint8_t _a = (uint8_t)(a), _b = (uint8_t)(b); \
        if (_a != _b) FAIL("%s == %s: got 0x%02X, expected 0x%02X", #a, #b, _a, _b); \
    } while(0)

#define ASSERT_MEM_EQ(ptr, expected, len) \
    do { \
        if (memcmp((ptr), (expected), (len)) != 0) \
            FAIL("memory mismatch at %s (length %u)", #ptr, (unsigned)(len)); \
    } while(0)

/* Non-fatal check — logs failure but continues */
#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        if (_a != _b) { \
            fprintf(stderr, "  CHECK %s:%d: %s == %s: got %lld (0x%llX), expected %lld (0x%llX)\n", \
                    tf_current_test, __LINE__, #a, #b, _a, _a, _b, _b); \
            tf_current_failed = true; \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/* Core loader (dlsym-based, loads virtualjaguar_libretro.dylib)       */
/* ------------------------------------------------------------------ */

#include "../libretro-common/include/libretro.h"

struct vj_core {
    void *handle;

    /* libretro API */
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);

    /* Hardware subsystem functions */
    void (*GPUInit)(void);
    void (*GPUReset)(void);
    void (*GPUExec)(int32_t);
    void (*GPUHandleIRQs)(void);
    void (*GPUSetIRQLine)(int, int);
    uint8_t  (*GPUReadByte)(uint32_t, uint32_t);
    uint16_t (*GPUReadWord)(uint32_t, uint32_t);
    uint32_t (*GPUReadLong)(uint32_t, uint32_t);
    void (*GPUWriteByte)(uint32_t, uint8_t, uint32_t);
    void (*GPUWriteWord)(uint32_t, uint16_t, uint32_t);
    void (*GPUWriteLong)(uint32_t, uint32_t, uint32_t);
    uint32_t (*GPUGetPC)(void);
    int (*GPUIsRunning)(void);

    void (*DSPInit)(void);
    void (*DSPReset)(void);
    void (*DSPExec)(int32_t);
    void (*DSPHandleIRQs)(void);
    void (*DSPSetIRQLine)(int, int);
    uint8_t  (*DSPReadByte)(uint32_t, uint32_t);
    uint16_t (*DSPReadWord)(uint32_t, uint32_t);
    uint32_t (*DSPReadLong)(uint32_t, uint32_t);
    void (*DSPWriteByte)(uint32_t, uint8_t, uint32_t);
    void (*DSPWriteWord)(uint32_t, uint16_t, uint32_t);
    void (*DSPWriteLong)(uint32_t, uint32_t, uint32_t);

    void (*TOMInit)(void);
    void (*TOMReset)(void);
    uint16_t (*TOMReadWord)(uint32_t, uint32_t);
    void (*TOMWriteWord)(uint32_t, uint16_t, uint32_t);
    int (*TOMIRQEnabled)(int);
    uint16_t (*TOMIRQControlReg)(void);
    void (*TOMSetIRQLatch)(int, int);
    void (*TOMSetPendingVideoInt)(void);
    void (*TOMSetPendingGPUInt)(void);
    void (*TOMSetPendingTimerInt)(void);
    void (*TOMSetPendingObjectInt)(void);
    void (*TOMSetPendingJERRYInt)(void);

    void (*JERRYInit)(void);
    void (*JERRYReset)(void);
    uint16_t (*JERRYReadWord)(uint32_t, uint32_t);
    void (*JERRYWriteWord)(uint32_t, uint16_t, uint32_t);
    bool (*JERRYIRQEnabled)(int);
    void (*JERRYSetPendingIRQ)(int);

    void (*CDROMInit)(void);
    void (*CDROMReset)(void);
    uint16_t (*CDROMReadWord)(uint32_t, uint32_t);
    void (*CDROMWriteWord)(uint32_t, uint16_t, uint32_t);

    uint8_t (*JaguarReadByte)(uint32_t, uint32_t);
    uint16_t (*JaguarReadWord)(uint32_t, uint32_t);
    void (*JaguarWriteByte)(uint32_t, uint8_t, uint32_t);
    void (*JaguarWriteWord)(uint32_t, uint16_t, uint32_t);
    void (*JaguarWriteLong)(uint32_t, uint32_t, uint32_t);

    void (*JaguarInit)(void);
    void (*JaguarReset)(void);

    /* m68k access */
    unsigned int (*m68k_get_reg)(void *, int);
    void (*m68k_set_reg)(int, unsigned int);
    int (*m68k_execute)(int);
    void (*m68k_pulse_reset)(void);

    /* Raw memory pointer */
    uint8_t * (*GetRamPtr)(void);

    /* GPU register banks (exported arrays) */
    uint32_t *gpu_reg_bank_0;
    uint32_t *dsp_reg_bank_0;
    uint32_t *gpu_reg_bank_1;

    /* Settings */
    void *vjs;
};

#define LOAD_SYM(coreptr, sym) \
    do { \
        (coreptr)->sym = dlsym((coreptr)->handle, #sym); \
        if (!(coreptr)->sym) { \
            fprintf(stderr, "  WARN: dlsym(%s) failed: %s\n", #sym, dlerror()); \
        } \
    } while(0)

#define LOAD_SYM_REQUIRED(coreptr, sym) \
    do { \
        (coreptr)->sym = dlsym((coreptr)->handle, #sym); \
        if (!(coreptr)->sym) { \
            fprintf(stderr, "  FATAL: dlsym(%s) failed: %s\n", #sym, dlerror()); \
            return false; \
        } \
    } while(0)

/* Stub callbacks for libretro */
static void tf_video_refresh(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void tf_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t tf_audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void tf_input_poll(void) {}
static int16_t tf_input_state(unsigned p, unsigned d, unsigned i, unsigned id) { (void)p; (void)d; (void)i; (void)id; return 0; }

static bool tf_environment(unsigned cmd, void *data)
{
    switch (cmd & 0xFF)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = ".";
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        struct retro_variable *var = (struct retro_variable *)data;
        if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
        { var->value = "disabled"; return true; }
        if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
        { var->value = "enabled"; return true; }
        if (var->key && strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)
        { var->value = "hle"; return true; }
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

static bool vj_core_load(struct vj_core *core)
{
    memset(core, 0, sizeof(*core));

#ifdef __APPLE__
    const char *lib = "./virtualjaguar_libretro.dylib";
#elif defined(_WIN32)
    const char *lib = "./virtualjaguar_libretro.dll";
#else
    const char *lib = "./virtualjaguar_libretro.so";
#endif

    core->handle = dlopen(lib, RTLD_LAZY);
    if (!core->handle)
    {
        fprintf(stderr, "FATAL: dlopen(%s): %s\n", lib, dlerror());
        return false;
    }

    /* libretro API */
    LOAD_SYM_REQUIRED(core, retro_init);
    LOAD_SYM_REQUIRED(core, retro_deinit);
    LOAD_SYM_REQUIRED(core, retro_set_environment);
    LOAD_SYM_REQUIRED(core, retro_set_video_refresh);
    LOAD_SYM_REQUIRED(core, retro_set_audio_sample);
    LOAD_SYM_REQUIRED(core, retro_set_audio_sample_batch);
    LOAD_SYM_REQUIRED(core, retro_set_input_poll);
    LOAD_SYM_REQUIRED(core, retro_set_input_state);

    /* GPU */
    LOAD_SYM(core, GPUInit);
    LOAD_SYM(core, GPUReset);
    LOAD_SYM(core, GPUExec);
    LOAD_SYM(core, GPUHandleIRQs);
    LOAD_SYM(core, GPUSetIRQLine);
    LOAD_SYM(core, GPUReadByte);
    LOAD_SYM(core, GPUReadWord);
    LOAD_SYM(core, GPUReadLong);
    LOAD_SYM(core, GPUWriteByte);
    LOAD_SYM(core, GPUWriteWord);
    LOAD_SYM(core, GPUWriteLong);
    LOAD_SYM(core, GPUGetPC);
    LOAD_SYM(core, GPUIsRunning);

    /* DSP */
    LOAD_SYM(core, DSPInit);
    LOAD_SYM(core, DSPReset);
    LOAD_SYM(core, DSPExec);
    LOAD_SYM(core, DSPHandleIRQs);
    LOAD_SYM(core, DSPSetIRQLine);
    LOAD_SYM(core, DSPReadByte);
    LOAD_SYM(core, DSPReadWord);
    LOAD_SYM(core, DSPReadLong);
    LOAD_SYM(core, DSPWriteByte);
    LOAD_SYM(core, DSPWriteWord);
    LOAD_SYM(core, DSPWriteLong);

    /* TOM */
    LOAD_SYM(core, TOMInit);
    LOAD_SYM(core, TOMReset);
    LOAD_SYM(core, TOMReadWord);
    LOAD_SYM(core, TOMWriteWord);
    LOAD_SYM(core, TOMIRQEnabled);
    LOAD_SYM(core, TOMIRQControlReg);
    LOAD_SYM(core, TOMSetIRQLatch);
    LOAD_SYM(core, TOMSetPendingVideoInt);
    LOAD_SYM(core, TOMSetPendingGPUInt);
    LOAD_SYM(core, TOMSetPendingTimerInt);
    LOAD_SYM(core, TOMSetPendingObjectInt);
    LOAD_SYM(core, TOMSetPendingJERRYInt);

    /* JERRY */
    LOAD_SYM(core, JERRYInit);
    LOAD_SYM(core, JERRYReset);
    LOAD_SYM(core, JERRYReadWord);
    LOAD_SYM(core, JERRYWriteWord);
    LOAD_SYM(core, JERRYIRQEnabled);
    LOAD_SYM(core, JERRYSetPendingIRQ);

    /* CDROM */
    LOAD_SYM(core, CDROMInit);
    LOAD_SYM(core, CDROMReset);
    LOAD_SYM(core, CDROMReadWord);
    LOAD_SYM(core, CDROMWriteWord);

    /* Jaguar core */
    LOAD_SYM(core, JaguarReadByte);
    LOAD_SYM(core, JaguarReadWord);
    LOAD_SYM(core, JaguarWriteByte);
    LOAD_SYM(core, JaguarWriteWord);
    LOAD_SYM(core, JaguarWriteLong);
    LOAD_SYM(core, JaguarInit);
    LOAD_SYM(core, JaguarReset);

    /* m68k */
    LOAD_SYM(core, m68k_get_reg);
    LOAD_SYM(core, m68k_set_reg);
    LOAD_SYM(core, m68k_execute);
    LOAD_SYM(core, m68k_pulse_reset);

    /* Memory */
    LOAD_SYM(core, GetRamPtr);

    /* Exported data */
    core->gpu_reg_bank_0 = dlsym(core->handle, "gpu_reg_bank_0");
    core->dsp_reg_bank_0 = dlsym(core->handle, "dsp_reg_bank_0");
    core->gpu_reg_bank_1 = dlsym(core->handle, "gpu_reg_bank_1");
    core->vjs = dlsym(core->handle, "vjs");

    return true;
}

static void vj_core_init(struct vj_core *core)
{
    core->retro_set_environment(tf_environment);
    core->retro_set_video_refresh(tf_video_refresh);
    core->retro_set_audio_sample(tf_audio_sample);
    core->retro_set_audio_sample_batch(tf_audio_sample_batch);
    core->retro_set_input_poll(tf_input_poll);
    core->retro_set_input_state(tf_input_state);
    core->retro_init();
}

static void vj_core_unload(struct vj_core *core)
{
    if (core->retro_deinit) core->retro_deinit();
    if (core->handle) dlclose(core->handle);
    memset(core, 0, sizeof(*core));
}

/* ------------------------------------------------------------------ */
/* GPU/DSP instruction encoding helpers                                */
/* ------------------------------------------------------------------ */

/* Jaguar GPU/DSP instruction format: 6-bit opcode | 5-bit src | 5-bit dst
 * Bits: [15:10] opcode  [9:5] src_reg  [4:0] dst_reg */
static inline uint16_t gpu_encode(uint8_t opcode, uint8_t src, uint8_t dst)
{
    return (uint16_t)((opcode & 0x3F) << 10) | ((src & 0x1F) << 5) | (dst & 0x1F);
}

/* MOVEI: opcode 38, followed by 32-bit immediate (low word first) */
static inline void gpu_write_movei(struct vj_core *c, uint32_t addr,
                                   uint8_t dst, uint32_t imm)
{
    c->GPUWriteWord(addr,     gpu_encode(38, 0, dst), 0);
    c->GPUWriteWord(addr + 2, (uint16_t)(imm & 0xFFFF), 0);
    c->GPUWriteWord(addr + 4, (uint16_t)(imm >> 16), 0);
}

/* NOP: opcode 57 */
#define GPU_NOP  gpu_encode(57, 0, 0)

/* Common opcodes */
#define GPU_OP_ADD     0
#define GPU_OP_ADDC    1
#define GPU_OP_ADDQ    2
#define GPU_OP_ADDQT   3
#define GPU_OP_SUB     4
#define GPU_OP_SUBC    5
#define GPU_OP_SUBQ    6
#define GPU_OP_SUBQT   7
#define GPU_OP_NEG     8
#define GPU_OP_AND     9
#define GPU_OP_OR     10
#define GPU_OP_XOR    11
#define GPU_OP_NOT    12
#define GPU_OP_BTST   13
#define GPU_OP_BSET   14
#define GPU_OP_BCLR   15
#define GPU_OP_MULT   16
#define GPU_OP_IMULT  17
#define GPU_OP_IMULTN 18
#define GPU_OP_RESMAC 19
#define GPU_OP_IMACN  20
#define GPU_OP_DIV    21
#define GPU_OP_ABS    22
#define GPU_OP_SH     23
#define GPU_OP_SHLQ   24
#define GPU_OP_SHRQ   25
#define GPU_OP_SHA    26
#define GPU_OP_SHARQ  27
#define GPU_OP_ROR    28
#define GPU_OP_RORQ   29
#define GPU_OP_CMP    30
#define GPU_OP_CMPQ   31
#define GPU_OP_SAT8   32
#define GPU_OP_SAT16  33
#define GPU_OP_MOVE   34
#define GPU_OP_MOVEQ  35
#define GPU_OP_MOVETA 36
#define GPU_OP_MOVEFA 37
#define GPU_OP_MOVEI  38
#define GPU_OP_LOADB  39
#define GPU_OP_LOADW  40
#define GPU_OP_LOAD   41
#define GPU_OP_LOADP  42
#define GPU_OP_SAT24  42
#define GPU_OP_LOAD14I 43
#define GPU_OP_LOAD15I 44
#define GPU_OP_STOREB 45
#define GPU_OP_STOREW 46
#define GPU_OP_STORE  47
#define GPU_OP_STOREP 48
#define GPU_OP_STORE14I 49
#define GPU_OP_STORE15I 50
#define GPU_OP_MOVPC  51
#define GPU_OP_JR     52
#define GPU_OP_JUMP   53
#define GPU_OP_MMULT  54
#define GPU_OP_MTOI   55
#define GPU_OP_NORMI  56
#define GPU_OP_NOP    57
#define GPU_OP_LOAD14R 58
#define GPU_OP_LOAD15R 59
#define GPU_OP_STORE14R 60
#define GPU_OP_STORE15R 61
#define GPU_OP_SAT16S 62
#define GPU_OP_PACK   63

/* GPU register addresses for control regs */
#define GPU_FLAGS_REG   0xF02100
#define GPU_MTXC_REG    0xF02104
#define GPU_MTXA_REG    0xF02108
#define GPU_END_REG     0xF0210C
#define GPU_PC_REG      0xF02110
#define GPU_CTRL_REG    0xF02114
#define GPU_HIDATA_REG  0xF02118

/* GPU flag bits in G_FLAGS ($F02100) */
#define GPU_FLAG_ZERO   0x0001
#define GPU_FLAG_CARRY  0x0002
#define GPU_FLAG_NEGA   0x0004
#define GPU_FLAG_IMASK  0x0008

/* DSP register addresses */
#define DSP_FLAGS_REG   0xF1A100
#define DSP_CTRL_REG    0xF1A114
#define DSP_PC_REG      0xF1A110
#define DSP_RAM_BASE    0xF1B000

/* Pad remaining program space with NOPs up to a max address */
static void gpu_fill_nops(struct vj_core *c, uint32_t from, uint32_t to)
{
    for (uint32_t a = from; a < to; a += 2)
        c->GPUWriteWord(a, GPU_NOP, 0);
}

/* Execute a GPU program: set PC, start GPU, run N cycles, then stop.
 * The program should be short enough to complete within cycle_budget. */
static void gpu_run_program(struct vj_core *c, uint32_t pc_addr)
{
    c->GPUWriteLong(GPU_PC_REG, pc_addr, 0);
    c->GPUWriteLong(GPU_CTRL_REG, 1, 0);  /* GPUGO */
    c->GPUExec(200);
    c->GPUWriteLong(GPU_CTRL_REG, 0, 0);  /* stop */
}

/* Read GPU flags register */
static uint32_t gpu_read_flags(struct vj_core *c)
{
    return c->GPUReadLong(GPU_FLAGS_REG, 0);
}

#endif /* TEST_FRAMEWORK_H */
