/* test_voice_netpacket.c — host-side voice over framed netpacket (#585).
 *
 * Two independent core images + a packet-queue relay (same pattern as
 * test_voicemodem_netpacket). Exercises:
 *   1) voice on both sides → hello/ack → NP_VOICE (unreliable) → far-side
 *      mix energy
 *   2) voice off on one side → hello never confirms → data-only, zero
 *      NP_VOICE from the voice-enabled side after the timeout
 *
 * Usage: test_voice_netpacket <core.so|.dylib>
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <libretro.h>

#define ROM_SIZE      131072
#define INBOX_PKTS    128
#define INBOX_PKTMAX  512
#define MAX_FRAMES    600

#define NP_UART        0x01
#define NP_VOICE       0x02
#define NP_VOICE_HELLO 0x03

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

typedef void (*vj_void_t)(void);
typedef int  (*vj_int_t)(void);

typedef struct
{
   int         idx;
   const char *name;
   void       *handle;
   char        path[512];
   int         voice_want; /* GET_VARIABLE answer */

   void (*set_environment)(retro_environment_t);
   void (*set_video_refresh)(retro_video_refresh_t);
   void (*set_audio_sample)(retro_audio_sample_t);
   void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*set_input_poll)(retro_input_poll_t);
   void (*set_input_state)(retro_input_state_t);
   vj_void_t init;
   bool (*load_game)(const struct retro_game_info *);
   vj_void_t run;
   vj_void_t unload_game;
   vj_void_t deinit;

   vj_int_t jlink_mode;
   vj_int_t jlink_np_voice_ready;

   struct retro_netpacket_callback np;
   int np_registered;

   uint8_t  inbox[INBOX_PKTS][INBOX_PKTMAX];
   size_t   inbox_len[INBOX_PKTS];
   unsigned inbox_head;
   unsigned inbox_count;
   int      inbox_overflow;

   unsigned pkts_uart;
   unsigned pkts_voice;
   unsigned pkts_hello;
   unsigned voice_unreliable_ok;
   unsigned voice_flags_bad;

   /* Accumulated far-end mix energy from audio_batch. */
   unsigned long audio_abs_sum;
   unsigned long audio_samples;
} instance;

static instance inst[2];
static int active_idx = 0;

/* ---- synthetic mic --------------------------------------------------- */

struct retro_microphone {
   int active;
   unsigned phase;
};

static struct retro_microphone g_mic_obj;
static int g_mic_open = 0;

static retro_microphone_t *mic_open(const retro_microphone_params_t *p)
{
   (void)p;
   g_mic_obj.active = 1;
   g_mic_obj.phase = 0;
   g_mic_open = 1;
   return &g_mic_obj;
}
static void mic_close(retro_microphone_t *m)
{
   if (m)
      m->active = 0;
   g_mic_open = 0;
}
static bool mic_get_params(const retro_microphone_t *m,
                           retro_microphone_params_t *p)
{
   (void)m;
   if (p) p->rate = 8000;
   return true;
}
static bool mic_set_state(retro_microphone_t *m, bool s)
{
   if (!m)
      return false;
   m->active = s ? 1 : 0;
   return true;
}
static bool mic_get_state(const retro_microphone_t *m)
{
   return m && m->active;
}
static int mic_read(retro_microphone_t *m, int16_t *samples, size_t num)
{
   size_t i;
   if (!m || !samples || !num || !m->active)
      return -1;
   /* Loud tone so VAD opens and far-side energy is unmistakable. */
   for (i = 0; i < num; i++)
   {
      samples[i] = (int16_t)((m->phase & 1) ? 12000 : -12000);
      m->phase++;
   }
   return (int)num;
}

static struct retro_microphone_interface g_mic_iface = {
   RETRO_MICROPHONE_INTERFACE_VERSION,
   mic_open, mic_close, mic_get_params,
   mic_set_state, mic_get_state, mic_read
};

/* ---- frontend callbacks ---------------------------------------------- */

