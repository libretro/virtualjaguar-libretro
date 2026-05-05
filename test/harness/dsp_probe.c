/*
 * test/harness/dsp_probe.c — DSP state inspection probe implementation.
 */

#include "dsp_probe.h"

#include <stdio.h>
#include <string.h>

bool dsp_probe_init(dsp_probe *p, harness_config *cfg)
{
    memset(p, 0, sizeof(*p));
    p->cfg = cfg;

    p->sym.pc        = harness_dlsym(cfg, "dsp_pc");
    p->sym.control   = harness_dlsym(cfg, "dsp_control");
    p->sym.bank0     = harness_dlsym(cfg, "dsp_reg_bank_0");
    p->sym.bank1     = harness_dlsym(cfg, "dsp_reg_bank_1");
    p->sym.get_ram   = (uint8_t *(*)(void))harness_dlsym(cfg, "DSPGetRAM");
    p->sym.is_running = (bool (*)(void))harness_dlsym(cfg, "DSPIsRunning");
    p->sym.get_flags = (uint32_t (*)(void))harness_dlsym(cfg, "DSPGetFlags");
    p->sym.ltxd      = (uint16_t **)harness_dlsym(cfg, "ltxd");
    p->sym.i2s_timer = harness_dlsym(cfg, "JERRYI2SInterruptTimer");

    /* Critical: need at minimum PC and one register bank */
    if (!p->sym.pc || !p->sym.bank0) {
        fprintf(stderr, "dsp_probe: cannot resolve critical DSP symbols "
                "(build with TEST_EXPORTS=1)\n");
        return false;
    }

    return true;
}

void dsp_probe_snapshot(dsp_probe *p)
{
    dsp_snapshot *s = &p->snap;

    s->frame = p->cfg->current_frame;
    s->pc = p->sym.pc ? *p->sym.pc : 0;
    s->control = p->sym.control ? *p->sym.control : 0;
    s->running = p->sym.is_running ? p->sym.is_running() : 0;

    if (p->sym.get_flags)
        s->flags = p->sym.get_flags();
    else
        s->flags = 0;

    if (p->sym.bank0)
        memcpy(s->bank0, p->sym.bank0, DSP_NUM_REGS * sizeof(uint32_t));
    if (p->sym.bank1)
        memcpy(s->bank1, p->sym.bank1, DSP_NUM_REGS * sizeof(uint32_t));

    if (p->sym.ltxd && *p->sym.ltxd)
        s->ltxd_value = (int16_t)**p->sym.ltxd;
    else
        s->ltxd_value = 0;

    if (p->sym.i2s_timer)
        s->i2s_timer = *p->sym.i2s_timer;
    else
        s->i2s_timer = -1;

    /* Store in history ring */
    p->history[p->history_idx] = *s;
    p->history_idx = (p->history_idx + 1) % 64;
    if (p->history_count < 64) p->history_count++;
}

bool dsp_probe_pc_escaped(const dsp_probe *p)
{
    uint32_t pc = p->snap.pc;
    if (!p->snap.running) return false;
    return (pc < DSP_WORK_RAM_BASE || pc > DSP_WORK_RAM_END);
}

unsigned dsp_probe_bank_nonzero(const uint32_t bank[DSP_NUM_REGS])
{
    unsigned count = 0;
    unsigned i;
    for (i = 0; i < DSP_NUM_REGS; i++) {
        if (bank[i] != 0) count++;
    }
    return count;
}

double dsp_probe_ltxd_ratio(const dsp_probe *p)
{
    if (p->counters.ltxd_total_samples == 0) return 0.0;
    return (double)p->counters.ltxd_nonzero_writes /
           (double)p->counters.ltxd_total_samples;
}

bool dsp_probe_per_frame(dsp_probe *p)
{
    dsp_probe_snapshot(p);

    /* Count I2S activity */
    p->counters.i2s_dispatches++;
    p->counters.ltxd_total_samples++;
    if (p->snap.ltxd_value != 0)
        p->counters.ltxd_nonzero_writes++;

    /* Check for PC escape */
    if (dsp_probe_pc_escaped(p)) {
        p->counters.pc_escape_count++;
        if (p->counters.pc_escape_count == 1) {
            p->counters.first_escape_pc = p->snap.pc;
            p->counters.first_escape_frame = p->snap.frame;
        }
        return false;
    }

    return true;
}

