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
static void (*execute_frame)(void);
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
/* A restart by the 68K part-way through a slice counts from the write:
 * the read that follows sees the same count however much code ran in
 * the slice before the restart.  (Review of #1226.) */
static void emit(uint32_t *p, uint16_t w)
{
    c.JaguarWriteWord(*p, w, 0);
    *p += 2;
}
static unsigned restart_and_read(int nops)
{
    uint32_t p = 0x4000;
    int i;
    c.JaguarReset();
    ((struct VJSettings *)c.vjs)->hardwareTypeNTSC=true;
    for(i=0;i<nops;i++) emit(&p,0x4E71);                    /* nop */
    emit(&p,0x33FC); emit(&p,0);    emit(&p,0xF1); emit(&p,0x0000); /* move.w #0,$F10000 */
    emit(&p,0x33FC); emit(&p,1000); emit(&p,0xF1); emit(&p,0x0002); /* move.w #1000,$F10002 */
    emit(&p,0x3039); emit(&p,0xF1); emit(&p,0x0038);        /* move.w $F10038,d0 */
    emit(&p,0x33C0); emit(&p,0);    emit(&p,0x2000);        /* move.w d0,$2000 */
    emit(&p,0x4E72); emit(&p,0x2700);                       /* stop #$2700 */
    c.m68k_set_reg(17,0x2700); c.m68k_set_reg(16,0x4000);
    execute_frame();
    return c.JaguarReadWord(0x2000,0);
}
TEST(pit_restart_in_slice)
{
    unsigned r0 = restart_and_read(0);
    ASSERT_TRUE(r0 > 950 && r0 <= 1000);
    ASSERT_EQ(restart_and_read(40), r0);
    ASSERT_EQ(restart_and_read(41), r0);
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
    RESOLVE(execute_frame,"JaguarExecuteNew");
    RUN_TEST(pit_live); RUN_TEST(pit_reload); RUN_TEST(pit_stopped);
    RUN_TEST(pit_restart_in_slice);
    vj_core_unload(&c);
    return TEST_REPORT();
}
