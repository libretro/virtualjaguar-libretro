/* test/tools/netlink_rebuild_witness.c -- proves netlink_rebuild_host_options()
 * (task 4, #467) actually executes, end to end, through a real dlopen'd
 * core.
 *
 * Why this exists: two other tests look adjacent but don't cover it.
 * netlink_discover_probe.c calls JLinkDiscStart/Poll/PeerCount directly --
 * it never loads the core or calls retro_run(), so it can never reach
 * netlink_rebuild_host_options() (that function lives in libretro.c and is
 * only ever called from inside retro_run()).  netlink_pair.c does load the
 * real core and drive retro_run() in tcp_server/tcp_client mode, but it
 * exits the moment its two sides exchange a test byte -- observed around
 * frame 36, well under a second of wall-clock time.  netlink_last_rebuild_ms
 * starts at 0 and JLinkNowMs() (jlink.c) is real wall-clock milliseconds,
 * not a frame count, so the rebuild's 2s rate-limit gate cannot open until
 * about two real seconds after the core starts running frames.  No test in
 * the suite ran a core that long in a mode that starts discovery, so the
 * "[NETLINK] host picker rebuilt" log line had never fired anywhere.
 *
 * This test closes that gap directly:
 *   1. dlopen the real core, load a synthetic cartridge with
 *      virtualjaguar_netlink=tcp_server (discovery beacons AND listens in
 *      this mode -- see the comment in netlink_apply()).
 *   2. Craft one fake-peer beacon packet with the core's OWN
 *      JLinkDiscEncode() (dlsym'd from the very library under test, so the
 *      wire format can never drift from what its own JLinkDiscDecode()
 *      expects) and deliver it over a real UDP socket to the core's real
 *      discovery listener on 127.0.0.1:JLINK_DISC_PORT.  JLinkDiscPeerSeen()
 *      alone would NOT do: only JLinkDiscPoll()'s real socket-receive path
 *      sets the jlinkDiscChanged flag JLinkDiscConsumeChanged() reads
 *      (jlink.c), so the peer has to arrive as a genuine packet.
 *   3. Call retro_run() in a loop paced against REAL wall-clock time (not
 *      frame count) for several seconds, past the 2s gate.
 *   4. A GET_LOG_INTERFACE callback captures every core log line and
 *      checks for the rebuild's own log text -- a real witness, not an
 *      inference from a green suite.
 *
 * Doubles as the regression test for the review-round-1 visibility fix:
 * SET_CORE_OPTIONS_V2 (which the rebuild calls) tears down and rebuilds
 * RetroArch's whole core_option_manager from definitions that carry no
 * visibility field, so every option comes back visible on the frontend
 * side.  update_option_visibility() only re-pushes SET_CORE_OPTIONS_DISPLAY
 * for a group whose show_* flag CHANGED since the last call, and right
 * after a rebuild none of them did -- so without the force-push fix, rows
 * hidden before the rebuild (CD-only keys on a cartridge session, the
 * mouse-tuning keys with no mouse attached, the host row itself in
 * tcp_server mode) would silently reappear and stay reappeared.  This test
 * records SET_CORE_OPTIONS_DISPLAY like test_option_visibility.c does,
 * checks three independent hidden rows before the rebuild, then re-checks
 * the SAME rows after it fires.
 *
 * Deliberately NOT built on test/harness/harness.h: that shared harness's
 * environment callback (harness.c) has no SET_CORE_OPTIONS_DISPLAY
 * recording and its log callback isn't interceptable from test code (it
 * always writes straight to stderr), and both are needed here.  Adding
 * either to the shared harness would touch infrastructure every other test
 * depends on for one caller; a small standalone dlopen loader (the same
 * shape test_option_visibility.c already uses) keeps the risk contained to
 * this one file.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./src/jerry -I./libretro-common/include \
 *      -o test/tools/netlink_rebuild_witness \
 *      test/tools/netlink_rebuild_witness.c -ldl
 *
 * Usage: netlink_rebuild_witness [core]
 * Exit 0 on PASS, 1 on FAIL. Requires TEST_EXPORTS=1 (JLinkDiscEncode /
 * JLinkDiscPeerCount / JLinkNowMs must be exported; see exports-test.list's
 * _JLink* wildcard).
 */

#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dlfcn.h>

