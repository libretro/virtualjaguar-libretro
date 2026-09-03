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
/* The mnemonic table and decode logic live in exactly one place -- see
 * docs/gdb-stub-design.md (issue #652): "do not write a second
 * disassembler." src/debug/gdbtarget.c's `monitor disasm` uses the same
 * header for the live, in-process case; this tool is the offline,
 * dlsym-based one. */
#include "../../src/debug/gdbdisasm.h"

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint16_t (*rdw)(uint32_t, uint32_t);
    uint32_t (*getreg)(int);
    uint32_t (*getpc)(void);
    uint32_t at = 0xF03410, words = 16, i;
    int a, dumpregs = 0;
    cfg.frames = 900;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--at") && a + 1 < argc)
            at = (uint32_t)strtoul(argv[++a], NULL, 16);
        else if (!strcmp(argv[a], "--words") && a + 1 < argc)
            words = (uint32_t)strtoul(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--regs"))
            dumpregs = 1;
    }
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!harness_load_rom(&cfg)) return 1;
    harness_run(&cfg);
    rdw = (uint16_t (*)(uint32_t, uint32_t))harness_dlsym(&cfg, "GPUReadWord");
    if (!rdw) { fprintf(stderr, "GPUReadWord not exported\n"); return 1; }
    if (dumpregs)
    {
        getreg = (uint32_t (*)(int))harness_dlsym(&cfg, "GPUGetReg");
        getpc = (uint32_t (*)(void))harness_dlsym(&cfg, "GPUGetPC");
        if (getreg && getpc)
        {
            uint32_t r;
            printf("GPU PC=$%06X\n", getpc());
            for (r = 0; r < 32; r++)
                printf("r%-2u=$%08X%s", r, getreg((int)r), (r % 4 == 3) ? "\n" : "  ");
        }
        else
            fprintf(stderr, "GPUGetReg/GPUGetPC not exported\n");
    }
    for (i = 0; i < words; i++)
    {
        uint32_t addr = at + i * 2;
        uint16_t w, w2, w3;
        char line[128];
        int n;

        w  = rdw(addr, 0);
        w2 = ((w >> 10) == 38) ? rdw(addr + 2, 0) : 0;   /* movei low word */
        w3 = ((w >> 10) == 38) ? rdw(addr + 4, 0) : 0;   /* movei high word */
        n = GDBDisasmOne(GDBDisasmGPUMnemonics, addr, w, w2, w3,
                         line, (int)sizeof(line) - 1);
        line[n] = '\0';
        printf("%s\n", line);
    }
    harness_shutdown(&cfg);
    return 0;
}
