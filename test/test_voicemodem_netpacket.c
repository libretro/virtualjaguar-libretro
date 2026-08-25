/* test_voicemodem_netpacket.c — Voice Modem over libretro netpacket (#494).
 *
 * The Voice Modem is a DEVICE (virtualjaguar_uart_device=voicemodem) in
 * front of whichever TRANSPORT the link uses.  Two transports carry it,
 * and until this test only the TCP one was covered: voicemodem_pair /
 * uv_modem_game_test both drive tcp_server + tcp_client.  The transport
 * most users actually get is the other one — RetroArch's own netplay
 * (JLINK_MODE_NETPACKET), engaged when virtualjaguar_netlink is left
 * disabled, which the option blurb steers people to.  Its framing and
 * flow control differ from the socket path (per-frame batching, flush at
 * TX-burst end, receive() feeding the ring reentrantly), and the modem
 * layers its own 2-byte inter-modem frames on top of that.
 *
 * This test is the netplay frontend AND both players: it dlopens two
 * private COPIES of the core (distinct files, so each image gets its own
 * statics — asserted, not assumed), starts a netpacket session on each,
 * and relays every packet one side sends into the other side's
 * receive().  No sockets, no ports, no ROM, no fixture.
 *
 * Both consoles are then driven through the real Ultra Vortek modem
 * choreography at the JERRY UART registers (the same command words as
 * test/tools/voicemodem_pair.c, docs/voice-modem.md): wake, mode,
 * dial digit, the $6800 poll that ends dialling, the $8100 carrier query
 * with its $A4FC follow-up, and a 4-byte data packet each way ending in
 * the modem-generated $F301.  Nothing here knows the inter-modem frame
 * format: the peer is the core's own voicemodem.c, so the assertions are
 * purely console-visible behaviour.
 *
 * Pacing mirrors voicemodem_pair.c: ASICLK $0FFF (~27 ms/char, that figure
 * inherited from voicemodem_pair.c rather than measured here) with the
 * transmitter topped up once per frame, so a 4-word data packet stays
 * ONE TX burst and the modem's end-of-packet marker fires exactly once.
 *
 * Usage: test_voicemodem_netpacket <core.so|.dylib>
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1   /* glibc: expose POSIX (getpid, chmod) under c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libretro.h>

#define ROM_SIZE     131072
#define INBOX_SIZE   4096
#define RXQ_SIZE     256
#define MAX_FRAMES   3000
#define CONNECT_TRIES 200

/* jlink.h values, spelled out so this test needs no core headers. */
#define VJ_MODE_DISABLED  0
#define VJ_MODE_NETPACKET 4
#define VJ_DEV_VOICEMODEM 1

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

typedef void     (*vj_void_t)(void);
typedef void     (*vj_ww_t)(uint32_t, uint16_t, uint32_t);
typedef uint16_t (*vj_rw_t)(uint32_t, uint32_t);
typedef int      (*vj_int_t)(void);
typedef uint32_t (*vj_u32_t)(void);

typedef struct
{
   int         idx;
   const char *name;
   void       *handle;
   char        path[512];

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

   vj_ww_t   jerry_ww;
   vj_rw_t   jerry_rw;
   vj_int_t  jlink_mode;
   vj_int_t  jlink_device;
   vj_int_t  jlink_connected;
   vj_u32_t  jlink_tx_total;
   vj_u32_t  jlink_rx_total;

   struct retro_netpacket_callback np;
   int      np_registered;

   /* packets the peer sent us, awaiting delivery into this core */
   uint8_t  inbox[INBOX_SIZE];
   size_t   inbox_len;
   int      inbox_overflow;

   unsigned pkts_out;
   unsigned bytes_out;
   int      flags_ok;      /* every outgoing packet was RELIABLE     */
   int      bcast_ok;      /* every outgoing packet was a broadcast  */
} instance;

static instance inst[2];

/* ---- frontend callbacks (one thin trampoline per instance) ---- */

