/* netlink_game.c — game-level probe for the network link.
 *
 *   netlink_game <core> <rom> --role loopback|server|client [--port N]
 *                [--outdir DIR] [--shot-every N] [--frames N]
 *                [--press F:BTN[:HOLD]]...
 *
 * Runs a real game with the netlink active, sampling the UART registers
 * once per frame (side-effect-free reads only: ASISTAT, ASICLK) and the
 * transport state, logging every change so a game's link programming is
 * visible; dumps periodic PPM screenshots for menu navigation work.
 * Exit code: 0 if the game ever programmed the UART (ASICLK/ASICTRL
 * write or TX observed), 2 if it never touched it, 1 on setup error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../harness/harness.h"

typedef void     (*jerry_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*jerry_rw_t)(uint32_t, uint32_t);
typedef int      (*jlink_int_t)(void);

#define MAX_W 1024
#define MAX_H 512

typedef struct {
    /* screenshots */
    const char *outdir;
    unsigned shot_every;
    uint32_t fb[MAX_W * MAX_H];
    unsigned w, h;
    unsigned frame_no;
    /* uart sampling */
    jerry_rw_t jerry_rw;
    jlink_int_t jlink_connected;
    jlink_int_t jlink_rx_pending;
    uint16_t last_stat, last_clk;
    int last_conn;
    int uart_touched;
    unsigned tbe_drops;      /* TBE observed low => game transmitted */
    int realtime;            /* pace frames to ~60 fps wall clock */
} ng_state;

static void ng_write_ppm(const ng_state *st)
{
    char path[1024];
    FILE *f;
    unsigned x, y;
    if (!st->outdir || !st->w || !st->h) return;
    snprintf(path, sizeof(path), "%s/frame_%05u.ppm", st->outdir, st->frame_no);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", st->w, st->h);
    for (y = 0; y < st->h; y++) {
        for (x = 0; x < st->w; x++) {
            uint32_t p = st->fb[y * st->w + x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((p >> 16) & 0xFF);
            rgb[1] = (unsigned char)((p >> 8) & 0xFF);
            rgb[2] = (unsigned char)(p & 0xFF);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static void ng_video_cb(void *ud, const void *data, unsigned w, unsigned h,
                        size_t pitch)
{
    ng_state *st = (ng_state *)ud;
    unsigned y;
    const uint8_t *src = (const uint8_t *)data;
    if (!data || w == 0 || h == 0 || w > MAX_W || h > MAX_H) return;
    for (y = 0; y < h; y++)
        memcpy(&st->fb[y * w], src + y * pitch, w * 4);
    st->w = w; st->h = h;
    if (st->outdir && st->shot_every && (st->frame_no % st->shot_every) == 0)
        ng_write_ppm(st);
}

static bool ng_frame_cb(void *ud, unsigned frame)
{
    ng_state *st = (ng_state *)ud;
    uint16_t stat = st->jerry_rw(0xF10032, 0);
    uint16_t clk  = st->jerry_rw(0xF10034, 0);
    int conn = st->jlink_connected ? st->jlink_connected() : 0;

    st->frame_no = frame + 1;

    if (stat != st->last_stat || clk != st->last_clk || conn != st->last_conn)
    {
        fprintf(stderr,
                "[uart] frame %5u  ASISTAT=%04X (cfg=%02X RBF=%d TBE=%d ERR=%d)"
                "  ASICLK=%04X  link=%d  rxq=%d\n",
                frame, stat, stat & 0x3F, !!(stat & 0x0080),
                !!(stat & 0x0100), !!(stat & 0x8000), clk, conn,
                st->jlink_rx_pending ? st->jlink_rx_pending() : -1);
        /* Any nonzero config/divider or a busy transmitter means the
           game is driving the UART. */
        if ((stat & 0x3F) != 0 || clk != 0)
            st->uart_touched = 1;
        if (!(stat & 0x0100))
        {
            st->tbe_drops++;
            st->uart_touched = 1;
        }
        st->last_stat = stat;
        st->last_clk = clk;
        st->last_conn = conn;
    }
    if (st->realtime)
        usleep(15000);   /* ~60 fps incl. frame cost; keeps two paired
                            instances aligned in wall-clock time */
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    ng_state st;
    const char *role = "loopback";
    const char *port = "42171";
    char port_env[64];
    int i;

    memset(&st, 0, sizeof(st));
    st.shot_every = 120;
    st.last_stat = 0xFFFF;
    st.last_clk = 0xFFFF;
    st.last_conn = -1;

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
        else if (!strcmp(argv[i], "--outdir") && i + 1 < argc)
        {
            st.outdir = argv[++i];
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc)
        {
            st.shot_every = (unsigned)atoi(argv[++i]);
            argv[i - 1] = argv[i] = (char *)"--quiet";
        }
        else if (!strcmp(argv[i], "--realtime"))
        {
            st.realtime = 1;
            argv[i] = (char *)"--quiet";
        }
    }

    snprintf(port_env, sizeof(port_env), "VJ_NETLINK_PORT=%s", port);
    putenv(port_env);
    putenv((char *)"VJ_NETLINK_HOST=127.0.0.1");

    cfg.frames = 1800;
    cfg.options[0].key = "virtualjaguar_netlink";
    cfg.options[0].value = !strcmp(role, "server") ? "tcp_server"
                         : !strcmp(role, "client") ? "tcp_client"
                         : "loopback";
    cfg.num_options = 1;
    cfg.frame_callback = ng_frame_cb;
    cfg.frame_callback_data = &st;
    cfg.video_callback = ng_video_cb;
    cfg.video_callback_data = &st;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!harness_load_rom(&cfg))                   return 1;

    st.jerry_rw = (jerry_rw_t)harness_dlsym(&cfg, "JERRYReadWord");
    st.jlink_connected = (jlink_int_t)harness_dlsym(&cfg, "JLinkConnected");
    st.jlink_rx_pending = (jlink_int_t)harness_dlsym(&cfg, "JLinkRxPending");
    if (!st.jerry_rw)
    {
        fprintf(stderr, "netlink_game: JERRY symbols missing (TEST_EXPORTS=1)\n");
        return 1;
    }

    harness_run(&cfg);
    harness_shutdown(&cfg);

    fprintf(stderr, "[uart] summary: touched=%d tbe_drops=%u\n",
            st.uart_touched, st.tbe_drops);
    return st.uart_touched ? 0 : 2;
}