static bool env_common(int idx, unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
         memcpy(&inst[idx].np, data,
                sizeof(struct retro_netpacket_callback));
         inst[idx].np_registered = 1;
         return true;
      case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE:
         if (!data)
            return false;
         {
            struct retro_microphone_interface *iface =
               (struct retro_microphone_interface *)data;
            unsigned want = iface->interface_version;
            *iface = g_mic_iface;
            if (want && want < RETRO_MICROPHONE_INTERFACE_VERSION)
               iface->interface_version = want;
            return true;
         }
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         if (!var || !var->key)
            return false;
         if (!strcmp(var->key, "virtualjaguar_netlink"))
         {
            var->value = "disabled";
            return true;
         }
         if (!strcmp(var->key, "virtualjaguar_voice_chat"))
         {
            var->value = inst[idx].voice_want ? "enabled" : "disabled";
            return true;
         }
         if (!strcmp(var->key, "virtualjaguar_voice_chat_volume"))
         {
            var->value = "100";
            return true;
         }
         if (!strcmp(var->key, "virtualjaguar_voice_chat_vad"))
         {
            var->value = "100";
            return true;
         }
         return false;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = false;
         return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool *)data = true;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
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

static bool env_cb0(unsigned cmd, void *data) { return env_common(0, cmd, data); }
static bool env_cb1(unsigned cmd, void *data) { return env_common(1, cmd, data); }

static void classify_pkt(instance *src, int flags, const uint8_t *buf, size_t len)
{
   if (!buf || len < 1)
      return;
   switch (buf[0])
   {
      case NP_UART:
         src->pkts_uart++;
         break;
      case NP_VOICE:
         src->pkts_voice++;
         if ((flags & (RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_UNSEQUENCED))
             == RETRO_NETPACKET_UNSEQUENCED)
            src->voice_unreliable_ok++;
         else if ((flags & RETRO_NETPACKET_RELIABLE) == 0)
            src->voice_unreliable_ok++; /* UNRELIABLE==0 + UNSEQUENCED */
         else
            src->voice_flags_bad++;
         break;
      case NP_VOICE_HELLO:
         src->pkts_hello++;
         break;
      default:
         break;
   }
}

static void send_common(int from, int flags, const void *buf, size_t len,
                        uint16_t client_id)
{
   instance *src = &inst[from];
   instance *dst = &inst[from ^ 1];
   unsigned slot;

   (void)client_id;
   classify_pkt(src, flags, (const uint8_t *)buf, len);
   if (!buf || !len)
      return;
   if (dst->inbox_count >= INBOX_PKTS || len > INBOX_PKTMAX)
   {
      dst->inbox_overflow = 1;
      return;
   }
   slot = (dst->inbox_head + dst->inbox_count) % INBOX_PKTS;
   memcpy(dst->inbox[slot], buf, len);
   dst->inbox_len[slot] = len;
   dst->inbox_count++;
}

static void send0(int f, const void *b, size_t l, uint16_t c)
{ send_common(0, f, b, l, c); }
static void send1(int f, const void *b, size_t l, uint16_t c)
{ send_common(1, f, b, l, c); }

static void deliver_inbox(instance *in)
{
   uint8_t buf[INBOX_PKTMAX];
   size_t len;
   unsigned head;

   if (!in->np.receive)
      return;
   while (in->inbox_count > 0)
   {
      head = in->inbox_head;
      len = in->inbox_len[head];
      memcpy(buf, in->inbox[head], len);
      in->inbox_head = (in->inbox_head + 1) % INBOX_PKTS;
      in->inbox_count--;
      in->np.receive(buf, len, (uint16_t)(in->idx ^ 1));
   }
}

static void poll_recv0(void) { deliver_inbox(&inst[0]); }
static void poll_recv1(void) { deliver_inbox(&inst[1]); }

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample_cb(int16_t l, int16_t r)
{
   instance *in = &inst[active_idx];
   int al = l < 0 ? -l : l;
   int ar = r < 0 ? -r : r;
   in->audio_abs_sum += (unsigned)al + (unsigned)ar;
   in->audio_samples += 2;
}
static size_t audio_batch_cb(const int16_t *d, size_t f)
{
   instance *in = &inst[active_idx];
   size_t i;
   if (!d)
      return f;
   for (i = 0; i < f * 2; i++)
   {
      int v = d[i];
      if (v < 0)
         v = -v;
      in->audio_abs_sum += (unsigned)v;
   }
   in->audio_samples += f * 2;
   return f;
}
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

/* ---- load helpers ---------------------------------------------------- */

static int copy_file(const char *src, const char *dst)
{
   FILE *fi, *fo;
   char buf[65536];
   size_t n;
   int ok = 1;
   fi = fopen(src, "rb");
   if (!fi)
      return 0;
   fo = fopen(dst, "wb");
   if (!fo)
   {
      fclose(fi);
      return 0;
   }
   while ((n = fread(buf, 1, sizeof(buf), fi)) > 0)
   {
      if (fwrite(buf, 1, n, fo) != n)
      {
         ok = 0;
         break;
      }
   }
   fclose(fi);
   if (fclose(fo) != 0)
      ok = 0;
   return ok;
}

