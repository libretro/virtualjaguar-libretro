/* netlink_latency.c — measures UART link exchange throughput across the
 * full stack with REAL 68K polling code, with netlink_delay_proxy
 * between the endpoints simulating network latency.
 *
 *   netlink_latency <core> --role echo  --port N            (tcp_client)
 *   netlink_latency <core> --role probe --port N [opts]     (tcp_server)
 *
 * Both sides boot a synthetic cartridge whose 68K program spins on
 * ASISTAT and echoes every received byte back incremented — the same
 * poll-the-link-mid-frame pattern Doom/BattleSphere use for lockstep
 * tic exchange.  The probe host injects one byte to start the ping-pong
 * chain, then paces retro_run at 60 fps (like a real frontend) and
 * counts completed exchanges via JLinkRxTotal over --measure-sec.
 *
 * The exchange rate exposes receive-side frame quantization: the 68K's
 * poll loop burns its 16.7 ms of emulated frame time in ~1 ms of wall
 * clock, so without a wall-clock wait in the core any transport latency
 * slips the reply to a later frame and the rate collapses to ~1 per
 * video frame regardless of actual RTT (the Wi-Fi lag failure mode).
 *
 * probe opts: --measure-sec N (default 3), --min-rate X / --max-rate X
 * (pass/fail bounds on exchanges/sec), --wait enabled|disabled (the
 * virtualjaguar_netlink_wait core option; the wait budget itself is
 * adaptive inside the core, not user-tuned).
 * Exit 0 on pass, 1 on fail/error.  Driven by netlink_latency_test.sh.
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include "../harness/harness.h"

typedef void     (*jerry_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*jerry_rw_t)(uint32_t, uint32_t);
typedef int      (*jlink_conn_t)(void);
typedef uint32_t (*jlink_u32_t)(void);
typedef void     (*retro_run_t)(void);

#define ROM_SIZE 131072
#define MAX_WAIT_FRAMES 900
#define FRAME_USEC 16667

/* 68K UART echo loop, hand-assembled, entry $802000 (ROM offset $2000):
 *   lea    $F10030.l,a0        41F9 00F1 0030
 *   move.w #$0001,$F10034.l    33FC 0001 00F1 0034   (fast baud)
 * poll:
 *   move.w $F10032.l,d0        3039 00F1 0032        (ASISTAT)
 *   btst   #7,d0               0800 0007             (RBF)
 *   beq.s  poll                67F4
 *   move.w (a0),d1             3210                  (read ASIDATA)
 *   addq.w #1,d1               5241
 *   andi.w #$FF,d1             0241 00FF
 *   move.w d1,(a0)             3081                  (transmit)
 *   bra.s  poll                60E8
 */
static const uint8_t echo_prog[] = {
    0x41, 0xF9, 0x00, 0xF1, 0x00, 0x30,
    0x33, 0xFC, 0x00, 0x01, 0x00, 0xF1, 0x00, 0x34,
    0x30, 0x39, 0x00, 0xF1, 0x00, 0x32,
    0x08, 0x00, 0x00, 0x07,
    0x67, 0xF4,
    0x32, 0x10,
    0x52, 0x41,
    0x02, 0x41, 0x00, 0xFF,
    0x30, 0x81,
    0x60, 0xE8
};

static const char *make_synth_rom(const char *role)
{
    static char path[256];
    static uint8_t rom_buf[ROM_SIZE];
    FILE *f;
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/vj_netlink_lat_%s.j64",
             tmp ? tmp : "/tmp", role);
    memset(rom_buf, 0, ROM_SIZE);
    rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
    rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
    memcpy(rom_buf + 0x2000, echo_prog, sizeof(echo_prog));
    f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(rom_buf, 1, ROM_SIZE, f);
    fclose(f);
    return path;
}

