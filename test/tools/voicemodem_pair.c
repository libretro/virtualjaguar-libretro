/* voicemodem_pair.c — one endpoint of the two-process Voice Modem test.
 *
 *   voicemodem_pair <core> --role dial|answer [--port N] [--host ADDR]
 *
 * Drives the JERRY UART registers with the exact command sequences Ultra
 * Vortek's modem driver issues (docs/voice-modem.md): wake, ident,
 * config, dial / answer, the mutual DTMF probe, the $8100 carrier query
 * with its $A4FC follow-up, and a 4-byte data packet each way ending in
 * the modem-generated $F301.  Bytes travel the full stack: UART frames
 * -> voicemodem -> jlink -> TCP -> peer process.  Exit 0 on success.
 * Driven by test/tools/voicemodem_pair_test.sh.
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../harness/harness.h"
#include "jlink.h"

typedef void     (*jerry_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*jerry_rw_t)(uint32_t, uint32_t);
typedef int      (*jlink_conn_t)(void);
typedef void     (*retro_run_t)(void);

#define ROM_SIZE 131072
#define MAX_WAIT_FRAMES 1200    /* rendezvous budget (~20 s at 60 fps) */
#define MSG_TIMEOUT 600         /* frames to wait for one 3-byte reply */

static jerry_ww_t jerry_ww;
static jerry_rw_t jerry_rw;
static retro_run_t run_frame;
static const char *g_role = "?";

/* Software RX FIFO.  On real hardware Ultra Vortek's DSP drains RBF at
 * I2S rate no matter what the 68K is doing; without this, RX bytes
 * arriving while the test is busy transmitting overrun (OE) and the
 * 3-byte message stream slips out of sync. */
#define RXFIFO_SIZE 256
static uint8_t rxfifo[RXFIFO_SIZE];
static unsigned rxf_head, rxf_count;

static void drain_rbf(void)
{
    while ((jerry_rw(0xF10032, 0) & 0x0080) && rxf_count < RXFIFO_SIZE)
    {
        rxfifo[(rxf_head + rxf_count) % RXFIFO_SIZE] =
            (uint8_t)(jerry_rw(0xF10030, 0) & 0xFF);
        rxf_count++;
    }
}

static void step(void)
{
    run_frame();
    drain_rbf();
    usleep(2000);   /* headless frames run unthrottled; stay ~wall time */
}

/* Wait for TBE, then load one byte into the transmitter. */
static int tx_byte(uint8_t b)
{
    int i;
    for (i = 0; i < MSG_TIMEOUT; i++)
    {
        if (jerry_rw(0xF10032, 0) & 0x0100)
        {
            jerry_ww(0xF10030, b, 0);
            return 1;
        }
        step();
    }
    fprintf(stderr, "[%s] FAIL: TBE never set\n", g_role);
    return 0;
}

/* Command word: low byte first (the driver's SendWord). */
static int send_word(uint16_t w)
{
    if (!tx_byte((uint8_t)(w & 0xFF)))
        return 0;
    return tx_byte((uint8_t)(w >> 8));
}

/* Pop one received byte, pumping frames until it arrives. */
static int rx_byte(uint8_t *b)
{
    int i;
    for (i = 0; i < MSG_TIMEOUT; i++)
    {
        drain_rbf();
        if (rxf_count)
        {
            *b = rxfifo[rxf_head];
            rxf_head = (rxf_head + 1) % RXFIFO_SIZE;
            rxf_count--;
            return 1;
        }
        step();
    }
    return 0;
}

/* Read one 3-byte modem message ($FF sync, high, low); $B1xx ring
 * indications repeat and are skipped transparently. */
static int recv_msg(uint16_t *w)
{
    uint8_t sync, hi, lo;
    for (;;)
    {
        if (!rx_byte(&sync))
            return 0;
        if (sync != 0xFF && sync != 0xFE)
        {
            fprintf(stderr, "[%s] FAIL: bad sync byte %02X\n", g_role, sync);
            return 0;
        }
        if (!rx_byte(&hi) || !rx_byte(&lo))
            return 0;
        *w = (uint16_t)(((uint16_t)hi << 8) | lo);
        if (hi == 0xB1)
            continue;           /* ring indicate: consume and keep going */
        return 1;
    }
}

