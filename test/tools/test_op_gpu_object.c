/*
 * test_op_gpu_object.c — Object Processor GPU-object (type 2) semantics.
 *
 * Hardware contract (JTRM "Graphics Processor Object" + MAME + observed
 * game behavior): when the OP reaches a GPU object it latches the phrase
 * into OB, interrupts the GPU (IRQ3), and halts.  The GPU's service
 * routine releases the OP by writing the object flag OBF ($F00026); the
 * OP then continues with the next sequential phrase (single-phrase
 * object, no link field).  The YPOS field is NOT honored by the silicon —
 * games gate the object with BRANCH objects instead (yarc: BRANCH VC==506
 * in front of a stale-YPOS object; Primal Rage: a YPOS=0 object gated to
 * halflines >= 352).
 *
 * Regression under test: OPProcessList() stopped at every GPU object and
 * never resumed, so all objects after one were dropped for that line.
 * Primal Rage (Jaguar CD) fight scenes rendered the bottom third of the
 * screen black — every object behind the halfline>=352 GPU object died.
 *
 * Needs a TEST_EXPORTS=1 core (make test builds one) and any loadable
 * ROM (test/roms/yarc.j64, committed).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/test_op_gpu_object test/tools/test_op_gpu_object.c \
 *      test/harness/harness.c -ldl -lm
 * Run:
 *   ./test/tools/test_op_gpu_object ./virtualjaguar_libretro.dylib \
 *      test/roms/yarc.j64
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define LIST_BASE   0x10000u   /* phrase-aligned scratch in main RAM */
#define GPU_OBJ     (LIST_BASE + 0x08)
#define BMP_OBJ     (LIST_BASE + 0x10)  /* double-phrase aligned */
#define STOP_OBJ    (LIST_BASE + 0x20)
#define PIX_DATA    (LIST_BASE + 0x800)

#define TEST_HALFLINE 100
#define BMP_XPOS      8

/* GPU addresses */
#define G_FLAGS  0xF02100u
#define G_PC     0xF02110u
#define G_CTRL   0xF02114u
#define ISR_BASE 0xF03030u     /* IRQ3 vector: GPU RAM base + 3*$10 */
#define SPIN_BASE 0xF03100u

static uint8_t *g_tom;
static uint8_t *g_ram;
/* Must match OPProcessList's exact prototype (int, bool) — UBSan's
 * function-type-mismatch check rejects calls through a differently
 * typed pointer.  boolean.h resolves bool to <stdbool.h>'s _Bool on
 * every non-ancient-MSVC platform, same as harness.h pulls in here. */
static void (*g_opproc)(int halfline, bool render);
static void (*g_gwl)(uint32_t addr, uint32_t data, uint32_t who);

