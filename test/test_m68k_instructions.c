/*
 * test_m68k_instructions.c — Unit tests for Motorola 68000 CPU emulation.
 *
 * Writes small 68K programs directly to jaguarMainRAM, resets the CPU
 * with the test code as the entry point, executes, and verifies state.
 *
 * Build:
 *   make -j4 DEBUG=1 && cc -O0 -g -Wno-incompatible-pointer-types \
 *       -o test/test_m68k_instructions test/test_m68k_instructions.c -ldl
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_m68k_instructions
 */

#include "test_framework.h"

static struct vj_core C;
static uint8_t *ram;

/* m68k register enum values */
enum {
    D0, D1, D2, D3, D4, D5, D6, D7,
    A0, A1, A2, A3, A4, A5, A6, A7,
    REG_PC = 16,
    REG_SR = 17,
    REG_SP = 18
};

/* SR flag bits */
#define SR_C  0x0001
#define SR_V  0x0002
#define SR_Z  0x0004
#define SR_N  0x0008
#define SR_X  0x0010
#define SR_S  0x2000  /* supervisor */

/* Test program base address */
#define PROG_BASE  0x4000
#define STACK_BASE 0x10000
#define DATA_AREA  0x5000

/* Write big-endian word to RAM */
static void w16(uint32_t addr, uint16_t val)
{
    ram[addr]     = (val >> 8) & 0xFF;
    ram[addr + 1] = val & 0xFF;
}

/* Write big-endian long to RAM */
static void w32(uint32_t addr, uint32_t val)
{
    ram[addr]     = (val >> 24) & 0xFF;
    ram[addr + 1] = (val >> 16) & 0xFF;
    ram[addr + 2] = (val >> 8) & 0xFF;
    ram[addr + 3] = val & 0xFF;
}

/* Read big-endian long from RAM */
static uint32_t r32(uint32_t addr)
{
    return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr+1] << 16)
         | ((uint32_t)ram[addr+2] << 8) | (uint32_t)ram[addr+3];
}

/* Fill program area with NOPs, set reset vectors, reset CPU */
static void cpu_test_setup(void)
{
    for (uint32_t i = 0; i < 0x100; i += 2)
        w16(PROG_BASE + i, 0x4E71);  /* NOP */

    /* Reset vector: SP at address 0, PC at address 4 */
    w32(0, STACK_BASE);
    w32(4, PROG_BASE);

    C.m68k_pulse_reset();
}

/* Write program starting at PROG_BASE, return next write offset */
static uint32_t prog_start(void) { return PROG_BASE; }

static uint32_t emit16(uint32_t off, uint16_t val)
{
    w16(off, val);
    return off + 2;
}

static uint32_t emit32(uint32_t off, uint32_t val)
{
    w32(off, val);
    return off + 4;
}

/* Execute after writing program */
static void cpu_run(int cycles)
{
    C.m68k_execute(cycles);
}

static uint32_t getreg(int r)
{
    return C.m68k_get_reg(NULL, r);
}

static uint16_t getsr(void)
{
    return (uint16_t)C.m68k_get_reg(NULL, REG_SR);
}

/* ------------------------------------------------------------------ */
/* MOVEQ tests                                                         */
/* ------------------------------------------------------------------ */

TEST(moveq_positive)
{
    cpu_test_setup();
    /* MOVEQ #42, D0 = $702A */
    w16(PROG_BASE, 0x702A);
    cpu_run(10);
    ASSERT_EQ_U32(getreg(D0), 42);
}

TEST(moveq_negative)
{
    cpu_test_setup();
    /* MOVEQ #-1, D0 = $70FF */
    w16(PROG_BASE, 0x70FF);
    cpu_run(10);
    ASSERT_EQ_U32(getreg(D0), 0xFFFFFFFF);
}

TEST(moveq_d3)
{
    cpu_test_setup();
    /* MOVEQ #7, D3 = $7607 */
    w16(PROG_BASE, 0x7607);
    cpu_run(10);
    ASSERT_EQ_U32(getreg(D3), 7);
}