void dsp_probe_dump_ram(const dsp_probe *p, uint32_t addr, unsigned words)
{
    uint8_t *ram;
    unsigned i;
    uint32_t offset;

    if (!p->sym.get_ram) {
        fprintf(stderr, "  (DSP RAM not available)\n");
        return;
    }

    ram = p->sym.get_ram();

    if (addr < DSP_WORK_RAM_BASE || addr > DSP_WORK_RAM_END) {
        printf("  DSP RAM dump around $%06X: (outside DSP RAM)\n", addr);
        return;
    }
    offset = addr - DSP_WORK_RAM_BASE;

    printf("  DSP RAM dump around $%06X:\n", addr);
    for (i = 0; i < words; i++) {
        uint32_t off = offset + i * 2;
        uint16_t opcode;
        if (off + 1 >= DSP_RAM_SIZE) break;
        opcode = ((uint16_t)ram[off] << 8) | (uint16_t)ram[off + 1];
        printf("    $%06X: %04X", addr + i * 2, opcode);
        /* Basic decode: top 6 bits = opcode, next 5 = reg1, next 5 = reg2 */
        {
            unsigned op = (opcode >> 10) & 0x3F;
            unsigned r1 = (opcode >> 5) & 0x1F;
            unsigned r2 = opcode & 0x1F;
            printf("  (op=%2u r1=R%02u r2=R%02u)", op, r1, r2);
        }
        printf("\n");
    }
}

void dsp_probe_dump_banks(const dsp_probe *p)
{
    unsigned i;
    printf("  Bank0: ");
    for (i = 0; i < DSP_NUM_REGS; i++) {
        if (p->snap.bank0[i] != 0)
            printf("R%02u=%08X ", i, p->snap.bank0[i]);
    }
    printf("\n  Bank1: ");
    for (i = 0; i < DSP_NUM_REGS; i++) {
        if (p->snap.bank1[i] != 0)
            printf("R%02u=%08X ", i, p->snap.bank1[i]);
    }
    printf("\n");
}

void dsp_probe_print_snapshot(const dsp_probe *p, int json)
{
    if (json) {
        unsigned i;
        printf("{\"frame\":%u,\"pc\":\"0x%06X\",\"flags\":\"0x%05X\","
               "\"control\":\"0x%08X\",\"running\":%s,\"ltxd\":%d,"
               "\"i2s_timer\":%d,",
               p->snap.frame, p->snap.pc, p->snap.flags,
               p->snap.control,
               p->snap.running ? "true" : "false",
               p->snap.ltxd_value, p->snap.i2s_timer);
        printf("\"bank0_nonzero\":%u,\"bank1_nonzero\":%u,",
               dsp_probe_bank_nonzero(p->snap.bank0),
               dsp_probe_bank_nonzero(p->snap.bank1));
        printf("\"bank0\":[");
        for (i = 0; i < DSP_NUM_REGS; i++)
            printf("%s%u", i ? "," : "", p->snap.bank0[i]);
        printf("],\"bank1\":[");
        for (i = 0; i < DSP_NUM_REGS; i++)
            printf("%s%u", i ? "," : "", p->snap.bank1[i]);
        printf("]}");
    } else {
        printf("  Frame %u: PC=$%06X FLAGS=$%05X CTRL=$%08X %s "
               "LTXD=%d I2S_TMR=%d\n",
               p->snap.frame, p->snap.pc, p->snap.flags,
               p->snap.control,
               p->snap.running ? "RUNNING" : "STOPPED",
               p->snap.ltxd_value, p->snap.i2s_timer);
        printf("  Bank0: %u/32 non-zero | Bank1: %u/32 non-zero\n",
               dsp_probe_bank_nonzero(p->snap.bank0),
               dsp_probe_bank_nonzero(p->snap.bank1));
    }
}
