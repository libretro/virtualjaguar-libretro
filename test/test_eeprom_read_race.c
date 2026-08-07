/*
 * test/test_eeprom_read_race.c — EEPROM DO sampling vs clocking contract.
 *
 * Regression test for the Raiden background-music death: the 93-series
 * EEPROM's DO line is sampled at $F14000 — the same address as the
 * JOYSTICK register the game polls from its VBL/timer interrupt.  The
 * old model shifted the read shifter on every DO sample, so a joystick
 * poll preempting an in-flight READ transaction stole data bits and the
 * mainline driver's remaining samples returned the idle level (1s).
 * Raiden's game-over settings reload read its music-enabled EEPROM
 * option back as $FF that way and muted the music sequencer for the
 * rest of the session.
 *
 * Contract under test (93C46 data sheet + Atari-standard driver shape):
 *   - Reading $F14800 (`tst.w $F14800`) is the serial clock: it shifts
 *     the next bit onto DO during the data-out phase.
 *   - Reading $F14000 samples DO without advancing anything.
 *   - Therefore joystick polls interleaved with an EEPROM READ
 *     transaction must not corrupt the value read.
 *
 * Build:  cc -O2 -Wall -std=c99 -I./test -Ilibretro-common/include \
 *           -o test/test_eeprom_read_race test/test_eeprom_read_race.c \
 *           test/harness/harness.c -ldl -lm
 *
 * Usage:  ./test/test_eeprom_read_race [core.dylib] <any_rom.j64>
 *         (needs the wide test ABI: make TEST_EXPORTS=1)
 */

#include "harness/harness.h"

#include <stdio.h>

typedef uint16_t (*jrw_fn)(uint32_t, uint32_t);
typedef void (*jww_fn)(uint32_t, uint16_t, uint32_t);

static jrw_fn jrw;
static jww_fn jww;

#define WHO 0 /* UNKNOWN */

static void cs_pulse(void)  { (void)jrw(0xF15000, WHO); }
static void send_bit(int b) { jww(0xF14800, (uint16_t)(b & 1), WHO); }

static void send_bits(uint32_t v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--)
        send_bit((int)((v >> i) & 1));
}

static int sample_do(void) { return jrw(0xF14000, WHO) & 1; }
static void strobe(void)   { (void)jrw(0xF14800, WHO); }

/* Atari-standard write: EWEN, WRITE addr+data, CS pulse, busy poll, EWDS */
static void ee_write(int addr, uint16_t val)
{
    int i;
    cs_pulse();
    send_bits(0x130, 9);                     /* EWEN  %1 00 110000 */
    cs_pulse();
    send_bits(0x140 | (addr & 0x3F), 9);     /* WRITE %1 01 aaaaaa */
    send_bits(val, 16);
    cs_pulse();
    for (i = 0; i < 100; i++)
        if (sample_do())
            break;
    cs_pulse();
    send_bits(0x100, 9);                     /* EWDS */
}

/* Atari-standard read (strobe then sample, 16 bits), optionally with a
 * burst of joystick polls injected mid-transaction like an ISR pad scan. */
static uint16_t ee_read(int addr, int inject_at, int inject_reads)
{
    uint16_t v = 0;
    int i, j;
    cs_pulse();
    send_bits(0x180 | (addr & 0x3F), 9);     /* READ  %1 10 aaaaaa */
    for (i = 0; i < 16; i++) {
        if (i == inject_at)
            for (j = 0; j < inject_reads; j++)
                (void)jrw(0xF14000, WHO);    /* preempting pad scan */
        strobe();                            /* tst.w $F14800 */
        v = (uint16_t)((v << 1) | (uint16_t)sample_do());
    }
    return v;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[4];
    unsigned n = 0;
    uint16_t got;
    int fails = 0;

    cfg.frames = 1;
    if (!harness_init_from_args(&cfg, argc, argv)) return 2;
    if (!harness_load_rom(&cfg)) return 2;

    jrw = (jrw_fn)harness_dlsym(&cfg, "JERRYReadWord");
    jww = (jww_fn)harness_dlsym(&cfg, "JERRYWriteWord");
    if (!jrw || !jww) {
        fprintf(stderr, "FAIL: JERRYReadWord/JERRYWriteWord not exported "
                        "(build with TEST_EXPORTS=1)\n");
        return 2;
    }

    ee_write(3, 0xA5C3);

    got = ee_read(3, -1, 0);
    results[n].status = (got == 0xA5C3) ? "PASS" : "FAIL";
    results[n].name = "clean_read";
    results[n].detail = "uninterrupted READ returns the written word";
    if (got != 0xA5C3) { fprintf(stderr, "clean read: got $%04X want $A5C3\n", got); fails++; }
    n++;

    got = ee_read(3, 8, 8);
    results[n].status = (got == 0xA5C3) ? "PASS" : "FAIL";
    results[n].name = "preempted_read";
    results[n].detail = "8 joystick polls mid-READ must not steal DO bits";
    if (got != 0xA5C3) { fprintf(stderr, "preempted read: got $%04X want $A5C3\n", got); fails++; }
    n++;

    got = ee_read(3, 0, 20);
    results[n].status = (got == 0xA5C3) ? "PASS" : "FAIL";
    results[n].name = "preempted_read_head";
    results[n].detail = "20 joystick polls before first strobe must not steal bits";
    if (got != 0xA5C3) { fprintf(stderr, "head-preempted read: got $%04X want $A5C3\n", got); fails++; }
    n++;

    got = ee_read(3, -1, 0);
    results[n].status = (got == 0xA5C3) ? "PASS" : "FAIL";
    results[n].name = "post_read";
    results[n].detail = "transaction after the preempted one still reads clean";
    if (got != 0xA5C3) { fprintf(stderr, "post read: got $%04X want $A5C3\n", got); fails++; }
    n++;

    harness_report(&cfg, results, n);
    harness_shutdown(&cfg);
    return fails ? 1 : 0;
}