static int expect(uint16_t want)
{
    uint16_t got;
    if (!recv_msg(&got))
    {
        fprintf(stderr, "[%s] FAIL: timeout waiting for %04X\n",
                g_role, want);
        return 0;
    }
    if (got != want)
    {
        fprintf(stderr, "[%s] FAIL: got %04X, want %04X\n",
                g_role, got, want);
        return 0;
    }
    return 1;
}

/* Send a command word and require its echo. */
static int cmd_echo(uint16_t w)
{
    if (!send_word(w))
        return 0;
    return expect(w);
}

/* Poll $6800 until the modem reports a heard digit; require it. */
static int expect_digit(uint8_t digit, int tries)
{
    uint16_t got;
    int i;
    for (i = 0; i < tries; i++)
    {
        if (!send_word(0x6800))
            return 0;
        if (!recv_msg(&got))
            return 0;
        if (got == 0xFFFE)
            continue;           /* nothing heard yet */
        if (got == (uint16_t)(0x6800 | digit))
            return 1;
        fprintf(stderr, "[%s] FAIL: heard %04X, want %04X\n",
                g_role, got, 0x6800 | digit);
        return 0;
    }
    fprintf(stderr, "[%s] FAIL: digit %u never heard\n", g_role, digit);
    return 0;
}

/* Transmit a 4-byte data packet as one continuous burst so the modem's
 * end-of-packet marker fires once, after the last byte. */
static int send_packet(const uint8_t *payload)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        if (!send_word((uint16_t)(0xF000 | payload[i])))
            return 0;
    }
    return 1;
}

static int recv_packet(const uint8_t *want)
{
    uint16_t got;
    int i;
    for (i = 0; i < 4; i++)
    {
        if (!recv_msg(&got))
        {
            fprintf(stderr, "[%s] FAIL: data byte %d never arrived\n",
                    g_role, i);
            return 0;
        }
        if (got != (uint16_t)(0xF000 | want[i]))
        {
            fprintf(stderr, "[%s] FAIL: data %d: got %04X want %04X\n",
                    g_role, i, got, 0xF000 | want[i]);
            return 0;
        }
    }
    return expect(0xF301);      /* modem-generated end-of-packet */
}

