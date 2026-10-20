/* TOM live PIT and MEMCON1 fallback (#1044, #1045).
 * Primary sources: jag_sim tom/MISC.NET Dtp/Dt and IODEC.NET Float1-5.
 * JTRM v8 p.15 lists PIT as WO; netlist read strobes correct that table.
 */
#include "test_framework.h"
#include "settings.h"
static struct vj_core c;
static void (*init_events)(void);
static void (*subtract_time)(double,int);
static double (*next_time)(int);
static void (*handle_event)(int);
static size_t (*save_events)(uint8_t *);
static size_t (*load_events)(const uint8_t *);
static uint8_t (*read_byte)(uint32_t,uint32_t);
static uint32_t (*read_long)(uint32_t,uint32_t);
static void reset_timer(void)
{
    c.TOMReset(); init_events();
    ((struct VJSettings *)c.vjs)->hardwareTypeNTSC=true;
    c.TOMWriteWord(0xF00050, 9, 0);
    c.TOMWriteWord(0xF00052, 3, 0);
}
TEST(pit_live)
{
    reset_timer();
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),3);
    subtract_time(10 * 0.03760684198,0);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),2);
    ASSERT_EQ(read_byte(0xF00052,0),0);
    ASSERT_EQ(read_byte(0xF00053,0),2);
    subtract_time(5 * 0.03760684198,0);
    ASSERT_EQ(c.TOMReadWord(0xF00050,0),4);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),2);
}
TEST(pit_rollover_and_state)
{
    uint8_t state[8192];
    reset_timer();
    subtract_time(15 * 0.03760684198,0);
    save_events(state);
    subtract_time(15 * 0.03760684198,0);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),0);
    load_events(state);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),2);
    ASSERT_EQ(c.TOMReadWord(0xF00050,0),4);
    ASSERT_TRUE(next_time(0)>0);
    handle_event(0);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),3);
    ASSERT_EQ(c.TOMReadWord(0xF00050,0),9);
}
TEST(pit_full_width)
{
    reset_timer();
    c.TOMWriteWord(0xF00050,0xFFFF,0);
    c.TOMWriteWord(0xF00052,0xFFFF,0);
    subtract_time(65536.0 * 0.03760684198,0);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),0xFFFE);
    c.TOMWriteWord(0xF00050,0,0);
    ASSERT_EQ(c.TOMReadWord(0xF00050,0),0);
    ASSERT_EQ(c.TOMReadWord(0xF00052,0),0);
}
TEST(memcon_fallback)
{
    static const uint32_t offsets[]={0x0C,0x26,0x28,0x3E,0x4E,0xE2,0xFE,0x100,0x800};
    unsigned i;
    c.TOMReset();
    c.TOMWriteWord(0xF00000,0x1865,0);
    for(i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
    {
        ASSERT_EQ(c.TOMReadWord(0xF00000+offsets[i],0),0x1865);
        ASSERT_EQ(read_byte(0xF00000+offsets[i],0),0x18);
        ASSERT_EQ(read_byte(0xF00001+offsets[i],0),0x65);
    }
    ASSERT_EQ(c.TOMReadWord(0xF000E2,1),0x1865);
    ASSERT_EQ(read_long(0xF000E2,0),0x18651865);
    c.TOMWriteWord(0xF00000,0x1234,0);
    ASSERT_EQ(c.TOMReadWord(0xF000E2,0),0x1234);
}
TEST(readable_memory)
{
    c.TOMWriteWord(0xF00002,0x1234,0);
    ASSERT_EQ(c.TOMReadWord(0xF00002,0),0x1234);
    c.TOMWriteWord(0xF00400,0xABCD,0);
    ASSERT_EQ(c.TOMReadWord(0xF00400,0),0xABCD);
    ASSERT_EQ(c.TOMReadWord(0xF00600,0),0xABCD);
    c.TOMWriteWord(0xF00800,0xEEEE,0);
    ASSERT_EQ(c.TOMReadWord(0xF00800,0),0x1234);
    /* GB1/GB2 are outside the Float read-decode correction. Preserve
     * the existing backing/dispatch behavior of their unmodeled holes. */
    c.TOMWriteWord(0xF02020,0x5678,0);
    ASSERT_EQ(c.TOMReadWord(0xF02020,0),0x5678);
}
int main(void)
{
    TEST_INIT("TOM hardware readback");
    if(!vj_core_load(&c)) return 1;
    vj_core_init(&c); c.TOMInit();
#define RESOLVE(name,symbol) name=dlsym(c.handle,symbol); if(!name) return 1
    RESOLVE(init_events,"InitializeEventList");
    RESOLVE(subtract_time,"SubtractEventTimes");
    RESOLVE(next_time,"GetTimeToNextEvent");
    RESOLVE(handle_event,"HandleNextEvent");
    RESOLVE(save_events,"EventStateSave");
    RESOLVE(load_events,"EventStateLoad");
    RESOLVE(read_byte,"TOMReadByte");
    RESOLVE(read_long,"JaguarReadLong");
    RUN_TEST(pit_live); RUN_TEST(pit_rollover_and_state);
    RUN_TEST(pit_full_width); RUN_TEST(memcon_fallback);
    RUN_TEST(readable_memory);
    vj_core_unload(&c);
    return TEST_REPORT();
}
