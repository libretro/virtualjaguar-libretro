/* test/tools/netlink_mismatch_witness.c -- regression witness for #501:
 * the JagLink-vs-Voice-Modem device-mismatch warning must fire on the
 * "From file" (vj_netlink.txt) and VJ_NETLINK_HOST configurations, not
 * just when a discovered peer is picked directly from the host list.
 *
 * The bug: netlink_check_device_mismatch() (libretro.c) compares the
 * selected host against every discovered peer's dotted-quad addr.  The
 * call site with a populated peer table is netlink_rebuild_host_options()
 * -- the apply-time call runs before JLinkDiscStart() in the same
 * function, so at load it always scans an empty table.  That rebuild-path
 * call used to pass the RAW virtualjaguar_netlink_host option value.
 * With the "From file" preset that value is the literal sentinel
 * "vj_netlink.txt", which can never equal an IP address, so the check was
 * dead code for that entire configuration; with VJ_NETLINK_HOST set it
 * scanned for the option's address while the link dialed the env's.  A
 * mismatched player got the silent JagLink<->Voice Modem failure the
 * warning exists to prevent.
 *
 * Why a whole binary: nothing else exercises the resolved-vs-raw
 * distinction.  netlink_rebuild_witness.c proves the rebuild path RUNS,
 * but it runs the core in tcp_server mode (where the mismatch check is
 * skipped outright -- it is gated on JLinkMode() == TCP_CLIENT) with the
 * default host preset, so the raw and resolved values are identical there
 * and the bug is invisible to it.  This test is deliberately the same
 * shape -- dlopen the real core, inject one real UDP beacon encoded by
 * the core's OWN JLinkDiscEncode(), pace retro_run() against wall-clock
 * past the rebuild's 2s rate limit -- with the two things that matter
 * changed: mode is tcp_client, and the host option is the "From file"
 * sentinel with the peer's address supplied only through
 * <system_dir>/vj_netlink.txt.
 *
 * Both witnesses are asserted, not just the toast: the rebuild's own log
 * line ("host picker rebuilt") proves the code path executed at all, and
 * the OSD text proves it reached the right verdict.  Without the first, a
 * timeout with no rebuild would be indistinguishable from a fix that
 * silently does nothing.
 *
 * Run against the pre-#501 code (rebuild passing cur_host) this FAILS:
 * the rebuild fires, the peer is present, and no toast is ever emitted.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./src/jerry -I./libretro-common/include \
 *      -o test/tools/netlink_mismatch_witness \
 *      test/tools/netlink_mismatch_witness.c -ldl
 *
 * Usage: netlink_mismatch_witness [core]
 * Exit 0 on PASS, 1 on FAIL.  Requires TEST_EXPORTS=1 (JLinkDiscEncode /
 * JLinkDiscPeerCount must be exported; see exports-test.list's _JLink*
 * wildcard).
 */

#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
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

#define ROM_SIZE           131072
#define FRAME_USEC         16667  /* ~60 fps, the cadence JLinkNowMs()'s
                                    * real-time gate assumes */
#define WITNESS_TIMEOUT_MS 6000   /* 1s beacon cadence + 2s rate limit + slop */

static int failures;
static int checks;

/* The peer address the core must end up comparing against.  It is the
 * source address of our injected beacon (loopback), and it is reachable
 * ONLY through vj_netlink.txt -- the host option is the sentinel. */
#define PEER_ADDR "127.0.0.1"

static char g_port[16];
static char g_sysdir[512];

/* --------------------------------------------------------------------
 * Witnesses
 * -------------------------------------------------------------------- */
static int rebuild_seen;    /* the rebuild call site executed */
static int mismatch_seen;   /* ... and warned about the device mismatch */

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   char buf[512];
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   fputs(buf, stdout);
   if (strstr(buf, "host picker rebuilt"))
      rebuild_seen = 1;
}

static void note_osd(const char *text)
{
   printf("  [osd] %s\n", text);
   /* Match on the invariant halves, not the whole format string: the
    * wording of the sentence is not what this test is pinning down. */
   if (strstr(text, "Host is running") && strstr(text, "Voice Modem"))
      mismatch_seen = 1;
}

/* --------------------------------------------------------------------
 * Environment callback
 * -------------------------------------------------------------------- */
static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb;
         return true;

      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = g_sysdir;
         return true;

      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
         return true;

      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;

      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      {
         const struct retro_message_ext *m =
            (const struct retro_message_ext *)data;
         if (m && m->msg)
            note_osd(m->msg);
         return true;
      }

      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *v = (struct retro_variable *)data;
         v->value = NULL;
         if (!v->key)
            return true;
         if (!strcmp(v->key, "virtualjaguar_netlink"))
            v->value = "tcp_client";
         /* The whole point: the "From file" preset.  The peer's address
          * appears nowhere in the option values, only in the file. */
         else if (!strcmp(v->key, "virtualjaguar_netlink_host"))
            v->value = "vj_netlink.txt";
         else if (!strcmp(v->key, "virtualjaguar_netlink_port"))
            v->value = g_port;
         else if (!strcmp(v->key, "virtualjaguar_netlink_wait"))
            v->value = "disabled";
         /* virtualjaguar_uart_device left unset -> JagLink, so the
          * Voice Modem beacon below is a genuine mismatch. */
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