/* ------------------------------------------------------------------ */
/* Arithmetic                                                          */
/* ------------------------------------------------------------------ */

TEST(add_long_reg)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x700A);  /* MOVEQ #10, D0 */
    p = emit16(p, 0x7220);  /* MOVEQ #32, D1 */
    p = emit16(p, 0xD081);  /* ADD.L D1, D0 */
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 42);
}

TEST(add_zero_flag)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7000);  /* MOVEQ #0, D0 */
    p = emit16(p, 0x7200);  /* MOVEQ #0, D1 */
    p = emit16(p, 0xD081);  /* ADD.L D1, D0 */
    cpu_run(30);
    ASSERT_TRUE(getsr() & SR_Z);
}

TEST(sub_long_reg)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    p = emit16(p, 0x720A);  /* MOVEQ #10, D1 */
    p = emit16(p, 0x9081);  /* SUB.L D1, D0 */
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 32);
}

TEST(sub_negative_flag)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x700A);  /* MOVEQ #10, D0 */
    p = emit16(p, 0x722A);  /* MOVEQ #42, D1 */
    p = emit16(p, 0x9081);  /* SUB.L D1, D0 */
    cpu_run(30);
    ASSERT_TRUE(getsr() & SR_N);
}

TEST(neg_long)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    p = emit16(p, 0x4480);  /* NEG.L D0 */
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), (uint32_t)-42);
}

TEST(clr_long)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    p = emit16(p, 0x4280);  /* CLR.L D0 */
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0);
    ASSERT_TRUE(getsr() & SR_Z);
}

TEST(mulu_word)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7006);  /* MOVEQ #6, D0 */
    p = emit16(p, 0x7207);  /* MOVEQ #7, D1 */
    p = emit16(p, 0xC0C1);  /* MULU D1, D0 */
    cpu_run(80);
    ASSERT_EQ_U32(getreg(D0), 42);
}

TEST(muls_word)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FD);  /* MOVEQ #-3, D0 */
    p = emit16(p, 0x720E);  /* MOVEQ #14, D1 */
    p = emit16(p, 0xC1C1);  /* MULS D1, D0 */
    cpu_run(80);
    ASSERT_EQ_U32(getreg(D0), (uint32_t)-42);
}

TEST(divu_word)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    p = emit16(p, 0x7207);  /* MOVEQ #7, D1 */
    p = emit16(p, 0x80C1);  /* DIVU D1, D0 */
    cpu_run(160);
    /* Result: quotient in low word, remainder in high word */
    ASSERT_EQ_U32(getreg(D0) & 0xFFFF, 6);       /* quotient */
    ASSERT_EQ_U32((getreg(D0) >> 16) & 0xFFFF, 0); /* remainder */
}

TEST(swap_reg)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* MOVE.L #$12345678, D0 */
    p = emit16(p, 0x203C);
    p = emit32(p, 0x12345678);
    /* SWAP D0 */
    p = emit16(p, 0x4840);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0x56781234);
}

TEST(ext_word)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FF);  /* MOVEQ #-1, D0 → D0=$FFFFFFFF */
    /* MOVE.L #$00000080, D0 — only set low byte to $80 */
    p = emit16(p, 0x203C);
    p = emit32(p, 0x00000080);
    /* EXT.W D0 — sign-extend byte to word */
    p = emit16(p, 0x4880);
    cpu_run(20);
    /* Low word should be $FF80, high word $0000 (EXT.W only affects low word) */
    ASSERT_EQ_U32(getreg(D0) & 0xFFFF, 0xFF80);
}

TEST(ext_long)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* MOVE.L #$0000FF80, D0 */
    p = emit16(p, 0x203C);
    p = emit32(p, 0x0000FF80);
    /* EXT.L D0 — sign-extend word to long */
    p = emit16(p, 0x48C0);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0xFFFFFF80);
}