static int inst_open(instance *in, int idx, const char *name,
                     const char *core_path, int voice_want)
{
   const char *tmp = getenv("TMPDIR");
   memset(in, 0, sizeof(*in));
   in->idx = idx;
   in->name = name;
   in->voice_want = voice_want;
   snprintf(in->path, sizeof(in->path), "%s/vj_np_vc_%ld_%s.lib",
            (tmp && tmp[0]) ? tmp : "/tmp", (long)getpid(), name);
   if (!copy_file(core_path, in->path))
   {
      fprintf(stderr, "cannot copy core to %s\n", in->path);
      return 0;
   }
   chmod(in->path, 0755);
   in->handle = dlopen(in->path, RTLD_NOW | RTLD_LOCAL);
   if (!in->handle)
   {
      fprintf(stderr, "dlopen(%s): %s\n", in->path, dlerror());
      return 0;
   }

#define SYM(var, sym) do { \
      *(void **)&in->var = dlsym(in->handle, sym); \
      if (!in->var) { fprintf(stderr, "missing %s\n", sym); return 0; } \
   } while (0)
   SYM(set_environment, "retro_set_environment");
   SYM(set_video_refresh, "retro_set_video_refresh");
   SYM(set_audio_sample, "retro_set_audio_sample");
   SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
   SYM(set_input_poll, "retro_set_input_poll");
   SYM(set_input_state, "retro_set_input_state");
   SYM(init, "retro_init");
   SYM(load_game, "retro_load_game");
   SYM(run, "retro_run");
   SYM(unload_game, "retro_unload_game");
   SYM(deinit, "retro_deinit");
   SYM(jlink_mode, "JLinkMode");
#undef SYM
   /* Optional wide-ABI export (TEST_EXPORTS=1 builds). */
   *(void **)&in->jlink_np_voice_ready = dlsym(in->handle, "JLinkNPVoiceReady");
   return 1;
}

static void inst_cleanup(instance *in)
{
   if (in->path[0])
      remove(in->path);
}

static uint8_t rom_buf[ROM_SIZE];

static void make_rom(void)
{
   memset(rom_buf, 0, ROM_SIZE);
   rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
   rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
   rom_buf[0x2000] = 0x60; rom_buf[0x2001] = 0xFE;
}

static void run_pair_frames(int frames)
{
   int f, i;
   for (f = 0; f < frames; f++)
   {
      for (i = 0; i < 2; i++)
      {
         active_idx = i;
         deliver_inbox(&inst[i]);
         inst[i].run();
         if (inst[i].np.poll)
            inst[i].np.poll();
         deliver_inbox(&inst[i]);
      }
   }
}

static int boot_pair(const char *core_path, int voice0, int voice1)
{
   struct retro_game_info game;
   int i;

   if (!inst_open(&inst[0], 0, "a", core_path, voice0))
      return 0;
   if (!inst_open(&inst[1], 1, "b", core_path, voice1))
      return 0;

   CHECK(dlsym(inst[0].handle, "JLinkMode")
             != dlsym(inst[1].handle, "JLinkMode"),
         "two core copies load as independent images");

   inst[0].set_environment(env_cb0);
   inst[1].set_environment(env_cb1);
   CHECK(inst[0].np_registered && inst[1].np_registered,
         "both cores register netpacket");
   CHECK(inst[0].np.protocol_version
             && !strcmp(inst[0].np.protocol_version, "vjag-netlink-2"),
         "protocol_version is vjag-netlink-2");

   make_rom();
   memset(&game, 0, sizeof(game));
   game.path = "voice_netpacket_stub.j64";
   game.data = rom_buf;
   game.size = ROM_SIZE;

   for (i = 0; i < 2; i++)
   {
      inst[i].set_video_refresh(video_cb);
      inst[i].set_audio_sample(audio_sample_cb);
      inst[i].set_audio_sample_batch(audio_batch_cb);
      inst[i].set_input_poll(input_poll_cb);
      inst[i].set_input_state(input_state_cb);
      inst[i].init();
      if (!inst[i].load_game(&game))
      {
         fprintf(stderr, "load_game failed on %s\n", inst[i].name);
         return 0;
      }
   }

   inst[0].np.start(0, send0, poll_recv0);
   inst[1].np.start(1, send1, poll_recv1);
   CHECK(inst[0].jlink_mode() == 4 && inst[1].jlink_mode() == 4,
         "netpacket session active on both");
   return 1;
}