static long long now_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* One frontend-like frame slot: run, then sleep out the remainder. */
static void paced_frame(retro_run_t run_frame)
{
    long long fstart = now_usec(), spent;
    run_frame();
    spent = now_usec() - fstart;
    if (spent < FRAME_USEC)
        usleep((useconds_t)(FRAME_USEC - spent));
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    const char *role = NULL;
    const char *port = "42171";
    const char *wait_opt = "enabled";
    int measure_sec = 3;
    double min_rate = -1.0, max_rate = -1.0;
    char port_env[64];
    jerry_ww_t jerry_ww;
    jerry_rw_t jerry_rw;
    jlink_conn_t jlink_connected;
    jlink_u32_t rx_total;
    retro_run_t run_frame;
    int i, connected = 0;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--role") && i + 1 < argc)
        {
            role = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
        {
            port = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--measure-sec") && i + 1 < argc)
        {
            measure_sec = atoi(argv[++i]);
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--min-rate") && i + 1 < argc)
        {
            min_rate = atof(argv[++i]);
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--max-rate") && i + 1 < argc)
        {
            max_rate = atof(argv[++i]);
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc)
        {
            wait_opt = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
    }
    if (!role || (strcmp(role, "echo") && strcmp(role, "probe")))
    {
        fprintf(stderr, "usage: netlink_latency <core> --role echo|probe "
                        "[--port N] [--measure-sec N] [--min-rate X] "
                        "[--max-rate X] [--wait enabled|disabled]\n");
        return 1;
    }

    snprintf(port_env, sizeof(port_env), "VJ_NETLINK_PORT=%s", port);
    putenv(port_env);
    putenv((char *)"VJ_NETLINK_HOST=127.0.0.1");

    cfg.frames = 1;
    cfg.options[0].key = "virtualjaguar_netlink";
    cfg.options[0].value = !strcmp(role, "probe") ? "tcp_server" : "tcp_client";
    cfg.options[1].key = "virtualjaguar_netlink_port";
    cfg.options[1].value = port;
    cfg.options[2].key = "virtualjaguar_netlink_wait";
    cfg.options[2].value = wait_opt;
    cfg.num_options = 3;

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
    rx_total = (jlink_u32_t)harness_dlsym(&cfg, "JLinkRxTotal");
    run_frame = (retro_run_t)harness_dlsym(&cfg, "retro_run");
    if (!jerry_ww || !jerry_rw || !jlink_connected || !rx_total || !run_frame)
    {
        fprintf(stderr, "[%s] symbols missing — build with TEST_EXPORTS=1\n",
                role);
        return 1;
    }

    /* Rendezvous. */
    for (i = 0; i < MAX_WAIT_FRAMES && !connected; i++)
    {
        run_frame();
        usleep(5000);
        if (jlink_connected())
            connected = 1;
    }
    if (!connected)
    {
        fprintf(stderr, "[%s] FAIL: link never came up\n", role);
        harness_shutdown(&cfg);
        return 1;
    }
    fprintf(stderr, "[%s] link up\n", role);

    if (!strcmp(role, "echo"))
    {
        /* The 68K does all the work.  Deliberately NOT paced at 60 fps:
           a free-running echo answers within ~2 ms consistently, so the
           measured rate isolates the PROBE side's frame quantization
           instead of beating against a second paced loop's drifting
           phase (which made the metric noisy run-to-run). */
        uint32_t last = rx_total();
        int idle = 0;
        while (jlink_connected() && idle < 8000)   /* ~10 s of silence */
        {
            uint32_t now_rx;
            run_frame();
            usleep(1000);
            now_rx = rx_total();
            idle = (now_rx != last) ? 0 : idle + 1;
            last = now_rx;
        }
        fprintf(stderr, "[echo] done\n");
        harness_shutdown(&cfg);
        return 0;
    }

    /* Probe: kick the ping-pong chain, warm up, then measure. */
    {
        long long t0, t1;
        uint32_t rx0, rx1;
        double rate, elapsed;

        jerry_ww(0xF10030, 0x0040, 0);   /* first byte starts the chain */
        for (i = 0; i < 60; i++)         /* 1 s warmup: lets the core's
                                            adaptive wait budget converge
                                            before the measured window */
            paced_frame(run_frame);
        if (rx_total() == 0)
        {
            fprintf(stderr, "[probe] FAIL: chain never started\n");
            harness_shutdown(&cfg);
            return 1;
        }

        rx0 = rx_total();
        t0 = now_usec();
        while (now_usec() - t0 < (long long)measure_sec * 1000000LL)
            paced_frame(run_frame);
        t1 = now_usec();
        rx1 = rx_total();

        elapsed = (double)(t1 - t0) / 1000000.0;
        rate = (double)(rx1 - rx0) / elapsed;
        printf("[probe] %.1f exchanges/sec (%u exchanges in %.2f s, "
               "wait=%s)\n", rate, rx1 - rx0, elapsed, wait_opt);

        harness_shutdown(&cfg);
        if (min_rate >= 0.0 && rate < min_rate)
        {
            printf("[probe] FAIL: rate %.1f < min %.1f\n", rate, min_rate);
            return 1;
        }
        if (max_rate >= 0.0 && rate > max_rate)
        {
            printf("[probe] FAIL: rate %.1f > max %.1f "
                   "(proxy delay not in effect?)\n", rate, max_rate);
            return 1;
        }
        printf("[probe] PASS\n");
        return 0;
    }
}