/* ------------------------------------------------------------------ */
/* Logic                                                               */
/* ------------------------------------------------------------------ */

TEST(and_long_reg)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* MOVE.L #$FF00FF00, D0 */
    p = emit16(p, 0x203C);
    p = emit32(p, 0xFF00FF00);
    /* MOVE.L #$0F0F0F0F, D1 */
    p = emit16(p, 0x223C);
    p = emit32(p, 0x0F0F0F0F);
    /* AND.L D1, D0 */
    p = emit16(p, 0xC081);
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 0x0F000F00);
}

TEST(or_long_reg)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x700F);  /* MOVEQ #$0F, D0 */
    p = emit16(p, 0x72F0);  /* MOVEQ #-16, D1 → $FFFFFFF0 */
    p = emit16(p, 0x8081);  /* OR.L D1, D0 */
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 0xFFFFFFFF);
}

TEST(eor_long_reg)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FF);  /* MOVEQ #-1, D0 → $FFFFFFFF */
    p = emit16(p, 0x720F);  /* MOVEQ #$0F, D1 */
    /* EOR.L D1, D0: D0 = D0 XOR D1 */
    p = emit16(p, 0xB380);
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 0xFFFFFFF0);
}

TEST(not_long)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7000);  /* MOVEQ #0, D0 */
    p = emit16(p, 0x4680);  /* NOT.L D0 */
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0xFFFFFFFF);
}

/* ------------------------------------------------------------------ */
/* Shift / Rotate                                                      */
/* ------------------------------------------------------------------ */

TEST(lsl_long_imm)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7001);  /* MOVEQ #1, D0 */
    /* LSL.L #4, D0: count=4, dir=1(L), size=10(L), i/r=0, type=01(LS), reg=000 */
    p = emit16(p, 0xE988);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0x10);
}

TEST(lsr_long_imm)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* MOVE.L #$80, D0 */
    p = emit16(p, 0x7080);  /* MOVEQ #-128, D0 → $FFFFFF80 */
    /* Need unsigned $80, use MOVE.L immediate instead */
    p = emit16(p, 0x203C);
    p = emit32(p, 0x00000080);
    /* LSR.L #4, D0: count=4, dir=0(R), size=10(L), i/r=0, type=01(LS), reg=000 */
    p = emit16(p, 0xE888);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0x08);
}

TEST(asr_long_imm)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* MOVE.L #$80000000, D0 */
    p = emit16(p, 0x203C);
    p = emit32(p, 0x80000000);
    /* ASR.L #1, D0: count=1, dir=0(R), size=10(L), i/r=0, type=00(AS), reg=000 */
    p = emit16(p, 0xE280);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0xC0000000);  /* sign-extended */
}

TEST(rol_long_imm)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FF);  /* MOVEQ #-1, D0 → $FFFFFFFF */
    /* Actually use a value where rotation is visible */
    p = emit16(p, 0x203C);
    p = emit32(p, 0x00000001);
    /* ROL.L #4, D0: count=4, dir=1(L), size=10(L), i/r=0, type=11(RO), reg=000 */
    p = emit16(p, 0xE998);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0x10);
}

TEST(ror_long_imm)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x203C);
    p = emit32(p, 0x000000FF);
    /* ROR.L #4, D0: count=4, dir=0(R), size=10(L), i/r=0, type=11(RO), reg=000 */
    p = emit16(p, 0xE898);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0xF000000F);
}

/* ------------------------------------------------------------------ */
/* Compare and flags                                                   */
/* ------------------------------------------------------------------ */

TEST(cmp_equal)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    p = emit16(p, 0x722A);  /* MOVEQ #42, D1 */
    p = emit16(p, 0xB081);  /* CMP.L D1, D0 */
    cpu_run(30);
    ASSERT_TRUE(getsr() & SR_Z);
    ASSERT_FALSE(getsr() & SR_N);
}

