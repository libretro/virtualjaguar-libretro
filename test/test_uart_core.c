/* test_uart_core.c — integration: UART reachable through the JERRY
   memory dispatcher inside the real core, loopback echo works across
   retro_run, and the ASI pending bit latches in JINTCTRL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "harness/harness.h"

typedef void     (*jerry_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*jerry_rw_t)(uint32_t, uint32_t);
typedef void     (*set_mode_t)(int);

/* Minimal synthetic cartridge: entry vector -> $802000, bra.s * there.
   Same shape as test_irq_cascade's in-memory ROM, but written to a temp
   file so the standard harness load path can be used. */
#define ROM_SIZE 131072

static const char *make_synth_rom(void)
{
    static char path[256];
    static uint8_t rom_buf[ROM_SIZE];
    FILE *f;
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/vj_uart_core_stub.j64",
             tmp ? tmp : "/tmp");
    memset(rom_buf, 0, ROM_SIZE);
    rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
    rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
    rom_buf[0x2000] = 0x60; rom_buf[0x2001] = 0xFE;   /* bra.s * */
    f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(rom_buf, 1, ROM_SIZE, f);
    fclose(f);
    return path;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res[8];
    unsigned nres = 0;
    jerry_ww_t jerry_ww;
    jerry_rw_t jerry_rw;
    set_mode_t set_mode;
    uint16_t st, data, jint;
    int ok_rbf, ok_data, ok_pending, ok_state;

    cfg.frames = 2;
    cfg.options[0].key = "virtualjaguar_netlink";
    cfg.options[0].value = "loopback";
    cfg.num_options = 1;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path)
    {
        cfg.rom_path = make_synth_rom();
        if (!cfg.rom_path)
        {
            fprintf(stderr, "cannot write synthetic ROM stub\n");
            return 1;
        }
    }
    if (!harness_load_rom(&cfg))                   return 1;

    jerry_ww = (jerry_ww_t)harness_dlsym(&cfg, "JERRYWriteWord");
    jerry_rw = (jerry_rw_t)harness_dlsym(&cfg, "JERRYReadWord");
    if (!jerry_ww || !jerry_rw)
    {
        fprintf(stderr, "JERRY dispatch symbols not exported — "
                        "build with TEST_EXPORTS=1\n");
        return 1;
    }

    /* Loopback mode arrives via the virtualjaguar_netlink core option
       (cfg.options above) — no direct mode forcing. */
    (void)set_mode;

    /* J_ASYNENA (bit 4) on; RINTEN on; fastest baud; send a byte. */
    jerry_ww(0xF10020, 0x0010, 0);
    jerry_ww(0xF10032, 0x0020, 0);
    jerry_ww(0xF10034, 0x0000, 0);
    jerry_ww(0xF10030, 0x00C3, 0);

    harness_run(&cfg);   /* 2 frames ~= 33 ms >> 2 frame times at N=0 */

    st   = jerry_rw(0xF10032, 0);
    data = jerry_rw(0xF10030, 0);
    jint = jerry_rw(0xF10020, 0);
    ok_rbf     = (st & 0x0080) != 0;
    ok_data    = (data & 0xFF) == 0xC3;
    ok_pending = (jint & 0x0010) != 0;

    res[nres].status = ok_rbf ? "PASS" : "FAIL";
    res[nres].name   = "uart_core_rbf";
    res[nres].detail = "loopback byte sets RBF through dispatcher";
    nres++;
    res[nres].status = ok_data ? "PASS" : "FAIL";
    res[nres].name   = "uart_core_data";
    res[nres].detail = "ASIDATA returns echoed byte";
    nres++;
    res[nres].status = ok_pending ? "PASS" : "FAIL";
    res[nres].name   = "uart_core_jint";
    res[nres].detail = "IRQ2_ASI pending latched in JINTCTRL";
    nres++;

    /* Savestate round-trip: with a byte buffered (RBF set), serialize;
       consume the byte; restore; the byte and RBF must be back. */
    {
        typedef size_t (*rsz_t)(void);
        typedef int    (*rser_t)(void *, size_t);
        typedef int    (*runser_t)(const void *, size_t);
        rsz_t    r_size  = (rsz_t)harness_dlsym(&cfg, "retro_serialize_size");
        rser_t   r_ser   = (rser_t)harness_dlsym(&cfg, "retro_serialize");
        runser_t r_unser = (runser_t)harness_dlsym(&cfg, "retro_unserialize");
        static uint8_t state_buf[0x300000];
        size_t sz = r_size ? r_size() : 0;

        ok_state = 0;
        if (r_ser && r_unser && sz > 0 && sz <= sizeof(state_buf))
        {
            jerry_ww(0xF10030, 0x0077, 0);
            harness_run(&cfg);                  /* deliver into RBF */
            if ((jerry_rw(0xF10032, 0) & 0x0080) && r_ser(state_buf, sz))
            {
                (void)jerry_rw(0xF10030, 0);    /* consume: RBF clears */
                if (!(jerry_rw(0xF10032, 0) & 0x0080)
                    && r_unser(state_buf, sz))
                    ok_state = ((jerry_rw(0xF10032, 0) & 0x0080) != 0)
                        && (jerry_rw(0xF10030, 0) & 0xFF) == 0x77;
            }
        }
        res[nres].status = ok_state ? "PASS" : "FAIL";
        res[nres].name   = "uart_state_roundtrip";
        res[nres].detail = "RBF + data survive serialize/unserialize";
        nres++;
    }

    harness_report(&cfg, res, nres);
    harness_shutdown(&cfg);
    return (ok_rbf && ok_data && ok_pending && ok_state) ? 0 : 1;
}
