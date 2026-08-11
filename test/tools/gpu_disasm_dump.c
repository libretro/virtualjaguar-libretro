/* test/tools/gpu_disasm_dump.c -- dump + disassemble a span of GPU
 * local RAM at the end of a run, and report the GPU/68K PC.  Used to
 * identify which mailbox flag a spinning GPU kernel is polling.
 *
 * Usage: gpu_disasm_dump <core> <rom> --at F03410 [--words 16] [...]
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

static const char *mn[64] = {
"add","addc","addq","addqt","sub","subc","subq","subqt",
"neg","and","or","xor","not","btst","bset","bclr",
"mult","imult","imultn","resmac","imacn","div","abs","sh",
"shlq","shrq","sha","sharq","ror","rorq","cmp","cmpq",
"sat8","sat16","move","moveq","moveta","movefa","movei","loadb",
"loadw","load","loadp","load(r14+n)","load(r15+n)","storeb","storew","store",
"storep","store(r14+n)","store(r15+n)","move pc","jump","jr","mmult","mtoi",
"normi","nop","load(r14+rn)","load(r15+rn)","store(r14+rn)","store(r15+rn)","sat24","pack"
};
static const char *cc[8] = {"T","NZ","Z","?3","NC","NC NZ","NC Z","?7"};

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint16_t (*rdw)(uint32_t, uint32_t);
    uint32_t at = 0xF03410, words = 16, i;
    int a;
    cfg.frames = 900;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--at") && a + 1 < argc)
            at = (uint32_t)strtoul(argv[++a], NULL, 16);
        else if (!strcmp(argv[a], "--words") && a + 1 < argc)
            words = (uint32_t)strtoul(argv[++a], NULL, 10);
    }
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!harness_load_rom(&cfg)) return 1;
    harness_run(&cfg);
    rdw = (uint16_t (*)(uint32_t, uint32_t))harness_dlsym(&cfg, "GPUReadWord");
    if (!rdw) { fprintf(stderr, "GPUReadWord not exported\n"); return 1; }
    for (i = 0; i < words; i++)
    {
        uint32_t addr = at + i * 2;
        uint16_t w;
        unsigned op, r1, r2;
        w  = rdw(addr, 0);
        op = w >> 10; r1 = (w >> 5) & 0x1F; r2 = w & 0x1F;
        printf("$%06X: %04X  %-14s", addr, w, mn[op]);
        if (op == 53)               /* jr */
            printf(" %s, %+d -> $%06X", cc[r2 & 7], (int)((r1 & 0x10) ? (int)r1 - 32 : (int)r1),
                   addr + 2 + ((int)((r1 & 0x10) ? (int)r1 - 32 : (int)r1)) * 2);
        else if (op == 52)          /* jump */
            printf(" %s, (r%u)", cc[r2 & 7], r1);
        else if (op == 38)          /* movei */
        {
            uint32_t lo = rdw(addr + 2, 0);
            uint32_t hi = rdw(addr + 4, 0);
            printf(" #$%08X, r%u", (hi << 16) | lo, r2);
        }
        else if (op == 2 || op == 6 || op == 31 || op == 35 || op == 24 || op == 25)
            printf(" #%u, r%u", r1 ? r1 : 32, r2);
        else
            printf(" r%u, r%u", r1, r2);
        printf("\n");
    }
    harness_shutdown(&cfg);
    return 0;
}