TEST(cmp_less)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x700A);  /* MOVEQ #10, D0 */
    p = emit16(p, 0x722A);  /* MOVEQ #42, D1 */
    p = emit16(p, 0xB081);  /* CMP.L D1, D0 — D0 - D1 */
    cpu_run(30);
    ASSERT_TRUE(getsr() & SR_N);
    ASSERT_FALSE(getsr() & SR_Z);
}

TEST(tst_zero)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7000);  /* MOVEQ #0, D0 */
    p = emit16(p, 0x4A80);  /* TST.L D0 */
    cpu_run(20);
    ASSERT_TRUE(getsr() & SR_Z);
    ASSERT_FALSE(getsr() & SR_N);
}

TEST(tst_negative)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FF);  /* MOVEQ #-1, D0 */
    p = emit16(p, 0x4A80);  /* TST.L D0 */
    cpu_run(20);
    ASSERT_TRUE(getsr() & SR_N);
    ASSERT_FALSE(getsr() & SR_Z);
}

/* ------------------------------------------------------------------ */
/* Memory access (MOVE to/from memory)                                 */
/* ------------------------------------------------------------------ */

TEST(move_to_memory)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    /* LEA DATA_AREA, A0: $41F9 + 32-bit address */
    p = emit16(p, 0x41F9);
    p = emit32(p, DATA_AREA);
    /* MOVE.L D0, (A0): $2080 */
    p = emit16(p, 0x2080);
    cpu_run(30);
    ASSERT_EQ_U32(r32(DATA_AREA), 42);
}

TEST(move_from_memory)
{
    cpu_test_setup();
    w32(DATA_AREA, 0xDEADBEEF);
    uint32_t p = prog_start();
    /* LEA DATA_AREA, A0 */
    p = emit16(p, 0x41F9);
    p = emit32(p, DATA_AREA);
    /* MOVE.L (A0), D0: $2010 */
    p = emit16(p, 0x2010);
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 0xDEADBEEF);
}

TEST(move_predecrement)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x702A);  /* MOVEQ #42, D0 */
    /* LEA DATA_AREA+4, A0 */
    p = emit16(p, 0x41F9);
    p = emit32(p, DATA_AREA + 4);
    /* MOVE.L D0, -(A0): $2100 */
    p = emit16(p, 0x2100);
    cpu_run(30);
    ASSERT_EQ_U32(r32(DATA_AREA), 42);
    ASSERT_EQ_U32(getreg(A0), DATA_AREA);
}

TEST(move_postincrement)
{
    cpu_test_setup();
    w32(DATA_AREA, 0xCAFEBABE);
    uint32_t p = prog_start();
    /* LEA DATA_AREA, A0 */
    p = emit16(p, 0x41F9);
    p = emit32(p, DATA_AREA);
    /* MOVE.L (A0)+, D0: $2018 */
    p = emit16(p, 0x2018);
    cpu_run(30);
    ASSERT_EQ_U32(getreg(D0), 0xCAFEBABE);
    ASSERT_EQ_U32(getreg(A0), DATA_AREA + 4);
}

/* ------------------------------------------------------------------ */
/* Address register operations                                         */
/* ------------------------------------------------------------------ */

TEST(lea_basic)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* LEA $1234, A0 */
    p = emit16(p, 0x41F9);
    p = emit32(p, 0x00001234);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(A0), 0x1234);
}

TEST(adda_long)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* LEA $1000, A0 */
    p = emit16(p, 0x41F9);
    p = emit32(p, 0x00001000);
    /* MOVEQ #4, D0 */
    p = emit16(p, 0x7004);
    /* ADDA.L D0, A0: $D1C0 */
    p = emit16(p, 0xD1C0);
    cpu_run(30);
    ASSERT_EQ_U32(getreg(A0), 0x1004);
}

TEST(suba_long)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    /* LEA $1000, A0 */
    p = emit16(p, 0x41F9);
    p = emit32(p, 0x00001000);
    /* MOVEQ #4, D0 */
    p = emit16(p, 0x7004);
    /* SUBA.L D0, A0: $91C0 */
    p = emit16(p, 0x91C0);
    cpu_run(30);
    ASSERT_EQ_U32(getreg(A0), 0x0FFC);
}

