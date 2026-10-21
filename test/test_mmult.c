/* MMULT address-counter regressions (#1043).
 * Primary source: jag_sim netlists/tom/SYSTOLIC.NET MACOUNT and
 * R1COUNT (also used by JERRY): ten address bits, six vector bits.
 * JTRM v8 pp.42,59 describes bank 1 and row/column stepping.
 */
#include "test_framework.h"

static struct vj_core c;

static void check_mmult(int dsp, int column, int wrap)
{
    uint32_t base = dsp ? 0xF1B000 : 0xF03000;
    uint32_t ctrl = dsp ? 0xF1A100 : 0xF02100;
    uint32_t addr = base + (wrap ? 0xFFC : 0x800);
    uint32_t step = column ? 12 : 4;
    uint32_t *bank = dsp ? c.dsp_reg_bank_0 : c.gpu_reg_bank_0;
    uint32_t *vector = dsp ? dlsym(c.handle, "dsp_reg_bank_1") : c.gpu_reg_bank_1;
    void (*ww)(uint32_t,uint16_t,uint32_t) = dsp ? c.DSPWriteWord : c.GPUWriteWord;
    void (*wl)(uint32_t,uint32_t,uint32_t) = dsp ? c.DSPWriteLong : c.GPUWriteLong;
    uint32_t (*rl)(uint32_t,uint32_t) = dsp ? c.DSPReadLong : c.GPUReadLong;
    void (*exec)(int32_t) = dsp ? c.DSPExec : c.GPUExec;
    uint8_t state[16384];
    size_t (*save)(uint8_t *) = dlsym(c.handle,dsp?"DSPStateSave":"GPUStateSave");
    size_t (*load)(const uint8_t *) = dlsym(c.handle,dsp?"DSPStateLoad":"GPUStateLoad");
    unsigned i;
    if (dsp) c.DSPReset(); else c.GPUReset();
    ASSERT_TRUE(vector != NULL);
    for (i=0; i<0x1000; i+=2) ww(base+i, GPU_NOP, 0);
    vector[0]=0x00020001;
    vector[1]=3;
    for (i=0; i<6; i++)
        wl(base+((addr-base+i*step)&0xFFC), i+1, 0);
    wl(ctrl+4, 3 | (column ? 16 : 0), 0);
    wl(ctrl+8, addr, 0);
    ww(base+0x100, gpu_encode(54,0,10), 0);
    ww(base+0x102, GPU_NOP, 0);
    ww(base+0x104, GPU_NOP, 0);
    ww(base+0x106, gpu_encode(54,0,11), 0);
    wl(ctrl+0x10, base+0x100, 0);
    wl(ctrl+0x14, 1, 0);
    exec(40);
    wl(ctrl+0x14, 0, 0);
    ASSERT_EQ(bank[10], 14);
    ASSERT_EQ(bank[11], 32);
    ASSERT_EQ(rl(ctrl+8,0), base+((addr-base+6*step)&0xFFC));
    ASSERT_TRUE(save != NULL && load != NULL);
    save(state);
    wl(ctrl+8,base+0x400,0);
    load(state);
    ASSERT_EQ(rl(ctrl+8,0),base+((addr-base+6*step)&0xFFC));
}
TEST(gpu_row) { check_mmult(0,0,0); }
TEST(gpu_column) { check_mmult(0,1,0); }
TEST(gpu_row_wrap) { check_mmult(0,0,1); }
TEST(gpu_column_wrap) { check_mmult(0,1,1); }
TEST(dsp_row) { check_mmult(1,0,0); }
TEST(dsp_column) { check_mmult(1,1,0); }
TEST(dsp_row_wrap) { check_mmult(1,0,1); }
TEST(dsp_column_wrap) { check_mmult(1,1,1); }
int main(void)
{
    TEST_INIT("MMULT matrix address counter");
    if (!vj_core_load(&c)) return 1;
    vj_core_init(&c);
    RUN_TEST(gpu_row); RUN_TEST(gpu_column);
    RUN_TEST(gpu_row_wrap); RUN_TEST(gpu_column_wrap);
    RUN_TEST(dsp_row); RUN_TEST(dsp_column);
    RUN_TEST(dsp_row_wrap); RUN_TEST(dsp_column_wrap);
    vj_core_unload(&c);
    return TEST_REPORT();
}