static void teardown_pair(void)
{
   int i;
   for (i = 0; i < 2; i++)
   {
      if (inst[i].np.stop)
         inst[i].np.stop();
      if (inst[i].unload_game)
         inst[i].unload_game();
      if (inst[i].deinit)
         inst[i].deinit();
      if (inst[i].handle)
         dlclose(inst[i].handle);
      inst_cleanup(&inst[i]);
      memset(&inst[i], 0, sizeof(inst[i]));
   }
}

static int scenario_both_voice(const char *core_path)
{
   int frame;
   int ready = 0;

   printf("--- scenario: voice on both ---\n");
   if (!boot_pair(core_path, 1, 1))
      return 1;

   for (frame = 0; frame < MAX_FRAMES; frame++)
   {
      run_pair_frames(1);
      if (inst[0].pkts_hello > 0 && inst[1].pkts_hello > 0
          && inst[0].pkts_voice > 0 && inst[1].pkts_voice > 0)
      {
         ready = 1;
         break;
      }
      if (inst[0].jlink_np_voice_ready && inst[1].jlink_np_voice_ready
          && inst[0].jlink_np_voice_ready()
          && inst[1].jlink_np_voice_ready()
          && inst[0].pkts_voice > 0)
      {
         ready = 1;
         /* keep running a bit for mix energy */
         if (frame > 30)
            break;
      }
   }
   /* Extra frames so MixInto sees jitter samples. */
   run_pair_frames(60);

   CHECK(inst[0].pkts_hello > 0 && inst[1].pkts_hello > 0,
         "both sides emitted NP_VOICE_HELLO");
   CHECK(inst[0].pkts_voice > 0 && inst[1].pkts_voice > 0,
         "both sides emitted NP_VOICE");
   CHECK(inst[0].voice_unreliable_ok > 0 && inst[1].voice_unreliable_ok > 0,
         "NP_VOICE used unreliable|unsequenced flags");
   CHECK(inst[0].voice_flags_bad == 0 && inst[1].voice_flags_bad == 0,
         "no NP_VOICE went out RELIABLE-only");
   CHECK(inst[0].audio_abs_sum > 10000 || inst[1].audio_abs_sum > 10000,
         "far-side audio mix shows voice energy");
   CHECK(!inst[0].inbox_overflow && !inst[1].inbox_overflow,
         "relay did not drop packets");
   (void)ready;

   printf("a: hello=%u voice=%u uart=%u audio_sum=%lu\n",
          inst[0].pkts_hello, inst[0].pkts_voice, inst[0].pkts_uart,
          inst[0].audio_abs_sum);
   printf("b: hello=%u voice=%u uart=%u audio_sum=%lu\n",
          inst[1].pkts_hello, inst[1].pkts_voice, inst[1].pkts_uart,
          inst[1].audio_abs_sum);

   teardown_pair();
   return 0;
}

static int scenario_data_only(const char *core_path)
{
   time_t t0;
   unsigned voice_after_timeout;

   printf("--- scenario: voice off on peer (data-only) ---\n");
   if (!boot_pair(core_path, 1, 0))
      return 1;

   /* Side A wants voice; B does not. A must time out (~5s) to data-only
    * and emit no NP_VOICE. */
   t0 = time(NULL);
   while ((time(NULL) - t0) < 6)
      run_pair_frames(5);

   voice_after_timeout = inst[0].pkts_voice;
   CHECK(inst[0].pkts_hello > 0, "voice side still offers hello");
   CHECK(inst[1].pkts_hello == 0, "peer with voice off sends no hello");
   CHECK(voice_after_timeout == 0,
         "no NP_VOICE after hello timeout (data-only)");
   if (inst[0].jlink_np_voice_ready)
      CHECK(!inst[0].jlink_np_voice_ready(),
            "JLinkNPVoiceReady stays false (data-only)");

   teardown_pair();
   return 0;
}

int main(int argc, char **argv)
{
   const char *core_path = argc > 1 ? argv[1]
                                    : "./virtualjaguar_libretro.dylib";
   scenario_both_voice(core_path);
   scenario_data_only(core_path);
   printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
   return failures ? 1 : 0;
}