/* ------------------------------------------------------------------ */
/* Bit operations                                                      */
/* ------------------------------------------------------------------ */

TEST(btst_set)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FF);  /* MOVEQ #-1, D0 ($FFFFFFFF) */
    /* BTST #0, D0: $0800 0000 */
    p = emit16(p, 0x0800);
    p = emit16(p, 0x0000);
    cpu_run(20);
    ASSERT_FALSE(getsr() & SR_Z);  /* bit is set, so Z=0 */
}

TEST(btst_clear)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7000);  /* MOVEQ #0, D0 */
    /* BTST #0, D0 */
    p = emit16(p, 0x0800);
    p = emit16(p, 0x0000);
    cpu_run(20);
    ASSERT_TRUE(getsr() & SR_Z);  /* bit is clear, so Z=1 */
}

TEST(bset_basic)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x7000);  /* MOVEQ #0, D0 */
    /* BSET #7, D0: $08C0 0007 */
    p = emit16(p, 0x08C0);
    p = emit16(p, 0x0007);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0x80);
}

TEST(bclr_basic)
{
    cpu_test_setup();
    uint32_t p = prog_start();
    p = emit16(p, 0x70FF);  /* MOVEQ #-1, D0 */
    /* BCLR #0, D0: $0880 0000 */
    p = emit16(p, 0x0880);
    p = emit16(p, 0x0000);
    cpu_run(20);
    ASSERT_EQ_U32(getreg(D0), 0xFFFFFFFE);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (!vj_core_load(&C))
    {
        fprintf(stderr, "Failed to load core\n");
        return 1;
    }
    vj_core_init(&C);

    ram = C.GetRamPtr();
    if (!ram)
    {
        fprintf(stderr, "GetRamPtr returned NULL\n");
        return 1;
    }

    TEST_INIT("M68K CPU Instructions");

    /* MOVEQ */
    RUN_TEST(moveq_positive);
    RUN_TEST(moveq_negative);
    RUN_TEST(moveq_d3);

    /* Arithmetic */
    RUN_TEST(add_long_reg);
    RUN_TEST(add_zero_flag);
    RUN_TEST(sub_long_reg);
    RUN_TEST(sub_negative_flag);
    RUN_TEST(neg_long);
    RUN_TEST(clr_long);
    RUN_TEST(mulu_word);
    RUN_TEST(muls_word);
    RUN_TEST(divu_word);
    RUN_TEST(swap_reg);
    RUN_TEST(ext_word);
    RUN_TEST(ext_long);

    /* Logic */
    RUN_TEST(and_long_reg);
    RUN_TEST(or_long_reg);
    RUN_TEST(eor_long_reg);
    RUN_TEST(not_long);

    /* Shift / Rotate */
    RUN_TEST(lsl_long_imm);
    RUN_TEST(lsr_long_imm);
    RUN_TEST(asr_long_imm);
    RUN_TEST(rol_long_imm);
    RUN_TEST(ror_long_imm);

    /* Compare & flags */
    RUN_TEST(cmp_equal);
    RUN_TEST(cmp_less);
    RUN_TEST(tst_zero);
    RUN_TEST(tst_negative);

    /* Memory */
    RUN_TEST(move_to_memory);
    RUN_TEST(move_from_memory);
    RUN_TEST(move_predecrement);
    RUN_TEST(move_postincrement);

    /* Address register */
    RUN_TEST(lea_basic);
    RUN_TEST(adda_long);
    RUN_TEST(suba_long);

    /* Bit operations */
    RUN_TEST(btst_set);
    RUN_TEST(btst_clear);
    RUN_TEST(bset_basic);
    RUN_TEST(bclr_basic);

    int result = TEST_REPORT();
    vj_core_unload(&C);
    return result;
}