static long long now_usec(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

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

/* Private system directory holding vj_netlink.txt, so the file the core
 * reads cannot collide with a parallel run's (or land in the repo root). */
static int make_system_dir(void)
{
   char path[600];
   FILE *f;

   snprintf(g_sysdir, sizeof(g_sysdir), "vj_mismatch_sys_%d", (int)getpid());
   if (mkdir(g_sysdir, 0700) != 0)
   {
      printf("  FAIL: mkdir %s failed\n", g_sysdir);
      return 0;
   }
   snprintf(path, sizeof(path), "%s/vj_netlink.txt", g_sysdir);
   f = fopen(path, "w");
   if (!f)
   {
      printf("  FAIL: cannot write %s\n", path);
      return 0;
   }
   /* Trailing newline on purpose: the resolver has to strip it, and a
    * host of "127.0.0.1\n" would silently never match a peer addr. */
   fprintf(f, "%s\n", PEER_ADDR);
   fclose(f);
   return 1;
}

static void remove_system_dir(void)
{
   char path[600];
   snprintf(path, sizeof(path), "%s/vj_netlink.txt", g_sysdir);
   remove(path);
   rmdir(g_sysdir);
}

static void expect(int cond, const char *what)
{
   checks++;
   if (cond)
      printf("  ok   %s\n", what);
   else
   {
      printf("  FAIL %s\n", what);
      failures++;
   }
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
   int            udp_sock;
   struct sockaddr_in dest;
   uint8_t        beacon[JLINK_DISC_PKT_LEN];
   size_t         beacon_len;
   long long      loop_start, elapsed_ms = 0;
   int            frame_i;
   int            resent = 0;

   printf("=== Netlink device-mismatch witness (#501, 'From file' host) ===\n");

   /* Never the fixed link port range, and never a hard-coded ephemeral
    * port: parallel runs must not collide (see the netlink port-flake
    * history in CLAUDE.md). */
   snprintf(g_port, sizeof(g_port), "%d", 17000 + (int)(getpid() % 4000));

   if (!make_system_dir())
      return 1;

   lib = dlopen(core, RTLD_NOW);
   if (!lib)
   {
      printf("  FAIL: dlopen %s: %s\n", core, dlerror());
      remove_system_dir();
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

   if (!p_load || !p_unload || !p_deinit || !p_run || !p_encode)
   {
      printf("  FAIL: symbols missing -- build with TEST_EXPORTS=1\n");
      dlclose(lib);
      remove_system_dir();
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
      remove_system_dir();
      return 1;
   }

   /* One real beacon for a VOICE MODEM peer.  Encoded with the core's own
    * JLinkDiscEncode() so the wire format cannot drift from its decoder,
    * and advertising a link port that cannot match our own, so the
    * self-beacon filter never eats it. */
   beacon_len = p_encode(beacon, sizeof(beacon), JLINK_DISC_DEV_VOICEMODEM,
                         9999, "vmpeer");
   if (beacon_len == 0)
   {
      printf("  FAIL: JLinkDiscEncode produced 0 bytes\n");
      p_unload(); p_deinit(); dlclose(lib);
      remove_system_dir();
      return 1;
   }

   udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (udp_sock < 0)
   {
      printf("  FAIL: socket(): could not create injector socket\n");
      p_unload(); p_deinit(); dlclose(lib);
      remove_system_dir();
      return 1;
   }
   memset(&dest, 0, sizeof(dest));
   dest.sin_family = AF_INET;
   dest.sin_port   = htons((unsigned short)witness_disc_port());
   inet_pton(AF_INET, PEER_ADDR, &dest.sin_addr);
   sendto(udp_sock, beacon, beacon_len, 0, (struct sockaddr *)&dest,
          sizeof(dest));

   loop_start = now_usec();
   frame_i = 0;
   for (;;)
   {
      elapsed_ms = (now_usec() - loop_start) / 1000;
      if ((rebuild_seen && mismatch_seen) || elapsed_ms >= WITNESS_TIMEOUT_MS)
         break;

      if (!resent && p_peer_count && p_peer_count() == 0 && elapsed_ms > 300)
      {
         sendto(udp_sock, beacon, beacon_len, 0,
                (struct sockaddr *)&dest, sizeof(dest));
         resent = 1;
      }

      paced_frame(p_run);
      frame_i++;

      if (frame_i % 60 == 0)
         printf("  ... frame %d, elapsed %lldms, peers=%d\n", frame_i,
                elapsed_ms, p_peer_count ? p_peer_count() : -1);
   }

   printf("  peers seen: %d (after %lldms)\n",
          p_peer_count ? p_peer_count() : -1, elapsed_ms);

   /* The rebuild check first: without it, "no toast" and "the code never
    * ran" would report identically. */
   expect(rebuild_seen,
          "host-picker rebuild executed (peer table populated)");
   expect(p_peer_count && p_peer_count() > 0,
          "Voice Modem peer present in the discovery table");
   expect(mismatch_seen,
          "device-mismatch warning raised for the 'From file' host");

   close(udp_sock);
   p_unload();
   p_deinit();
   dlclose(lib);
   remove_system_dir();

   printf("--- Netlink device-mismatch witness: %d checks, %d failed ---\n",
          checks, failures);
   return failures ? 1 : 0;
}