#include <libretro.h>
#include "jlink_discover.h"

/* Must match the core's JLinkDiscPort(): VJ_DISC_PORT overrides the fixed
   protocol port so concurrent `make test` runs cannot have the kernel
   load-balance one run's beacon into the other run's listener (SO_REUSEPORT
   makes that silent rather than an EADDRINUSE). */
static int witness_disc_port(void)
{
   const char *e = getenv("VJ_DISC_PORT");
   if (e && e[0])
   {
      int v = atoi(e);
      if (v > 0 && v < 65536)
         return v;
   }
   return JLINK_DISC_PORT;
}


#define ROM_SIZE          131072
#define FRAME_USEC        16667  /* ~60 fps, matches the real frontend cadence
                                   * JLinkNowMs()'s real-time-based gate assumes */
#define WITNESS_TIMEOUT_MS 6000  /* generous margin over the 2000ms gate: 1s
                                   * beacon cadence + 2s rate limit + CI slop */
#define MAX_REC           64

/* ---------------------------------------------------------------------
 * SET_CORE_OPTIONS_DISPLAY recording (same shape as test_option_visibility.c)
 * --------------------------------------------------------------------- */
static struct { char key[64]; bool visible; } rec[MAX_REC];
static unsigned rec_count;
static int failures;
static int checks;

static void record(const char *key, bool visible)
{
   unsigned i;
   for (i = 0; i < rec_count; i++)
   {
      if (!strcmp(rec[i].key, key))
      {
         rec[i].visible = visible;
         return;
      }
   }
   if (rec_count < MAX_REC)
   {
      strncpy(rec[rec_count].key, key, sizeof(rec[0].key) - 1);
      rec[rec_count].key[sizeof(rec[0].key) - 1] = '\0';
      rec[rec_count].visible = visible;
      rec_count++;
   }
}

/* Options default to visible: the core only calls SET_CORE_OPTIONS_DISPLAY
 * when a state CHANGES, so "never mentioned" means visible. */
static bool visible_of(const char *key)
{
   unsigned i;
   for (i = 0; i < rec_count; i++)
      if (!strcmp(rec[i].key, key))
         return rec[i].visible;
   return true;
}

static void expect_hidden(const char *what, const char *key)
{
   bool got = visible_of(key);
   checks++;
   if (!got)
      printf("  ok   %-28s %s hidden\n", key, what);
   else
   {
      printf("  FAIL %-28s %s visible, expected hidden\n", key, what);
      failures++;
   }
}

/* ---------------------------------------------------------------------
 * Log capture: the actual witness for "the rebuild ran".
 * --------------------------------------------------------------------- */
static int rebuild_seen;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   char buf[512];
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   /* Echo everything -- useful when this test goes red and someone needs
    * to see what the core actually logged instead of just "FAIL". */
   fputs(buf, stdout);
   if (strstr(buf, "host picker rebuilt"))
      rebuild_seen = 1;
}

/* ---------------------------------------------------------------------
 * Environment callback
 * --------------------------------------------------------------------- */
static char g_port[16];

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb;
         return true;

      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = ".";
         return true;

      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
         return true;

      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;

      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      {
         const struct retro_core_option_display *d =
            (const struct retro_core_option_display *)data;
         if (d && d->key)
            record(d->key, d->visible);
         return true;
      }

      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *v = (struct retro_variable *)data;
         v->value = NULL;
         if (v->key && !strcmp(v->key, "virtualjaguar_netlink"))
            v->value = "tcp_server";
         else if (v->key && !strcmp(v->key, "virtualjaguar_netlink_port"))
            v->value = g_port;
         else if (v->key && !strcmp(v->key, "virtualjaguar_netlink_wait"))
            v->value = "disabled";
         return true;
      }
   }
   return false;
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

typedef void   (*set_env_fn)(retro_environment_t);
typedef void   (*set_video_fn)(retro_video_refresh_t);
typedef void   (*set_audio_fn)(retro_audio_sample_t);
typedef void   (*set_audio_batch_fn)(retro_audio_sample_batch_t);
typedef void   (*set_ipoll_fn)(retro_input_poll_t);
typedef void   (*set_istate_fn)(retro_input_state_t);
typedef void   (*void_fn)(void);
typedef bool   (*load_fn)(const struct retro_game_info *);
typedef void   (*run_fn)(void);
typedef size_t (*disc_encode_fn)(uint8_t *, size_t, int, int, const char *);
typedef int    (*disc_count_fn)(void);
typedef uint32_t (*now_ms_fn)(void);