static const char *make_synth_rom(const char *role)
{
    static char path[256];
    static uint8_t rom_buf[ROM_SIZE];
    FILE *f;
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/vj_vmodem_pair_%s.j64",
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
    jlink_conn_t jlink_connected;
    jlink_conn_t jlink_mode;
    int i, dialer = 0, connected = 0, ok = 0;
    static const uint8_t pkt_dial[4]   = { 0xDE, 0xAD, 0xBE, 0xEF };
    static const uint8_t pkt_answer[4] = { 0x11, 0x22, 0x33, 0x44 };

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
        else if (!strcmp(argv[i], "--host") && i + 1 < argc)
        {
            host = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
    }
    if (!role || (strcmp(role, "dial") && strcmp(role, "answer")))
    {
        fprintf(stderr, "usage: voicemodem_pair <core> --role dial|answer"
                        " [--port N] [--host ADDR]\n");
        return 1;
    }
    g_role = role;
    dialer = !strcmp(role, "dial");

    if (snprintf(port_env, sizeof(port_env), "VJ_NETLINK_PORT=%s", port)
        >= (int)sizeof(port_env))
        return 1;
    putenv(port_env);
    if (snprintf(host_env, sizeof(host_env), "VJ_NETLINK_HOST=%s", host)
        >= (int)sizeof(host_env))
        return 1;
    putenv(host_env);

    cfg.frames = 1;
    cfg.options[0].key = "virtualjaguar_netlink";
    cfg.options[0].value = dialer ? "tcp_client" : "tcp_server";
    cfg.options[1].key = "virtualjaguar_netlink_port";
    cfg.options[1].value = port;
    cfg.options[2].key = "virtualjaguar_uart_device";
    cfg.options[2].value = "voicemodem";
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
    jlink_mode = (jlink_conn_t)harness_dlsym(&cfg, "JLinkMode");
    run_frame = (retro_run_t)harness_dlsym(&cfg, "retro_run");
    if (!jerry_ww || !jerry_rw || !jlink_connected || !run_frame)
    {
        fprintf(stderr, "[%s] symbols missing -- build with TEST_EXPORTS=1\n",
                role);
        return 1;
    }
    if (!dialer && jlink_mode && jlink_mode() == JLINK_MODE_DISABLED)
    {
        fprintf(stderr, "[%s] FAIL: netlink server did not open "
                        "(port %s busy?)\n", role, port);
        harness_shutdown(&cfg);
        return 2;
    }

    /* Slow character pacing (~27 ms/char, one per video frame): this
     * test reads and refills the UART at frame granularity, and the
     * 4-byte data packet must stay a single TX burst (TBE is re-armed
     * every frame, well inside the char time) so the modem's
     * end-of-packet marker fires exactly once. */
    jerry_ww(0xF10034, 0x0FFF, 0);

    for (i = 0; i < MAX_WAIT_FRAMES && !connected; i++)
    {
        step();
        if (jlink_connected())
            connected = 1;
    }
    if (!connected)
    {
        fprintf(stderr, "[%s] FAIL: transport never connected\n", role);
        harness_shutdown(&cfg);
        return 1;
    }
    fprintf(stderr, "[%s] transport up\n", role);

    do
    {
        /* --- wake / ident (same on both sides) --- */
        if (!send_word(0xFFFF) || !expect(0xB800)) break;
        if (!send_word(0xFFFE)) break;              /* no reply */
        if (!cmd_echo(0x0102)) break;
        /* --- config block (representative subset; all echo) --- */
        if (!cmd_echo(0x0501)) break;
        if (!cmd_echo(0x000F)) break;
        if (!cmd_echo(0xA3FC)) break;

        if (dialer)
        {
            if (!cmd_echo(0x2C80)) break;           /* originate mode */
            if (!send_word(0x8C01) || !expect(0x8C01)) break; /* tone */
            if (!cmd_echo(0x8A21)) break;           /* dial digit 1 */
            if (!send_word(0x8C00) || !expect(0x8C00)) break;
            /* first $6800 marks end-of-dial; then hear the answerer's
             * probe digits 0, 9 */
            if (!expect_digit(0x0, 400)) break;
            if (!expect_digit(0x9, 400)) break;
            /* send our probe digits 1, 2 */
            if (!cmd_echo(0x8A21)) break;
            if (!cmd_echo(0x8A22)) break;
        }
        else
        {
            if (!cmd_echo(0x2480)) break;           /* answer mode */
            /* probe digits 0, 9 (buffered until the call is up) */
            if (!cmd_echo(0x8A20)) break;
            if (!cmd_echo(0x8A29)) break;
            if (!send_word(0x8C00) || !expect(0x8C00)) break;
            /* hear the dialer's probe digits 1, 2 */
            if (!expect_digit(0x1, 400)) break;
            if (!expect_digit(0x2, 400)) break;
        }

        /* --- carrier query: $86xx then the async $A4FC --- */
        if (!cmd_echo(0x8000)) break;
        if (!send_word(0x8100) || !expect(0x86D0) || !expect(0xA4FC)) break;

        /* --- data phase: one packet each way --- */
        if (dialer)
        {
            if (!send_packet(pkt_dial)) break;
            if (!recv_packet(pkt_answer)) break;
        }
        else
        {
            if (!send_packet(pkt_answer)) break;
            if (!recv_packet(pkt_dial)) break;
        }

        ok = 1;
    } while (0);

    /* Grace period so the peer finishes its paced reads before the
     * socket goes away under it. */
    if (ok)
        for (i = 0; i < 120; i++)
            step();

    harness_shutdown(&cfg);

    if (!ok)
    {
        fprintf(stderr, "[%s] FAIL\n", role);
        return 1;
    }
    fprintf(stderr, "[%s] PASS: modem handshake + data exchange\n", role);
    return 0;
}