static bool env_common(int idx, unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
         memcpy(&inst[idx].np, data,
                sizeof(struct retro_netpacket_callback));
         inst[idx].np_registered = 1;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         if (!var || !var->key)
            return false;
         if (!strcmp(var->key, "virtualjaguar_netlink"))
         {
            /* Left disabled on purpose: that is exactly the setting
               that hands the link to the frontend's netplay session. */
            var->value = "disabled";
            return true;
         }
         if (!strcmp(var->key, "virtualjaguar_uart_device"))
         {
            var->value = "voicemodem";
            return true;
         }
         return false;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         /* Must be answered explicitly: retro_run reads the bool it
            passes in, so returning true without writing it would let an
            uninitialised value schedule check_variables() at random. */
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

static void send_common(int from, int flags, const void *buf, size_t len,
                        uint16_t client_id)
{
   instance *src = &inst[from];
   instance *dst = &inst[from ^ 1];
   if ((flags & RETRO_NETPACKET_RELIABLE) == 0)
      src->flags_ok = 0;
   if (client_id != RETRO_NETPACKET_BROADCAST)
      src->bcast_ok = 0;
   src->pkts_out++;
   src->bytes_out += (unsigned)len;
   if (!buf || !len)
      return;
   if (dst->inbox_len + len > INBOX_SIZE)
   {
      dst->inbox_overflow = 1;
      return;
   }
   memcpy(dst->inbox + dst->inbox_len, buf, len);
   dst->inbox_len += len;
}

static void send0(int f, const void *b, size_t l, uint16_t c)
{ send_common(0, f, b, l, c); }
static void send1(int f, const void *b, size_t l, uint16_t c)
{ send_common(1, f, b, l, c); }

/* Hand this instance everything the peer has sent so far.  Snapshot,
   clear, THEN call receive(): the callback runs inside the core and can
   re-enter this function through poll_receive, and a packet delivered
   twice would corrupt the inter-modem frame stream. */
static void deliver_inbox(instance *in)
{
   uint8_t buf[INBOX_SIZE];
   size_t len = in->inbox_len;
   if (!len || !in->np.receive)
      return;
   memcpy(buf, in->inbox, len);
   in->inbox_len = 0;
   in->np.receive(buf, len, (uint16_t)(in->idx ^ 1));
}

static void poll_recv0(void) { deliver_inbox(&inst[0]); }
static void poll_recv1(void) { deliver_inbox(&inst[1]); }

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

/* ---- loading two independent images of the core ---- */

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
                     const char *core_path)
{
   const char *tmp = getenv("TMPDIR");
   memset(in, 0, sizeof(*in));
   in->idx = idx;
   in->name = name;
   in->flags_ok = 1;
   in->bcast_ok = 1;
   /* A private copy per instance: the dynamic loader keys already-loaded
      images on the file, so two distinct files give two independent sets
      of core statics (one emulated console each). */
   snprintf(in->path, sizeof(in->path), "%s/vj_np_vm_%ld_%s.lib",
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
   SYM(jerry_ww, "JERRYWriteWord");
   SYM(jerry_rw, "JERRYReadWord");
   SYM(jlink_mode, "JLinkMode");
   SYM(jlink_device, "JLinkDevice");
   SYM(jlink_connected, "JLinkConnected");
   SYM(jlink_tx_total, "JLinkTxTotal");
   SYM(jlink_rx_total, "JLinkRxTotal");
#undef SYM
   return 1;
}

/* ---- console-side script engine ---- */

enum
{
   OP_SEND,       /* send one command word, expect no reply       */
   OP_EXPECT,     /* read one modem message, require this value   */
   OP_ECHO,       /* send a word and require its echo             */
   OP_CONNECT,    /* poll $8100 to $86D0, then the async $A4FC    */
   OP_DATA_OUT,   /* 4 data words as a single TX burst            */
   OP_DATA_IN,    /* 4 data words back, then the modem's $F301    */
   OP_DONE
};

typedef struct
{
   int            op;
   uint16_t       arg;
   const uint8_t *payload;
} step;

typedef struct
{
   instance      *in;
   const char    *name;
   const step    *script;
   int            pc;
   int            sub;
   int            tries;
   uint8_t        rxq[RXQ_SIZE];
   unsigned       rxh;
   unsigned       rxn;
   int            rxq_overflow;
   int            done;
   int            failed;
} side;

/* Software RX FIFO: on hardware Ultra Vortek's DSP drains RBF at I2S
   rate regardless of what the 68K is doing.  Without it, bytes arriving
   while this test is busy transmitting would overrun (OE) and the
   3-byte message stream would slip out of sync. */
static void s_drain(side *s)
{
   while ((s->in->jerry_rw(0xF10032, 0) & 0x0080) && s->rxn < RXQ_SIZE)
   {
      s->rxq[(s->rxh + s->rxn) % RXQ_SIZE] =
         (uint8_t)(s->in->jerry_rw(0xF10030, 0) & 0xFF);
      s->rxn++;
   }

   /* If RBF still has a byte we had nowhere to put, the queue overran.  At
    * this test's volumes (a handful of 3-byte messages per side) 256 entries
    * is comfortably oversized, so this cannot fire today -- it exists so a
    * future overrun reports itself instead of degrading into a "stalled at
    * step N" timeout, which would send the reader hunting the wrong fault. */
   if (s->in->jerry_rw(0xF10032, 0) & 0x0080)
      s->rxq_overflow = 1;
}

static void s_fail(side *s, const char *fmt, unsigned a, unsigned b)
{
   fprintf(stderr, "[%s] step %d: ", s->name, s->pc);
   fprintf(stderr, fmt, a, b);
   fprintf(stderr, "\n");
   s->failed = 1;
}

/* Load one byte if the transmitter can take it.  TBE stays set after the
   first write (shift busy, hold empty), so two bytes go out per frame
   and the hold register is topped up before the shift drains — which is
   what keeps a data packet a single burst. */
static int s_tx(side *s, uint8_t b)
{
   if (!(s->in->jerry_rw(0xF10032, 0) & 0x0100))
      return 0;
   s->in->jerry_ww(0xF10030, b, 0);
   return 1;
}

/* Pop one 3-byte modem message ($FF sync, high, low).  $B1xx ring
   indications repeat on their own cadence and are skipped. */
static int s_msg(side *s, uint16_t *w)
{
   uint8_t sync, hi, lo;
   while (s->rxn >= 3)
   {
      sync = s->rxq[s->rxh];
      hi   = s->rxq[(s->rxh + 1) % RXQ_SIZE];
      lo   = s->rxq[(s->rxh + 2) % RXQ_SIZE];
      s->rxh = (s->rxh + 3) % RXQ_SIZE;
      s->rxn -= 3;
      /* Strict: 0xFF only.  Accepting 0xFE as well used to be tolerated
       * here, but it weakens the one assertion guarding inter-modem frame
       * alignment -- if the stream ever slipped by one byte, the low half of
       * a $FFFE no-digit message landing in sync position would be silently
       * re-aligned rather than reported.  Nothing in this test legitimately
       * sends a 0xFE sync. */
      if (sync != 0xFF)
      {
         s_fail(s, "bad sync byte %02X", sync, 0);
         return 0;
      }
      if (hi == 0xB1)
         continue;
      *w = (uint16_t)(((uint16_t)hi << 8) | lo);
      return 1;
   }
   return 0;
}

/* Advance this side as far as it can go right now.  Returns 1 while it
   is still making progress (each advance consumes either a transmitter
   slot or a queued message, so this terminates). */
static int s_advance(side *s)
{
   const step *st = &s->script[s->pc];
   uint16_t got;

   switch (st->op)
   {
      case OP_DONE:
         s->done = 1;
         return 0;

      case OP_SEND:
      case OP_ECHO:
         if (s->sub < 2)
         {
            uint8_t b = (s->sub == 0) ? (uint8_t)(st->arg & 0xFF)
                                      : (uint8_t)(st->arg >> 8);
            if (!s_tx(s, b))
               return 0;
            s->sub++;
            if (s->sub == 2 && st->op == OP_SEND)
            {
               s->pc++;
               s->sub = 0;
            }
            return 1;
         }
         /* OP_ECHO tail: require the echo */
         if (!s_msg(s, &got))
            return 0;
         if (s->failed)
            return 0;
         if (got != st->arg)
         {
            s_fail(s, "echo %04X, want %04X", got, st->arg);
            return 0;
         }
         s->pc++;
         s->sub = 0;
         return 1;

      case OP_EXPECT:
         if (!s_msg(s, &got))
            return 0;
         if (s->failed)
            return 0;
         if (got != st->arg)
         {
            s_fail(s, "got %04X, want %04X", got, st->arg);
            return 0;
         }
         s->pc++;
         s->sub = 0;
         return 1;

      case OP_CONNECT:
         if (s->sub < 2)
         {
            uint8_t b = (s->sub == 0) ? 0x00 : 0x81;   /* $8100, low first */
            if (!s_tx(s, b))
               return 0;
            s->sub++;
            return 1;
         }
         if (s->sub == 2)
         {
            if (!s_msg(s, &got))
               return 0;
            if (s->failed)
               return 0;
            if (got == 0x8000)
            {
               /* carrier not up yet: the real driver retries too */
               if (++s->tries >= CONNECT_TRIES)
               {
                  s_fail(s, "no carrier after %u $8100 polls", s->tries, 0);
                  return 0;
               }
               s->sub = 0;
               return 1;
            }
            if (got != 0x86D0)
            {
               s_fail(s, "connect reply %04X, want 86D0", got, 0);
               return 0;
            }
            s->sub = 3;
            return 1;
         }
         if (!s_msg(s, &got))
            return 0;
         if (s->failed)
            return 0;
         if (got != 0xA4FC)
         {
            s_fail(s, "follow-up %04X, want A4FC", got, 0);
            return 0;
         }
         s->pc++;
         s->sub = 0;
         return 1;

      case OP_DATA_OUT:
      {
         /* Never gated on anything but TBE: a skipped transmitter slot
            would break the burst and the modem would emit a second
            end-of-packet marker. */
         uint8_t b = (s->sub & 1) ? 0xF0 : st->payload[s->sub >> 1];
         if (!s_tx(s, b))
            return 0;
         s->sub++;
         if (s->sub >= 8)
         {
            s->pc++;
            s->sub = 0;
         }
         return 1;
      }

      case OP_DATA_IN:
      {
         uint16_t want = (s->sub < 4)
            ? (uint16_t)(0xF000 | st->payload[s->sub]) : 0xF301;
         if (!s_msg(s, &got))
            return 0;
         if (s->failed)
            return 0;
         if (got != want)
         {
            s_fail(s, "data %04X, want %04X", got, want);
            return 0;
         }
         s->sub++;
         if (s->sub >= 5)
         {
            s->pc++;
            s->sub = 0;
         }
         return 1;
      }

      default:
         return 0;
   }
}

static void s_pump(side *s)
{
   if (s->done || s->failed)
      return;
   s_drain(s);
   while (!s->done && !s->failed && s_advance(s))
      ;
}

/* ---- the two scripts ---- */

static const uint8_t pkt_dial[4]   = { 0xDE, 0xAD, 0xBE, 0xEF };
static const uint8_t pkt_answer[4] = { 0x11, 0x22, 0x33, 0x44 };

static const step script_dial[] =
{
   { OP_SEND,     0xFFFF, NULL },        /* wake                        */
   { OP_EXPECT,   0xB800, NULL },
   { OP_ECHO,     0x2C80, NULL },        /* originate mode              */
   { OP_ECHO,     0x8A21, NULL },        /* dial digit "1"              */
   { OP_SEND,     0x6800, NULL },        /* ends dialling -> peer rings */
   { OP_EXPECT,   0xFFFE, NULL },        /* no digit heard yet          */
   { OP_CONNECT,  0x0000, NULL },
   { OP_DATA_OUT, 0x0000, pkt_dial },
   { OP_DATA_IN,  0x0000, pkt_answer },
   { OP_DONE,     0x0000, NULL }
};

static const step script_answer[] =
{
   { OP_SEND,     0xFFFF, NULL },        /* wake                        */
   { OP_EXPECT,   0xB800, NULL },
   { OP_ECHO,     0x2480, NULL },        /* answer mode                 */
   { OP_CONNECT,  0x0000, NULL },
   { OP_DATA_OUT, 0x0000, pkt_answer },
   { OP_DATA_IN,  0x0000, pkt_dial },
   { OP_DONE,     0x0000, NULL }
};

/* ---- synthetic ROM (entry vector -> bra.s *) ---- */
static uint8_t rom_buf[ROM_SIZE];

static void make_rom(void)
{
   memset(rom_buf, 0, ROM_SIZE);
   rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
   rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
   rom_buf[0x2000] = 0x60; rom_buf[0x2001] = 0xFE;   /* bra.s * */
}

static void inst_cleanup(instance *in)
{
   if (in->path[0])
      remove(in->path);
}

static int run_test(const char *core_path)
{
   struct retro_game_info game;
   side sides[2];
   int frame;
   int i;

   if (!inst_open(&inst[0], 0, "answer", core_path))
      return 1;
   if (!inst_open(&inst[1], 1, "dial", core_path))
      return 1;

   /* Two files, two images: if the loader deduplicated them the two
      "consoles" would share one set of statics and every assertion below
      would be meaningless. */
   CHECK(dlsym(inst[0].handle, "JLinkDevice")
             != dlsym(inst[1].handle, "JLinkDevice"),
         "two core copies load as independent images");
   if (inst[0].jlink_device == inst[1].jlink_device)
   {
      /* Not a host capability to skip over: without two images there is
         only one emulated console and nothing below means anything.
         dlopen keys already-loaded images on the file itself (dev/ino on
         glibc, the resolved path on dyld), so two copies at distinct
         paths load separately -- unless the copy silently failed and
         both handles name the same file, or this loader deduplicates on
         something else.  The two paths are printed: check they are two
         real, distinct files before blaming the loader. */
      fprintf(stderr, "FAIL: both core copies resolved to ONE loaded "
                      "image, so both 'consoles' would share one set of "
                      "core statics.\n  copies: %s , %s\n",
              inst[0].path, inst[1].path);
      return 1;
   }

   inst[0].set_environment(env_cb0);
   inst[1].set_environment(env_cb1);
   CHECK(inst[0].np_registered && inst[1].np_registered,
         "both cores register the netpacket interface");
   if (!inst[0].np_registered || !inst[1].np_registered)
      return 1;

   make_rom();
   memset(&game, 0, sizeof(game));
   game.path = "voicemodem_netpacket_stub.j64";
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
         return 1;
      }
   }

   CHECK(inst[0].jlink_device() == VJ_DEV_VOICEMODEM
             && inst[1].jlink_device() == VJ_DEV_VOICEMODEM,
         "uart_device option selects the voice modem on both");
   CHECK(inst[0].jlink_mode() == VJ_MODE_DISABLED
             && inst[1].jlink_mode() == VJ_MODE_DISABLED,
         "netlink disabled: link idle until netplay starts");

   /* Netplay session: instance 0 is the host, instance 1 the client. */
   inst[0].np.start(0, send0, poll_recv0);
   inst[1].np.start(1, send1, poll_recv1);
   CHECK(inst[0].jlink_mode() == VJ_MODE_NETPACKET
             && inst[1].jlink_mode() == VJ_MODE_NETPACKET,
         "start(): both links switch to netpacket mode");
   CHECK(inst[0].jlink_device() == VJ_DEV_VOICEMODEM
             && inst[1].jlink_device() == VJ_DEV_VOICEMODEM,
         "netpacket session keeps the voice modem device");
   CHECK(inst[0].jlink_connected() && inst[1].jlink_connected(),
         "netpacket session reports connected");

   memset(sides, 0, sizeof(sides));
   sides[0].in = &inst[0];
   sides[0].name = "answer";
   sides[0].script = script_answer;
   sides[1].in = &inst[1];
   sides[1].name = "dial";
   sides[1].script = script_dial;

   /* ~27 ms/char (as in voicemodem_pair.c -- test pacing, not a hardware
      claim): slow enough that the transmitter, topped up once per
      frame, never drains mid-packet. */
   for (i = 0; i < 2; i++)
      inst[i].jerry_ww(0xF10034, 0x0FFF, 0);

   for (frame = 0; frame < MAX_FRAMES; frame++)
   {
      if ((sides[0].done && sides[1].done)
          || sides[0].failed || sides[1].failed)
         break;
      for (i = 0; i < 2; i++)
      {
         /* Deliver before pumping, unconditionally: a side parked on a
            read never touches ASISTAT's pump path, so without this the
            two scripts would wait on each other forever. */
         deliver_inbox(&inst[i]);
         s_pump(&sides[i]);
         inst[i].run();
         if (inst[i].np.poll)
            inst[i].np.poll();
         s_pump(&sides[i]);
      }
   }

   for (i = 0; i < 2; i++)
   {
      if (!sides[i].done)
         fprintf(stderr, "[%s] stalled at step %d (sub %d) after %d frames\n",
                 sides[i].name, sides[i].pc, sides[i].sub, frame);
   }
   printf("frames used: %d of %d\n", frame, MAX_FRAMES);

   CHECK(sides[1].done && !sides[1].failed,
         "dial side completes the modem choreography over netpacket");
   CHECK(sides[0].done && !sides[0].failed,
         "answer side completes the modem choreography over netpacket");
   CHECK(inst[0].pkts_out > 0 && inst[1].pkts_out > 0,
         "both sides emitted netpackets");
   CHECK(inst[0].flags_ok && inst[1].flags_ok,
         "every modem packet went out RELIABLE");
   CHECK(inst[0].bcast_ok && inst[1].bcast_ok,
         "every modem packet went out as a broadcast");
   CHECK(!inst[0].inbox_overflow && !inst[1].inbox_overflow,
         "no packet was dropped by the relay");
   CHECK(!sides[0].rxq_overflow && !sides[1].rxq_overflow,
         "neither side's RX FIFO overran");
   CHECK(inst[0].jlink_tx_total() > 0 && inst[0].jlink_rx_total() > 0
             && inst[1].jlink_tx_total() > 0 && inst[1].jlink_rx_total() > 0,
         "transport counters moved in both directions");

   printf("answer: %u packets / %u bytes out, tx %u rx %u\n",
          inst[0].pkts_out, inst[0].bytes_out,
          (unsigned)inst[0].jlink_tx_total(),
          (unsigned)inst[0].jlink_rx_total());
   printf("dial:   %u packets / %u bytes out, tx %u rx %u\n",
          inst[1].pkts_out, inst[1].bytes_out,
          (unsigned)inst[1].jlink_tx_total(),
          (unsigned)inst[1].jlink_rx_total());

   for (i = 0; i < 2; i++)
   {
      inst[i].np.stop();
      CHECK(inst[i].jlink_mode() == VJ_MODE_DISABLED,
            i == 0 ? "answer stop(): link restored to prior mode"
                   : "dial stop(): link restored to prior mode");
      inst[i].unload_game();
      inst[i].deinit();
   }
   return 0;
}

int main(int argc, char **argv)
{
   const char *core_path = argc > 1 ? argv[1]
                                    : "./virtualjaguar_libretro.dylib";
   int rc = run_test(core_path);
   inst_cleanup(&inst[0]);
   inst_cleanup(&inst[1]);
   if (rc != 0)
      failures++;
   printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
   return failures ? 1 : 0;
}
