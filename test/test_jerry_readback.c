/* JERRY live PIT readback: JPIT1-4 at $F10036-$F1003D return the running
 * prescaler and divider counts, not the programmed reload values.  Both
 * count down at the full system clock (see test_pit_clock_rate.c); the
 * divider steps once per prescaler wrap.
 */
#include "test_framework.h"
#include "settings.h"
#define TICK 0.03760684198            /* NTSC system clock, usec */
#define EVENT_JERRY 1
static struct vj_core c;
static void (*init_events)(void);
static void (*subtract_time)(double,int);
static double (*next_time)(int);
static void (*handle_event)(int);
static uint8_t (*read_byte)(uint32_t,uint32_t);
static void reset_timers(void)
{
    c.JERRYReset(); init_events();
    ((struct VJSettings *)c.vjs)->hardwareTypeNTSC=true;
    c.JERRYWriteWord(0xF10000, 9, 0);     /* timer 1: /10, 4 periods */
    c.JERRYWriteWord(0xF10002, 3, 0);
    c.JERRYWriteWord(0xF10004, 1, 0);     /* timer 2: /2, 1000 periods */
    c.JERRYWriteWord(0xF10006, 999, 0);
}
TEST(pit_live)
{
    reset_timers();
    ASSERT_EQ(c.JERRYReadWord(0xF10036,0),9);
    ASSERT_EQ(c.JERRYReadWord(0xF10038,0),3);
    ASSERT_EQ(c.JERRYReadWord(0xF1003A,0),1);
    ASSERT_EQ(c.JERRYReadWord(0xF1003C,0),999);
    subtract_time(10 * TICK, EVENT_JERRY);
    ASSERT_EQ(c.JERRYReadWord(0xF10036,0),9);
    ASSERT_EQ(c.JERRYReadWord(0xF10038,0),2);
    ASSERT_EQ(c.JERRYReadWord(0xF1003C,0),994);
    subtract_time(5 * TICK, EVENT_JERRY);
    ASSERT_EQ(c.JERRYReadWord(0xF10036,0),4);
    ASSERT_EQ(c.JERRYReadWord(0xF10038,0),2);
    ASSERT_EQ(c.JERRYReadWord(0xF1003A,0),0);
    ASSERT_EQ(c.JERRYReadWord(0xF1003C,0),992);
    /* Byte reads see the same count. */
    ASSERT_EQ(read_byte(0xF1003C,0),992 >> 8);
    ASSERT_EQ(read_byte(0xF1003D,0),992 & 0xFF);
    ASSERT_EQ(read_byte(0xF10039,0),2);
}
TEST(pit_reload)
{
    reset_timers();
    subtract_time(39 * TICK, EVENT_JERRY);
    ASSERT_EQ(c.JERRYReadWord(0xF10036,0),0);
    ASSERT_EQ(c.JERRYReadWord(0xF10038,0),0);
    /* Timer 1 fires at 40 clocks and reloads. */
    ASSERT_TRUE(next_time(EVENT_JERRY) > 0);
    subtract_time(next_time(EVENT_JERRY), EVENT_JERRY);
    handle_event(EVENT_JERRY);
    ASSERT_EQ(c.JERRYReadWord(0xF10036,0),9);
    ASSERT_EQ(c.JERRYReadWord(0xF10038,0),3);
}
TEST(pit_stopped)
{
    /* A timer with both registers zero is not running and reads back
     * what was written. */
    reset_timers();
    c.JERRYWriteWord(0xF10000, 0, 0);
    c.JERRYWriteWord(0xF10002, 0, 0);
    subtract_time(10 * TICK, EVENT_JERRY);
    ASSERT_EQ(c.JERRYReadWord(0xF10036,0),0);
    ASSERT_EQ(c.JERRYReadWord(0xF10038,0),0);
}
int main(void)
{
    TEST_INIT("JERRY PIT readback");
    if(!vj_core_load(&c)) return 1;
    vj_core_init(&c);
#define RESOLVE(name,symbol) name=dlsym(c.handle,symbol); if(!name) return 1
    RESOLVE(init_events,"InitializeEventList");
    RESOLVE(subtract_time,"SubtractEventTimes");
    RESOLVE(next_time,"GetTimeToNextEvent");
    RESOLVE(handle_event,"HandleNextEvent");
    RESOLVE(read_byte,"JERRYReadByte");
    RUN_TEST(pit_live); RUN_TEST(pit_reload); RUN_TEST(pit_stopped);
    vj_core_unload(&c);
    return TEST_REPORT();
}