static void put_phrase(uint32_t addr, uint64_t v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        g_ram[addr + i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

static void set_olp(uint32_t addr)
{
    /* OLP is LO word at $20, HI word at $22, each stored big-endian */
    g_tom[0x20] = (uint8_t)((addr >> 8) & 0xFF);
    g_tom[0x21] = (uint8_t)(addr & 0xFF);
    g_tom[0x22] = (uint8_t)((addr >> 24) & 0xFF);
    g_tom[0x23] = (uint8_t)((addr >> 16) & 0xFF);
}

static uint64_t gpu_obj_p0(unsigned ypos)
{
    return 0x02ull | ((uint64_t)(ypos & 0x7FF) << 3);
}

/* Build: [GPU_OBJ ypos=0][BITMAP 4px 16bpp @ x=8, link->STOP][STOP].
 * List entry point is GPU_OBJ.  This is the Primal Rage shape: the GPU
 * object carries YPOS=0 and the objects that matter come after it. */
static void build_list(void)
{
    uint64_t p0, p1;

    put_phrase(GPU_OBJ, gpu_obj_p0(0));

    p0 = 0;                                       /* type 0 = BITMAP */
    p0 |= (uint64_t)(TEST_HALFLINE & 0x7FF) << 3; /* ypos */
    p0 |= (uint64_t)1 << 14;                      /* height = 1 */
    p0 |= (uint64_t)(STOP_OBJ & 0x3FFFF8) << 21;  /* link */
    p0 |= (uint64_t)(PIX_DATA & 0xFFFFF8) << 40;  /* data */
    put_phrase(BMP_OBJ, p0);

    p1 = 0;
    p1 |= (uint64_t)(BMP_XPOS & 0xFFF);           /* xpos */
    p1 |= (uint64_t)4 << 12;                      /* depth = 16bpp */
    p1 |= (uint64_t)1 << 15;                      /* pitch = 1 phrase */
    p1 |= (uint64_t)1 << 18;                      /* dwidth = 1 */
    p1 |= (uint64_t)1 << 28;                      /* iwidth = 1 */
    put_phrase(BMP_OBJ + 8, p1);

    put_phrase(STOP_OBJ, 0x04ull);                /* STOP, no IRQ */

    /* 4 pixels of recognizable data */
    memcpy(&g_ram[PIX_DATA], "\x12\x34\x56\x78\x9A\xBC\xDE\xF0", 8);

    set_olp(GPU_OBJ);
}

static void clear_linebuffer(void)
{
    memset(&g_tom[0x1800], 0, 1440);
}

static int linebuffer_has_pixels(void)
{
    /* 16bpp render writes pixels at lbuf 0x1800 + xpos*2 */
    return memcmp(&g_tom[0x1800 + BMP_XPOS * 2],
                  "\x12\x34\x56\x78\x9A\xBC\xDE\xF0", 8) == 0;
}

/* OB ($F00010-$F00017) holds the latched phrase LOW long first: the low
 * long at $F00010, the high long at $F00014, each big-endian.  For a
 * GPUOBJ that puts the DATA field (JTRM Rev 8: phrase bits 14-63) at
 * $F00014 and leaves TYPE/YPOS at $F00010 -- see OPSetCurrentObject()
 * in src/tom/op.c and issue #354. */
static int ob_holds(uint64_t p0)
{
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t lo = (uint8_t)((p0 >> ((3 - i) * 8)) & 0xFF);
        uint8_t hi = (uint8_t)((p0 >> (32 + (3 - i) * 8)) & 0xFF);
        if (g_tom[0x10 + i] != lo || g_tom[0x14 + i] != hi)
            return 0;
    }
    return 1;
}

/* Install a minimal GPU program:
 *   IRQ3 vector ($F03030): movei #$F00026,r10; moveq #1,r11;
 *                          storew r11,(r10); then spin forever.
 *   Main loop  ($F03100): spin forever.
 * Instruction encoding: (opcode<<10)|(reg1<<5)|reg2; movei carries the
 * 32-bit immediate as two following words, LSW first. */
static void install_gpu_release_isr(void)
{
    /* ISR words (16-bit, big-endian in GPU RAM), written as long pairs:
     *   movei #$F00026,r10 ; moveq #1,r11 ; storew r11,(r10)
     *   movei #spin,r20 ; jump (r20) ; nop  */
    g_gwl(ISR_BASE + 0,  0x980A0026u, 0); /* movei r10 | imm low         */
    g_gwl(ISR_BASE + 4,  0x00F08C2Bu, 0); /* imm high | moveq #1,r11     */
    g_gwl(ISR_BASE + 8,  0xB94B9814u, 0); /* storew r11,(r10) | movei r20 */
    g_gwl(ISR_BASE + 12,
          (((ISR_BASE + 16) & 0xFFFFu) << 16) | ((ISR_BASE + 16) >> 16), 0);
    g_gwl(ISR_BASE + 16, 0xD280E400u, 0); /* jump (r20) | nop            */

    /* Main loop: movei #spin,r20 ; jump (r20) ; nop */
    g_gwl(SPIN_BASE + 0,
          0x98140000u | ((SPIN_BASE + 6) & 0xFFFFu), 0);
    g_gwl(SPIN_BASE + 4,
          (((SPIN_BASE + 6) >> 16) << 16) | 0xD280u, 0);
    g_gwl(SPIN_BASE + 8, 0xE400E400u, 0); /* nop | nop                   */

    /* Clean slate: stop the GPU, clear all interrupt latches (CINT0-4
     *  write-1-to-clear bits) with all enables off and IMASK clear —
     *  the ROM may have been frozen mid-ISR with stale latches. */
    g_gwl(G_CTRL, 0x00, 0);
    g_gwl(G_FLAGS, 0x3E00, 0);

    g_gwl(G_FLAGS, 0x80, 0);          /* enable IRQ3 (OP), IMASK clear */
    g_gwl(G_PC, SPIN_BASE, 0);
    g_gwl(G_CTRL, 0x01, 0);           /* GO */
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[2];
    unsigned nres = 0;
    int pass_halt, pass_resume;

    cfg.frames = 2;
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path) cfg.rom_path = "test/roms/yarc.j64";
    if (!harness_load_rom(&cfg)) return 1;
    harness_run(&cfg);

    g_tom = (uint8_t *)dlsym(cfg.core_handle, "tomRam8");
    {
        /* jaguarMainRAM is a pointer variable (into jagMemSpace), not an
         * array — dereference the symbol. */
        uint8_t **ramp = (uint8_t **)dlsym(cfg.core_handle, "jaguarMainRAM");
        g_ram = ramp ? *ramp : NULL;
    }
    g_opproc = (void (*)(int, bool))dlsym(cfg.core_handle, "OPProcessList");
    g_gwl = (void (*)(uint32_t, uint32_t, uint32_t))
        dlsym(cfg.core_handle, "GPUWriteLong");
    if (!g_tom || !g_ram || !g_opproc || !g_gwl) {
        fprintf(stderr, "test_op_gpu_object: needs TEST_EXPORTS=1 core\n");
        return 1;
    }

    /* 1. GPU stopped: the OP halts at the GPU object (nothing can release
     *    it), latching it into OB.  Objects after it don't render. */
    g_gwl(G_CTRL, 0x00, 0);           /* stop whatever the ROM started */
    build_list();
    clear_linebuffer();
    g_opproc(TEST_HALFLINE, 1);
    pass_halt = !linebuffer_has_pixels() && ob_holds(gpu_obj_p0(0));
    results[nres].status = pass_halt ? "PASS" : "FAIL";
    results[nres].name   = "gpu_object_halts_unserviced";
    results[nres].detail = pass_halt
        ? "GPU idle: object latched into OB, list halted"
        : "GPU idle: halt/OB-latch behavior broken";
    nres++;

    /* 2. GPU running with an IRQ3 ISR that writes OBF: the OP must resume
     *    and render the bitmap that follows the GPU object.  This is the
     *    Primal Rage arena-floor case. */
    install_gpu_release_isr();
    build_list();
    clear_linebuffer();
    g_opproc(TEST_HALFLINE, 1);
    /* OB is expected to hold the STOP object afterwards (the OP latches
     * every GPU/STOP object it reaches) — the proof of the resume is the
     * rendered bitmap between them. */
    pass_resume = linebuffer_has_pixels();
    results[nres].status = pass_resume ? "PASS" : "FAIL";
    results[nres].name   = "gpu_object_obf_release_resumes";
    results[nres].detail = pass_resume
        ? "OBF write released the OP; following bitmap rendered"
        : "OP did not resume after the GPU ISR wrote OBF";
    nres++;

    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);
    return (pass_halt && pass_resume) ? 0 : 1;
}
