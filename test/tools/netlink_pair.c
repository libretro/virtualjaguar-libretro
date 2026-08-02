/* netlink_pair.c — one endpoint of the two-process TCP link test.
 *
 *   netlink_pair <core> --role server|client [--port N]
 *
 * Server sends CA FE and expects BE EF; client sends BE EF and expects
 * CA FE.  Bytes travel the full stack: JERRY registers -> UART frames
 * -> jlink -> TCP -> peer process.  Exit 0 on success, 1 on failure.
 * Driven by test/tools/netlink_pair_test.sh.
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../harness/harness.h"

typedef void     (*jerry_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*jerry_rw_t)(uint32_t, uint32_t);
typedef int      (*jlink_conn_t)(void);
typedef void     (*retro_run_t)(void);

#define ROM_SIZE 131072
#define MAX_WAIT_FRAMES 900   /* ~15 s at 60 fps */

static const char *make_synth_rom(const char *role)
{
    static char path[256];
    static uint8_t rom_buf[ROM_SIZE];
    FILE *f;
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/vj_netlink_pair_%s.j64",
             tmp ? tmp : "/tmp", role);
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
    const char *role = NULL;
    const char *port = "42171";
    const char *host = "127.0.0.1";
    char port_env[64];
    char host_env[192];
    jerry_ww_t jerry_ww;
    jerry_rw_t jerry_rw;
    jlink_conn_t jlink_connected;
    jlink_conn_t jlink_mode;
    retro_run_t run_frame;
    uint8_t send_bytes[2], want_bytes[2], got_bytes[2];
    unsigned ngot = 0;
    int i, connected = 0, sent = 0;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--role") && i + 1 < argc)
        {
            role = argv[++i];
            /* consume so harness_init doesn't see it */
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
        {
            port = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        /* Client-side hub address.  A name here (DNS or Bonjour
           ".local") is the point of the test: it exercises the core's
           resolver path rather than the dotted-quad fast path. */
        else if (!strcmp(argv[i], "--host") && i + 1 < argc)
        {
            host = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
    }
    if (!role || (strcmp(role, "server") && strcmp(role, "client")))
    {
        fprintf(stderr, "usage: netlink_pair <core> --role server|client [--port N] [--host ADDR]\n");
        return 1;
    }

    if (!strcmp(role, "server"))
    {
        send_bytes[0] = 0xCA; send_bytes[1] = 0xFE;
        want_bytes[0] = 0xBE; want_bytes[1] = 0xEF;
    }
    else
    {
        send_bytes[0] = 0xBE; send_bytes[1] = 0xEF;
        want_bytes[0] = 0xCA; want_bytes[1] = 0xFE;
    }

    snprintf(port_env, sizeof(port_env), "VJ_NETLINK_PORT=%s", port);
    putenv(port_env);
    snprintf(host_env, sizeof(host_env), "VJ_NETLINK_HOST=%s", host);
    putenv(host_env);

    cfg.frames = 1;
    cfg.options[0].key = "virtualjaguar_netlink";
    cfg.options[0].value = !strcmp(role, "server") ? "tcp_server" : "tcp_client";
    cfg.options[1].key = "virtualjaguar_netlink_port";
    cfg.options[1].value = port;
    cfg.num_options = 2;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path)
    {
        cfg.rom_path = make_synth_rom(role);
        if (!cfg.rom_path) return 1;
    }
    if (!harness_load_rom(&cfg)) return 1;

    jerry_ww = (jerry_ww_t)harness_dlsym(&cfg, "JERRYWriteWord");
    jerry_rw = (jerry_rw_t)harness_dlsym(&cfg, "JERRYReadWord");
    jlink_connected = (jlink_conn_t)harness_dlsym(&cfg, "JLinkConnected");
    jlink_mode = (jlink_conn_t)harness_dlsym(&cfg, "JLinkMode");
    run_frame = (retro_run_t)harness_dlsym(&cfg, "retro_run");
    if (jlink_mode)
        fprintf(stderr, "[%s] jlink mode after load = %d\n", role, jlink_mode());
    if (!jerry_ww || !jerry_rw || !jlink_connected || !run_frame)
    {
        fprintf(stderr, "[%s] symbols missing — build with TEST_EXPORTS=1\n", role);
        return 1;
    }

    /* Slow baud (~27 ms per character frame): this test only reads
       ASIDATA at video-frame granularity, so deliveries must be spaced
       wider than a frame or the second byte overruns (OE) exactly as
       real hardware would.  Games service RBF from the interrupt. */
    jerry_ww(0xF10034, 0x0FFF, 0);

    for (i = 0; i < MAX_WAIT_FRAMES && ngot < 2; i++)
    {
        run_frame();
        /* Headless frames run unthrottled; pace the rendezvous so the
           frame budget spans several wall-clock seconds. */
        if (!connected)
            usleep(5000);

        if (!connected && jlink_connected())
        {
            connected = 1;
            fprintf(stderr, "[%s] link up at frame %d\n", role, i);
        }
        if (connected && !sent)
        {
            /* Two writes: shift + holding; the UART paces them out at
               the programmed baud. */
            jerry_ww(0xF10030, send_bytes[0], 0);
            jerry_ww(0xF10030, send_bytes[1], 0);
            sent = 1;
        }
        while (ngot < 2 && (jerry_rw(0xF10032, 0) & 0x0080))
            got_bytes[ngot++] = (uint8_t)(jerry_rw(0xF10030, 0) & 0xFF);
    }

    /* Grace period: our peer may still be waiting on its second byte
       (paced at ~27 ms emulated per character).  Exiting immediately
       tears the socket down under it — seen as a flake on slow CI
       (i686 runner: server got 1 of 2 bytes).  Keep the link alive a
       moment after we are satisfied. */
    if (ngot == 2)
    {
        for (i = 0; i < 90; i++)
        {
            run_frame();
            usleep(5000);
        }
    }

    harness_shutdown(&cfg);

    if (!connected)
    {
        fprintf(stderr, "[%s] FAIL: link never came up\n", role);
        return 1;
    }
    if (ngot != 2 || got_bytes[0] != want_bytes[0] || got_bytes[1] != want_bytes[1])
    {
        fprintf(stderr, "[%s] FAIL: got %u bytes [%02X %02X], want [%02X %02X]\n",
                role, ngot, ngot > 0 ? got_bytes[0] : 0,
                ngot > 1 ? got_bytes[1] : 0, want_bytes[0], want_bytes[1]);
        return 1;
    }
    fprintf(stderr, "[%s] PASS: exchanged bytes correctly\n", role);
    return 0;
}
