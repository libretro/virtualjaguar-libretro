/*
 * op_list_dump.c -- decode the Object Processor display list.
 *
 * Answers "what is the OP actually drawing, and with what fields?".  Runs a
 * title headlessly (scripted input via the shared harness's --press), then
 * walks the object list in main RAM and prints every object decoded into its
 * JTRM fields: TYPE, YPOS, HEIGHT, XPOS, IWIDTH, DWIDTH, DEPTH, PITCH,
 * HSCALE, VSCALE, REMAINDER, DATA, plus the raw phrases and a BRANCH
 * object's LINK/CC.
 *
 * This exists because reasoning about a rendering bug from src/tom/op.c alone
 * is unreliable -- the object fields the game actually wrote are the ground
 * truth for whether the OP is misbehaving or faithfully drawing bad data.  It
 * was written for issue #354 (Val d'Isere foreground snow) and established
 * that the snow plane is a type-1 SCALED BITMAP whose own HSCALE=$10 yields a
 * 160px-wide object -- i.e. the OP was correct and the defect is upstream.
 *
 * FINDING THE LIST BASE: run any trace_probe_attach()-based tool with
 * --trace-out and read the OP_OBJECT events (test/tools/trace_dump --type
 * OP_OBJECT); their `addr` field is the object phrase address.  Pass the
 * lowest one as OPLIST_BASE.
 *
 * Field layout is JTRM Rev 8 "Bit Mapped Object" / "Scaled Bit Mapped Object";
 * see docs/jtrm-object-processor.md.  Note HSCALE/VSCALE are DESTINATION per
 * SOURCE (below $20 shrinks) -- do not read them backwards.
 *
 * Env knobs:
 *   OPLIST_BASE=13BA00   hex address of the first object   (default 13BA00)
 *   OPLIST_COUNT=26      number of 0x20-byte slots to walk (default 26)
 *   OPLIST_STRIDE=20     hex slot stride                   (default 20)
 *
 * Usage:
 *   ./test/tools/op_list_dump [core] <rom> [--frames N] [--press F:BTN[:HOLD]]...
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/op_list_dump test/tools/op_list_dump.c \
 *      test/harness/harness.c -ldl -lm
 */

#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t (*rdlong)(uint32_t, uint32_t);

static uint64_t phrase(uint32_t a)
{
    return ((uint64_t)rdlong(a, 0) << 32) | rdlong(a + 4, 0);
}

static const char *type_name(unsigned t)
{
    switch (t) {
        case 0:  return "BITMAP";
        case 1:  return "SCALED";
        case 2:  return "GPUOBJ";
        case 3:  return "BRANCH";
        case 4:  return "STOP  ";
        default: return "?     ";
    }
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint32_t base   = 0x13BA00;
    uint32_t stride = 0x20;
    int count = 26;
    uint32_t addr;
    int i;
    uint64_t p0, p1, p2;
    unsigned type;
    const char *env;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!harness_load_rom(&cfg))
        return 1;
    harness_run(&cfg);

    rdlong = (uint32_t (*)(uint32_t, uint32_t))harness_dlsym(&cfg, "JaguarReadLong");
    if (!rdlong) {
        fprintf(stderr, "op_list_dump: core does not export JaguarReadLong "
                        "(build with TEST_EXPORTS=1)\n");
        return 1;
    }

    env = getenv("OPLIST_BASE");
    if (env) base = (uint32_t)strtoul(env, NULL, 16);
    env = getenv("OPLIST_COUNT");
    if (env) count = atoi(env);
    env = getenv("OPLIST_STRIDE");
    if (env) stride = (uint32_t)strtoul(env, NULL, 16);

    printf("addr   type    ypos hgt  xpos iwid dwid dep pit hscl vscl remn data\n");
    for (i = 0; i < count; i++) {
        addr = base + (uint32_t)i * stride;
        p0 = phrase(addr);
        p1 = phrase(addr + 8);
        p2 = phrase(addr + 16);
        type = (unsigned)(p0 & 0x07);

        printf("%06X %s %4u %4u %5d %4u %4u  %u   %u  $%02X  $%02X $%04X %06X\n",
               addr, type_name(type),
               (unsigned)((p0 >> 3) & 0x7FF),
               (unsigned)((p0 >> 14) & 0x3FF),
               (int)(((int16_t)((p1 << 4) & 0xFFFF)) >> 4),
               (unsigned)((p1 >> 28) & 0x3FF),
               (unsigned)((p1 >> 18) & 0x3FF),
               (unsigned)((p1 >> 12) & 0x07),
               (unsigned)((p1 >> 15) & 0x07),
               (unsigned)(p2 & 0xFF),
               (unsigned)((p2 >> 8) & 0xFF),
               (unsigned)((p2 >> 16) & 0xFF),
               (unsigned)((p0 >> 40) & 0xFFFFF8));

        printf("       raw p0=%08X%08X p1=%08X%08X p2=%08X%08X",
               (unsigned)(p0 >> 32), (unsigned)p0,
               (unsigned)(p1 >> 32), (unsigned)p1,
               (unsigned)(p2 >> 32), (unsigned)p2);
        if (type == 3)
            printf(" | BRANCH link=%06X cc=%u",
                   (unsigned)((p0 & 0x000007FFFF000000ULL) >> 21),
                   (unsigned)((p0 >> 14) & 0x07));
        printf("\n");
    }

    harness_shutdown(&cfg);
    return 0;
}