static long long now_usec(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* One frontend-like frame slot: run, then sleep out the remainder -- same
 * idiom as netlink_latency.c's paced_frame(), needed here because
 * JLinkNowMs() (the rebuild's rate-limit clock) is real wall-clock, not
 * frame count. */
static void paced_frame(run_fn run_frame)
{
   long long fstart = now_usec(), spent;
   run_frame();
   spent = now_usec() - fstart;
   if (spent < FRAME_USEC)
      usleep((useconds_t)(FRAME_USEC - spent));
}

static const uint8_t *make_synth_rom(void)
{
   static uint8_t rom_buf[ROM_SIZE];
   memset(rom_buf, 0, ROM_SIZE);
   rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
   rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
   rom_buf[0x2000] = 0x60; rom_buf[0x2001] = 0xFE;   /* bra.s * */
   return rom_buf;
}

int main(int argc, char **argv)
{
   void *lib;
   const char *core = (argc > 1) ? argv[1] : "./virtualjaguar_libretro.dylib";
   struct retro_game_info gi;
   load_fn        p_load;
   void_fn        p_unload, p_deinit;
   run_fn         p_run;
   disc_encode_fn p_encode;
   disc_count_fn  p_peer_count;
   now_ms_fn      p_now_ms;
   int            udp_sock;
   struct sockaddr_in dest;
   uint8_t        beacon[JLINK_DISC_PKT_LEN];
   size_t         beacon_len;
   long long      loop_start, elapsed_ms;
   int            frame_i;
   int            resent = 0;

   printf("=== Netlink host-picker rebuild witness ===\n");

   snprintf(g_port, sizeof(g_port), "%d", 17000 + (int)(getpid() % 4000));

   lib = dlopen(core, RTLD_NOW);
   if (!lib)
   {
      printf("  FAIL: dlopen %s: %s\n", core, dlerror());
      return 1;
   }

   ((set_env_fn)dlsym(lib, "retro_set_environment"))(env_cb);
   ((set_video_fn)dlsym(lib, "retro_set_video_refresh"))(video_cb);
   ((set_audio_fn)dlsym(lib, "retro_set_audio_sample"))(audio_cb);
   ((set_audio_batch_fn)dlsym(lib, "retro_set_audio_sample_batch"))(audio_batch_cb);
   ((set_ipoll_fn)dlsym(lib, "retro_set_input_poll"))(input_poll_cb);
   ((set_istate_fn)dlsym(lib, "retro_set_input_state"))(input_state_cb);
   ((void_fn)dlsym(lib, "retro_init"))();

   p_load       = (load_fn)dlsym(lib, "retro_load_game");
   p_unload     = (void_fn)dlsym(lib, "retro_unload_game");
   p_deinit     = (void_fn)dlsym(lib, "retro_deinit");
   p_run        = (run_fn)dlsym(lib, "retro_run");
   p_encode     = (disc_encode_fn)dlsym(lib, "JLinkDiscEncode");
   p_peer_count = (disc_count_fn)dlsym(lib, "JLinkDiscPeerCount");
   p_now_ms     = (now_ms_fn)dlsym(lib, "JLinkNowMs");

   if (!p_load || !p_unload || !p_deinit || !p_run || !p_encode)
   {
      printf("  FAIL: symbols missing -- build with TEST_EXPORTS=1\n");
      dlclose(lib);
      return 1;
   }

   memset(&gi, 0, sizeof(gi));
   gi.path = "witness.j64";
   gi.data = make_synth_rom();
   gi.size = ROM_SIZE;

   if (!p_load(&gi))
   {
      printf("  FAIL: retro_load_game failed\n");
      dlclose(lib);
      return 1;
   }

   /* --- Before the rebuild: three independent hidden-row baselines ---
    * host row: tcp_server never shows it (only tcp_client dials out).
    * CD-only keys: hidden because a cartridge, not a disc, is loaded.
    * mouse tuning: hidden because no mouse is attached to port 2.
    * These three cover three different update_option_visibility() groups,
    * not just the one task 4 added -- the point of the review finding. */
   printf("[before rebuild]\n");
   expect_hidden("(tcp_server)", "virtualjaguar_netlink_host");
   expect_hidden("(cart)",       "virtualjaguar_cd_boot_mode");
   expect_hidden("(no mouse)",   "virtualjaguar_mouse_sensitivity");

   /* --- Deliver one real UDP beacon for a fake peer, encoded with the
    * core's own JLinkDiscEncode() so the wire format can never drift from
    * what its own decoder expects.  Use a device/port/name that cannot
    * collide with our own core's self-beacon (JLinkDiscPoll ignores a
    * packet whose name+port match its own -- a different fake port alone
    * guarantees no match regardless of hostname). --- */
   beacon_len = p_encode(beacon, sizeof(beacon), JLINK_DISC_DEV_JAGLINK,
                          9999, "witnesspeer");
   if (beacon_len == 0)
   {
      printf("  FAIL: JLinkDiscEncode produced 0 bytes\n");
      p_unload(); p_deinit(); dlclose(lib);
      return 1;
   }

   udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (udp_sock < 0)
   {
      printf("  FAIL: socket(): could not create injector socket\n");
      p_unload(); p_deinit(); dlclose(lib);
      return 1;
   }
   memset(&dest, 0, sizeof(dest));
   dest.sin_family = AF_INET;
   dest.sin_port   = htons((unsigned short)witness_disc_port());
   inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
   sendto(udp_sock, beacon, beacon_len, 0, (struct sockaddr *)&dest, sizeof(dest));

   /* --- Run retro_run() paced against real wall-clock time until either
    * the rebuild's log line appears or WITNESS_TIMEOUT_MS elapses. A
    * bounded deadline so a genuinely broken build fails in seconds rather
    * than hanging. --- */
   loop_start = now_usec();
   frame_i = 0;
   for (;;)
   {
      elapsed_ms = (now_usec() - loop_start) / 1000;
      if (rebuild_seen || elapsed_ms >= WITNESS_TIMEOUT_MS)
         break;

      /* Cheap insurance against a race between the sendto() above and the
       * discovery socket's bind (JLinkDiscStart already ran synchronously
       * inside retro_load_game(), so this should not be needed, but a
       * resend is nearly free and removes the doubt): resend until the
       * core actually reports a peer. */
      if (!resent && p_peer_count && p_peer_count() == 0 && elapsed_ms > 300)
      {
         sendto(udp_sock, beacon, beacon_len, 0,
                (struct sockaddr *)&dest, sizeof(dest));
         resent = 1;
      }

      paced_frame(p_run);
      frame_i++;

      if (frame_i % 60 == 0)
      {
         printf("  ... frame %d, elapsed %lldms, peers=%d\n", frame_i,
                elapsed_ms, p_peer_count ? p_peer_count() : -1);
      }
   }

   if (p_now_ms)
      printf("  JLinkNowMs() at exit: %u\n", (unsigned)p_now_ms());
   printf("  peers seen: %d\n", p_peer_count ? p_peer_count() : -1);

   checks++;
   if (rebuild_seen)
      printf("  ok   rebuild log line observed after %lldms\n", elapsed_ms);
   else
   {
      printf("  FAIL rebuild log line NEVER observed (timeout %dms)\n",
             WITNESS_TIMEOUT_MS);
      failures++;
   }

   /* --- After the rebuild: the same three rows must STILL be hidden.
    * SET_CORE_OPTIONS_V2 (which the rebuild calls) un-hides every option
    * on the frontend side; without the visibility force-push fix, none of
    * update_option_visibility()'s groups would re-push (their show_*
    * flags didn't change), so all three would incorrectly read back
    * visible here. --- */
   if (rebuild_seen)
   {
      printf("[after rebuild]\n");
      expect_hidden("(tcp_server)", "virtualjaguar_netlink_host");
      expect_hidden("(cart)",       "virtualjaguar_cd_boot_mode");
      expect_hidden("(no mouse)",   "virtualjaguar_mouse_sensitivity");
   }

   close(udp_sock);
   p_unload();
   p_deinit();
   dlclose(lib);

   printf("--- Netlink host-picker rebuild witness: %d checks, %d failed ---\n",
          checks, failures);
   return failures ? 1 : 0;
}
