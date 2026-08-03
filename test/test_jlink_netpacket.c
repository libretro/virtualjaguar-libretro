/* test_jlink_netpacket.c — the test IS the netplay frontend.
 *
 * A minimal libretro frontend (dlopen, own env/video/audio/input stubs,
 * synthetic ROM) that captures the retro_netpacket_callback the core
 * registers via RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, then drives a
 * fake netplay session: start() -> UART TX must arrive at our send_fn as
 * a RELIABLE broadcast; our receive() -> bytes must surface in ASIDATA;
 * stop() -> link back to the option-configured mode.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <libretro.h>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

/* ---- captured netpacket interface ---- */
static struct retro_netpacket_callback np_cb;
static int np_registered = 0;

/* ---- captured traffic at the fake frontend ---- */
static uint8_t sent_data[4096];
static size_t sent_len = 0;
static int sent_flags = -1;
static uint16_t sent_client = 0;

static void fake_send(int flags, const void *buf, size_t len,
                      uint16_t client_id)
{
    if (buf && len && sent_len + len <= sizeof(sent_data))
    {
        memcpy(sent_data + sent_len, buf, len);
        sent_len += len;
    }
    sent_flags = flags;
    sent_client = client_id;
}

/* ---- minimal frontend stubs ---- */
static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
            memcpy(&np_cb, data, sizeof(np_cb));
            np_registered = 1;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable *var = (struct retro_variable *)data;
            if (var->key && !strcmp(var->key, "virtualjaguar_netlink"))
            {
                var->value = "disabled";
                return true;
            }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char **)data = "/tmp";
            return true;
        default:
            return false;
    }
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f)
{ (void)d; return f; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

/* ---- synthetic ROM (entry vector -> bra.s *) ---- */
#define ROM_SIZE 131072
static uint8_t rom_buf[ROM_SIZE];

typedef void     (*jerry_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*jerry_rw_t)(uint32_t, uint32_t);
typedef int      (*jlink_int_t)(void);

int main(int argc, char **argv)
{
    const char *core_path = argc > 1 ? argv[1]
                                     : "./virtualjaguar_libretro.dylib";
    void *h;
    void (*p_set_environment)(retro_environment_t);
    void (*p_set_video_refresh)(retro_video_refresh_t);
    void (*p_set_audio_sample)(retro_audio_sample_t);
    void (*p_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*p_set_input_poll)(retro_input_poll_t);
    void (*p_set_input_state)(retro_input_state_t);
    void (*p_init)(void);
    bool (*p_load_game)(const struct retro_game_info *);
    void (*p_run)(void);
    void (*p_unload_game)(void);
    void (*p_deinit)(void);
    jerry_ww_t jerry_ww;
    jerry_rw_t jerry_rw;
    jlink_int_t jlink_mode;
    struct retro_game_info game;
    int i;
    uint8_t rx_payload[2];

    h = dlopen(core_path, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

#define SYM(var, name) do { *(void **)&var = dlsym(h, name); \
    if (!var) { fprintf(stderr, "missing %s\n", name); return 1; } } while (0)
    SYM(p_set_environment, "retro_set_environment");
    SYM(p_set_video_refresh, "retro_set_video_refresh");
    SYM(p_set_audio_sample, "retro_set_audio_sample");
    SYM(p_set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(p_set_input_poll, "retro_set_input_poll");
    SYM(p_set_input_state, "retro_set_input_state");
    SYM(p_init, "retro_init");
    SYM(p_load_game, "retro_load_game");
    SYM(p_run, "retro_run");
    SYM(p_unload_game, "retro_unload_game");
    SYM(p_deinit, "retro_deinit");
    SYM(jerry_ww, "JERRYWriteWord");
    SYM(jerry_rw, "JERRYReadWord");
    SYM(jlink_mode, "JLinkMode");
#undef SYM

    p_set_environment(env_cb);
    CHECK(np_registered, "core registers netpacket interface (env 78)");
    CHECK(np_cb.start && np_cb.receive && np_cb.stop && np_cb.poll,
          "callback members populated");
    CHECK(np_cb.protocol_version
              && !strcmp(np_cb.protocol_version, "vjag-netlink-1"),
          "protocol version pinned");

    p_set_video_refresh(video_cb);
    p_set_audio_sample(audio_sample_cb);
    p_set_audio_sample_batch(audio_batch_cb);
    p_set_input_poll(input_poll_cb);
    p_set_input_state(input_state_cb);
    p_init();

    memset(rom_buf, 0, ROM_SIZE);
    rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
    rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
    rom_buf[0x2000] = 0x60; rom_buf[0x2001] = 0xFE;   /* bra.s * */
    memset(&game, 0, sizeof(game));
    game.path = "netpacket_stub.j64";
    game.data = rom_buf;
    game.size = ROM_SIZE;
    if (!p_load_game(&game)) { fprintf(stderr, "load_game failed\n"); return 1; }

    CHECK(jlink_mode() == 0, "before session: link disabled (option)");

    /* --- netplay session starts (we are the host, client_id 0) --- */
    np_cb.start(0, fake_send, NULL);
    CHECK(jlink_mode() == 4, "start(): link switches to netpacket mode");

    /* UART TX -> our send_fn.  Fast baud; two bytes; run a frame so the
       TX events fire and JLinkPoll flushes the batch. */
    jerry_ww(0xF10034, 0x0000, 0);
    jerry_ww(0xF10030, 0x00A5, 0);
    jerry_ww(0xF10030, 0x005A, 0);
    for (i = 0; i < 3 && sent_len < 2; i++)
    {
        p_run();
        if (np_cb.poll)
            np_cb.poll();
    }
    CHECK(sent_len == 2 && sent_data[0] == 0xA5 && sent_data[1] == 0x5A,
          "TX bytes arrive at frontend send_fn in order");
    CHECK((sent_flags & RETRO_NETPACKET_RELIABLE) != 0,
          "TX uses RELIABLE flag");
    CHECK(sent_client == RETRO_NETPACKET_BROADCAST,
          "TX is broadcast");

    /* Frontend receive -> RBF.  Slow baud first (~27 ms/char): this test
       polls RBF at frame granularity, so back-to-back ring deliveries at
       fast baud would overrun exactly as hardware would. */
    jerry_ww(0xF10034, 0x0FFF, 0);
    rx_payload[0] = 0xC3; rx_payload[1] = 0x3C;
    np_cb.receive(rx_payload, 2, 1);
    for (i = 0; i < 6 && !(jerry_rw(0xF10032, 0) & 0x0080); i++)
        p_run();
    CHECK((jerry_rw(0xF10032, 0) & 0x0080) != 0, "receive(): RBF sets");
    CHECK((jerry_rw(0xF10030, 0) & 0xFF) == 0xC3, "receive(): 1st byte");
    for (i = 0; i < 6 && !(jerry_rw(0xF10032, 0) & 0x0080); i++)
        p_run();
    CHECK((jerry_rw(0xF10030, 0) & 0xFF) == 0x3C, "receive(): 2nd byte");

    /* --- session ends --- */
    np_cb.stop();
    CHECK(jlink_mode() == 0, "stop(): link restored to prior mode");

    p_unload_game();
    p_deinit();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
