#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libretro.h>

#include <libretro_core_options.h>
#include <streams/file_stream.h>
#include <compat/posix_string.h>
#include <compat/strl.h>

/* Forward declarations for file stream functions used in CD BIOS loading.
 * These come from libretro-common/streams/file_stream.c. */
RFILE* rfopen(const char *path, const char *mode);
int rfclose(RFILE* stream);
int64_t rfseek(RFILE* stream, int64_t offset, int origin);
int64_t rftell(RFILE* stream);
int64_t rfread(void* buffer, size_t elem_size, size_t elem_count, RFILE* stream);

#include "cheat.h"
#include "crash_detect.h"
#include "crc32.h"
#include "bus_arbiter.h"
#include "file.h"
#include "jagbios.h"
#include "jagcdbios.h"
#include "jagdevcdbios.h"
#include "jaguar.h"
#include "cdintf.h"
#include "cdrom.h"
#include "jagcd_boot.h"
#include "jagcd_hle.h"
#include "dac.h"
#include "dsp.h"
#include "jlink.h"
#include "jlink_netpacket.h"
#include "uart.h"
#include "joystick.h"
#include "settings.h"
#include "shadowfb.h"
#include "tom.h"
#include "blitter.h"
#include "gpu.h"
#include "eeprom.h"
#include "memtrack.h"
#include "jaggd.h"
#include "nvmbios.h"
#include "vjag_memory.h"
#include "state.h"
#include "titledb.h"
#include "log.h"
#include "version.h" /* generated; defines CORE_VERSION */

#define SAMPLERATE 48000
#define BUFPAL  1920
#define BUFNTSC 1600
#define BUFMAX 2048

/* File extensions accepted by the core for retro_load_game.
 * Mirrors what src/core/file.c::ParseFileType() can identify by
 * sniffing the header bytes (sizes/magic), regardless of the
 * filename extension:
 *   j64, jag, rom : standard cart images / JST_ROM / JST_ALPINE
 *   abs           : Removers/aln output, JST_ABS_TYPE1 / TYPE2
 *   cof           : COFF binaries (also routes through JST_ABS_TYPE1)
 *   bin, prg      : conservative headerless raw-homebrew with valid
 *                   68k bootstrap (JST_RAW_BINARY)
 *   cue, cdi      : Jaguar CD images (CUE/BIN and CDI).  Bare `iso`
 *                   images are not bootable -- see docs/cd-known-issues.md. */
#define JAGUAR_VALID_EXTENSIONS "j64|jag|rom|abs|cof|bin|prg|cue|cdi"

/* Framebuffer allocation, in pixels.  Sized for the widest / tallest video
 * mode TOM can be programmed into (TOMWriteWord clamps tomWidth to 1024 and
 * tomHeight to 512), so the buffer never has to be reallocated when the game
 * changes resolution mid-run. */
#define VIDEO_BUFFER_WIDTH   1024
#define VIDEO_BUFFER_HEIGHT  512
#define VIDEO_BUFFER_PIXELS  (VIDEO_BUFFER_WIDTH * VIDEO_BUFFER_HEIGHT)

/* Bounds on what counts as a stale TAIL worth blanking, rather than a sign
 * that TOM and the presented geometry disagree about the video mode.  Both
 * must hold; see the use site in retro_run() for the measurements these are
 * drawn from.  MAX_BLANK_TAIL_ROWS caps the shortfall in absolute rows;
 * MIN_BLANK_COVERAGE_{NUM,DEN} additionally require the frame to be
 * essentially fully rendered, which is the clause that catches the same
 * situation in a short video mode (where a small absolute shortfall can
 * still be most of the screen). */
#define MAX_BLANK_TAIL_ROWS      32
#define MIN_BLANK_COVERAGE_NUM   3
#define MIN_BLANK_COVERAGE_DEN   4

int videoWidth               = 0;
int videoHeight              = 0;
uint32_t *videoBuffer        = NULL;
int game_width               = 0;
int game_height              = 0;

/* Actual videoBuffer allocation in pixels.  VIDEO_BUFFER_PIXELS scaled by
 * N*N when the internal-resolution option is active (N fixed at load; see
 * shadowfb.h).  video_buffer_blank() must cover the real allocation. */
static int video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
/* One-shot latch for the "restart required" notice when the
 * internal-resolution option changes mid-game. */
static int hires_restart_notice_logged = 0;

extern uint16_t eeprom_ram[64];
extern uint16_t cdrom_eeprom_ram[64];
extern uint8_t mtMem[0x20000];
extern uint32_t jaguarMainROMCRC32;
extern void (*eeprom_dirty_cb)(void);

/* Save buffer for RETRO_MEMORY_SAVE_RAM.
 * Regular carts: 128 bytes (64 x 16-bit EEPROM words, big-endian packed),
 *                followed by 128 bytes of CD EEPROM (64 x 16-bit words).
 * Memory Track cart (CRC 0xFDF37F47): mtMem is used directly (128K).
 *
 * The save buffer is kept in sync on every EEPROM write via eeprom_dirty_cb,
 * so frontends that cache the pointer always see current data. */
#define EEPROM_SAVE_SIZE    128  /* 64 x 16-bit words, big-endian */
#define CD_EEPROM_SAVE_SIZE 128  /* CD EEPROM: 64 x 16-bit words */
#define MT_SAVE_SIZE        0x20000  /* 128K Memory Track */
/* CD content carries the EEPROM pair AND a Memory Track, so its save buffer
 * is the two EEPROM banks followed by the MT NVRAM.  Keeping the EEPROMs
 * first means the layout stays a prefix of the cart/CD-EEPROM-only one. */
#define CD_SAVE_SIZE        (EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE + MT_SAVE_SIZE)
static uint8_t eeprom_save_buf[EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE + MT_SAVE_SIZE];
#define MT_SAVE_OFFSET      (EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE)
static void eeprom_pack_save_buf(void);
static void eeprom_unpack_save_buf(void);
static void mt_pack_save_buf(void);

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
retro_audio_sample_batch_t audio_batch_cb;
retro_log_printf_t vj_log_cb = NULL;

static bool libretro_supports_bitmasks = false;
static bool save_data_needs_unpack = false;

/* CD content state. The Tier 1 weak symbols for external_cd_bios[] and
 * cd_bios_loaded_externally are overridden by the strong definitions below. */
static bool jaguar_cd_mode = false;
/* Memory Track presence option (CD only); default on. */
static bool opt_memory_track = true;
static char cd_image_path[4096] = {0};
bool cd_bios_loaded_externally = false;
uint8_t external_cd_bios[0x40000];  /* 256 KB */

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }



#define ANALOG_THRESHOLD 20000
#define BUTTON_NONE 21
#define RETRO_DEVICE_ID_JOYPAD_LU 16
#define RETRO_DEVICE_ID_JOYPAD_LD 17
#define RETRO_DEVICE_ID_JOYPAD_LL 18
#define RETRO_DEVICE_ID_JOYPAD_LR 19
#define RETRO_DEVICE_ID_JOYPAD_RU 20
#define RETRO_DEVICE_ID_JOYPAD_RD 21
#define RETRO_DEVICE_ID_JOYPAD_RL 22
#define RETRO_DEVICE_ID_JOYPAD_RR 23
#define RETROPAD_INPUT_COUNT (RETRO_DEVICE_ID_JOYPAD_RR + 1)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int jag_retropad[2][RETROPAD_INPUT_COUNT];
static int jag_numpad[2][12];
static int numpad_to_kb[2];
static bool show_input_options = true;
/* Content-type-dependent option visibility.  Both default to visible so
 * the options menu is complete before any content is loaded (the type is
 * unknown then, and the user may be configuring ahead of loading). */
static bool content_loaded         = false;
static bool show_cd_options        = true;
static bool show_cart_bios_option  = true;
static bool enable_alt_inputs = false;
static uint8_t *joypad_buttons[2] = { joypad0Buttons, joypad1Buttons };

static int number_keys[12] = {
   RETROK_MINUS,
   RETROK_7,
   RETROK_4,
   RETROK_1,
   RETROK_0,
   RETROK_8,
   RETROK_5,
   RETROK_2,
   RETROK_EQUALS,
   RETROK_9,
   RETROK_6,
   RETROK_3
};

static int keypad_keys[12] = {
   RETROK_KP_DIVIDE,
   RETROK_KP7,
   RETROK_KP4,
   RETROK_KP1,
   RETROK_KP0,
   RETROK_KP8,
   RETROK_KP5,
   RETROK_KP2,
   RETROK_KP_MULTIPLY,
   RETROK_KP9,
   RETROK_KP6,
   RETROK_KP3
};

typedef struct {
   int id;
   char value[10];
} JagMapping;

typedef struct {
   const char *suffix;
   unsigned id;
} RetropadOptionMapping;

static JagMapping jag_map[22] = {
   { BUTTON_U,      "up" },
   { BUTTON_D,      "down" },
   { BUTTON_L,      "left" },
   { BUTTON_R,      "right" },
   { BUTTON_A,      "btn_a" },
   { BUTTON_B,      "btn_b" },
   { BUTTON_C,      "btn_c" },
   { BUTTON_PAUSE,  "pause" },
   { BUTTON_OPTION, "option" },
   { BUTTON_0,      "num_0" },
   { BUTTON_1,      "num_1" },
   { BUTTON_2,      "num_2" },
   { BUTTON_3,      "num_3" },
   { BUTTON_4,      "num_4" },
   { BUTTON_5,      "num_5" },
   { BUTTON_6,      "num_6" },
   { BUTTON_7,      "num_7" },
   { BUTTON_8,      "num_8" },
   { BUTTON_9,      "num_9" },
   { BUTTON_s,      "star" },
   { BUTTON_d,      "hash" },
   { BUTTON_NONE,   "---" }
};

static const RetropadOptionMapping retropad_option_map[] = {
   { "_retropad_up",        RETRO_DEVICE_ID_JOYPAD_UP },
   { "_retropad_down",      RETRO_DEVICE_ID_JOYPAD_DOWN },
   { "_retropad_left",      RETRO_DEVICE_ID_JOYPAD_LEFT },
   { "_retropad_right",     RETRO_DEVICE_ID_JOYPAD_RIGHT },
   { "_retropad_a",         RETRO_DEVICE_ID_JOYPAD_A },
   { "_retropad_b",         RETRO_DEVICE_ID_JOYPAD_B },
   { "_retropad_y",         RETRO_DEVICE_ID_JOYPAD_Y },
   { "_retropad_select",    RETRO_DEVICE_ID_JOYPAD_SELECT },
   { "_retropad_start",     RETRO_DEVICE_ID_JOYPAD_START },
   { "_retropad_x",         RETRO_DEVICE_ID_JOYPAD_X },
   { "_retropad_l1",        RETRO_DEVICE_ID_JOYPAD_L },
   { "_retropad_r1",        RETRO_DEVICE_ID_JOYPAD_R },
   { "_retropad_l2",        RETRO_DEVICE_ID_JOYPAD_L2 },
   { "_retropad_r2",        RETRO_DEVICE_ID_JOYPAD_R2 },
   { "_retropad_l3",        RETRO_DEVICE_ID_JOYPAD_L3 },
   { "_retropad_r3",        RETRO_DEVICE_ID_JOYPAD_R3 },
   { "_retropad_analog_lu", RETRO_DEVICE_ID_JOYPAD_LU },
   { "_retropad_analog_ld", RETRO_DEVICE_ID_JOYPAD_LD },
   { "_retropad_analog_ll", RETRO_DEVICE_ID_JOYPAD_LL },
   { "_retropad_analog_lr", RETRO_DEVICE_ID_JOYPAD_LR },
   { "_retropad_analog_ru", RETRO_DEVICE_ID_JOYPAD_RU },
   { "_retropad_analog_rd", RETRO_DEVICE_ID_JOYPAD_RD },
   { "_retropad_analog_rl", RETRO_DEVICE_ID_JOYPAD_RL },
   { "_retropad_analog_rr", RETRO_DEVICE_ID_JOYPAD_RR },
};

static void build_port_option_key(char *key, size_t key_size, unsigned port, const char *suffix)
{
   size_t len;

   len = strlcpy(key, "virtualjaguar_p", key_size);
   if (len >= key_size)
      return;
   snprintf(key + len, key_size - len, "%u", port + 1);
   strlcat(key, suffix, key_size);
}

static int get_button_id(const char *val)
{
   unsigned i;
   for (i = 0; i <= BUTTON_NONE; i++)
   {
      if (!strcmp(jag_map[i].value, val))
         return jag_map[i].id;
   }
   return BUTTON_NONE;
}

static bool update_option_visibility(void)
{
   struct retro_core_option_display option_display;
   struct retro_variable var;
   bool updated = false;
   unsigned i;

   // Show/hide input options
   bool show_input_options_prev = show_input_options;
   show_input_options = true;

   var.key = "virtualjaguar_alt_inputs";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "disabled"))
      show_input_options = false;

   if (show_input_options != show_input_options_prev)
   {
      option_display.visible = show_input_options;

      for (i = 0; i < 2; i++)
      {
         unsigned j;
         char key[64];

         build_port_option_key(key, sizeof(key), i, "_numpad_to_kb");
         option_display.key = key;
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         for (j = 0; j < ARRAY_SIZE(retropad_option_map); j++)
         {
            build_port_option_key(key, sizeof(key), i, retropad_option_map[j].suffix);
            option_display.key = key;
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         }
      }

      updated = true;
   }

   /* Show/hide options that only apply to one content type.  Filtering
    * is deliberately skipped until content is loaded: with nothing
    * loaded the type is unknown, and hiding either group would make
    * options unreachable for someone configuring ahead of time. */
   {
      static const char * const cd_only_keys[] = {
         "virtualjaguar_cd_bios_type",
         "virtualjaguar_cd_boot_mode",
         "virtualjaguar_cd_read_speed",
         "virtualjaguar_cd_trace",
         "virtualjaguar_memory_track",
      };
      bool show_cd_prev        = show_cd_options;
      bool show_cart_bios_prev = show_cart_bios_option;

      show_cd_options       = (!content_loaded || jaguar_cd_mode);
      /* The cartridge BIOS setting is ignored for CD content —
       * ResolveBootConfig() lets CD Boot Mode drive showBootROM — so
       * showing it there would advertise a control that does nothing. */
      show_cart_bios_option = (!content_loaded || !jaguar_cd_mode);

      if (show_cd_options != show_cd_prev)
      {
         option_display.visible = show_cd_options;
         for (i = 0; i < ARRAY_SIZE(cd_only_keys); i++)
         {
            option_display.key = cd_only_keys[i];
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                       &option_display);
         }
         updated = true;
      }

      if (show_cart_bios_option != show_cart_bios_prev)
      {
         option_display.visible = show_cart_bios_option;
         option_display.key     = "virtualjaguar_bios";
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                    &option_display);
         updated = true;
      }
   }

   return updated;
}

/* Netpacket interface (env 78): registered unconditionally and inert
 * until the frontend starts a netplay session; the session then carries
 * the JagLink byte stream (jlink_netpacket.c), taking over whatever mode
 * the virtualjaguar_netlink option had configured and restoring it on
 * stop.  Broadcast TX makes multi-console (CatNet) sessions work too. */
static const struct retro_netpacket_callback netpacket_cb = {
   JLinkNPStart,
   JLinkNPReceive,
   JLinkNPStop,
   JLinkNPPoll,
   NULL,                /* connected */
   NULL,                /* disconnected */
   "vjag-netlink-1"     /* protocol_version */
};

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   struct retro_core_options_update_display_callback update_display_cb;
   bool option_categories = false;
   bool achievements = true;
   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, (void *)&netpacket_cb);

   {
      struct retro_log_callback log_iface;
      log_iface.log = NULL;
      if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_iface))
         vj_log_cb = log_iface.log;
      else
         vj_log_cb = NULL;
   }

   libretro_set_core_options(environ_cb, &option_categories);
   update_display_cb.callback = update_option_visibility;
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_display_cb);

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);

   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);
}

/* Resolve the TCP endpoint for the network link and apply the mode.
 * Host (client mode): VJ_NETLINK_HOST env, else the
 * virtualjaguar_netlink_host option (any string is accepted verbatim so
 * frontends with free-text option entry can supply arbitrary addresses;
 * the sentinel "vj_netlink.txt" defers to the file), else first line of
 * <system_dir>/vj_netlink.txt, else 127.0.0.1.  Port: VJ_NETLINK_PORT
 * env overrides the virtualjaguar_netlink_port option. */
static void netlink_apply(int mode)
{
   char host[128];
   int port = 42171;
   const char *env;
   struct retro_variable pvar;

   host[0] = '\0';

   pvar.key = "virtualjaguar_netlink_port";
   pvar.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
   {
      int p = atoi(pvar.value);
      if (p > 0)
         port = p;
   }
   env = getenv("VJ_NETLINK_PORT");
   if (env && env[0] && atoi(env) > 0)
      port = atoi(env);

   pvar.key = "virtualjaguar_netlink_wait";
   pvar.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value)
      JLinkSetWaitEnabled(strcmp(pvar.value, "disabled") != 0);

   env = getenv("VJ_NETLINK_HOST");
   if (env && env[0])
   {
      strncpy(host, env, sizeof(host) - 1);
      host[sizeof(host) - 1] = '\0';
   }
   if (!host[0])
   {
      pvar.key = "virtualjaguar_netlink_host";
      pvar.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pvar) && pvar.value
          && pvar.value[0] && strcmp(pvar.value, "vj_netlink.txt") != 0)
      {
         strncpy(host, pvar.value, sizeof(host) - 1);
         host[sizeof(host) - 1] = '\0';
      }
   }
   if (!host[0])
   {
      const char *system_dir = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)
          && system_dir)
      {
         char path[1024];
         FILE *f;
         snprintf(path, sizeof(path), "%s/vj_netlink.txt", system_dir);
         f = fopen(path, "r");
         if (f)
         {
            if (fgets(host, sizeof(host), f))
            {
               size_t n = strlen(host);
               while (n > 0 && (host[n - 1] == '\n' || host[n - 1] == '\r'
                                || host[n - 1] == ' '))
                  host[--n] = '\0';
            }
            fclose(f);
         }
      }
   }

   JLinkSetTCPEndpoint(host[0] ? host : "127.0.0.1", port);
   UARTSetLinkMode(mode);
}

/* Gate for per-title enhancement defaults (issue #368). Read raw (never
 * through get_variable_pertitle()) at the top of check_variables() and once
 * in retro_load_game() before the hires read, so it is never itself
 * substituted by the DB. Defaults to enabled so headless callers/tests that
 * never read the option still get stock behaviour identical to "enabled"
 * with no DB match. */
static bool pertitle_enabled = true;

/* Default value registered for a core option key, from the v2 definitions
 * in option_defs_us[] (libretro_core_options.h). */
static const char *core_option_default(const char *key)
{
   size_t i;
   for (i = 0; option_defs_us[i].key; i++)
      if (!strcmp(option_defs_us[i].key, key))
         return option_defs_us[i].default_value;
   return NULL;
}

/* GET_VARIABLE with per-title defaults (issue #368): when the frontend's
 * value equals the option's registered default (the user never touched it)
 * and the loaded title has a DB entry for this key, substitute the DB
 * value. A user-set non-default value always wins. Logs once per
 * substitution via LOG_INF. */
static bool get_variable_pertitle(struct retro_variable *var)
{
   bool ok = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, var) && var->value;
   const char *ovr, *def;

   if (!pertitle_enabled)
      return ok;

   ovr = TitleDBOverride(var->key);
   if (!ovr)
      return ok;

   def = core_option_default(var->key);
   if (!ok || (def && !strcmp(var->value, def)))
   {
      LOG_INF("[titledb] %s: %s=%s (option at default)\n",
              TitleDBTitleName(), var->key, ovr);
      var->value = ovr;
      return true;
   }
   return ok;
}

static void check_variables(void)
{
   unsigned i;
   struct retro_variable var;
   struct retro_variable gate_var;

   gate_var.key = "virtualjaguar_pertitle_defaults";
   gate_var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gate_var) && gate_var.value)
      pertitle_enabled = (strcmp(gate_var.value, "disabled") != 0);
   else
      pertitle_enabled = true;

   var.key = "virtualjaguar_usefastblitter";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useFastBlitter = true;
      else
         vjs.useFastBlitter = false;
   }

   var.key = "virtualjaguar_true_color";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
      ShadowFBSetEnabled(strcmp(var.value, "enabled") == 0);
   else
      ShadowFBSetEnabled(0);

   /* Internal resolution is applied ONCE at content load (retro_load_game)
    * because the libretro geometry maximum cannot grow mid-session.  Here
    * we only detect a mid-game change and tell the user it needs a
    * restart (design section 7.1). */
   var.key = "virtualjaguar_internal_resolution";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      int hires_want = (strcmp(var.value, "2x") == 0) ? 2 : 1;
      if (hires_want == shadowHiresN)
         hires_restart_notice_logged = 0;
      else if (content_loaded && !hires_restart_notice_logged)
      {
         LOG_INF("[HIRES] internal resolution change to %s takes effect on restart\n",
                 var.value);
         hires_restart_notice_logged = 1;
      }
   }

   var.key = "virtualjaguar_crash_detect";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "verbose") == 0)
         CrashDetectSetMode(CRASH_DETECT_VERBOSE);
      else if (strcmp(var.value, "disabled") == 0)
         CrashDetectSetMode(CRASH_DETECT_OFF);
      else
         CrashDetectSetMode(CRASH_DETECT_ON);
   }
   else
   {
      CrashDetectSetMode(CRASH_DETECT_ON);
   }

   var.key = "virtualjaguar_cd_trace";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
      CDTraceSetEnabled(strcmp(var.value, "enabled") == 0);
   else
      CDTraceSetEnabled(0);

   /* Blitter bus time: a blit kicked by the 68K charges the 68K the
    * blit's whole bus duration through the pending-stall channel (the
    * blitter is the top-priority bus master and the cacheless 68K is
    * frozen while it runs).  Fixes render-bound loops pacing faster
    * than hardware — Doom's menu auto-repeat landing inside a normal
    * button tap (#399/#401).  See BlitDurationSysclks() in
    * src/tom/blitter_mmio.c for the model and its deliberate floor. */
   var.key = "virtualjaguar_blitter_timing";
   var.value = NULL;
   vjs.blitterTiming = false;
   if (get_variable_pertitle(&var) && var.value)
      vjs.blitterTiming = (strcmp(var.value, "enabled") == 0);

   /* DRAM timing: enabled/disabled only, covering BOTH halves of the
    * symmetric self-cost model (GPU stalls in gpu.c, 68K wait-states
    * in jaguar.c).  The calibration scale is deliberately NOT a core
    * option (manual knobs proved untunable on device) — VJ_DRAM_SCALE
    * overrides it for headless calibration experiments only. */
   var.key = "virtualjaguar_dram_timing";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
      busArbiter.enabled = (strcmp(var.value, "enabled") == 0);
   else
      busArbiter.enabled = 0;
   /* No charging happens while disabled, so a carry left over from a
    * runtime toggle (or an older savestate) must not leak into the
    * first charged access when the option is re-enabled.  The
    * pending-stall channel is shared with the blitter bus-time model,
    * so it is only cleared when BOTH chargers are off — otherwise this
    * (default) branch would wipe live blitter charges at every option
    * read. */
   if (!busArbiter.enabled)
   {
      busArbiter.m68k_sysclk_carry = 0;
      busArbiter.refresh_clk_carry = 0;
      busArbiter.op_clk_accum = 0;
      if (!vjs.blitterTiming)
         busArbiter.m68k_pending_stall = 0;
   }
   {
      const char *scale_env = getenv("VJ_DRAM_SCALE");
      int dram_scale = (scale_env && scale_env[0]) ? atoi(scale_env) : 1;
      if (dram_scale < 1)
         dram_scale = 1;
      if (dram_scale > 16)
         dram_scale = 16;
      busArbiter.contention_scale = (uint8_t)dram_scale;
   }

   /* Clock-scale enhancement levers (issue #314).  Config, not state:
    * never serialized.  Stored in percent so 1x is an exact integer
    * identity (see jaguar.h).  Defaults to 100 whenever the option is
    * absent so nothing in the test suite ever runs at non-1x. */
   var.key = "virtualjaguar_m68k_clock_scale";
   var.value = NULL;
   m68kClockScalePct = 100;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "0.5x") == 0)
         m68kClockScalePct = 50;
      else if (strcmp(var.value, "1.5x") == 0)
         m68kClockScalePct = 150;
      else if (strcmp(var.value, "2x") == 0)
         m68kClockScalePct = 200;
      else if (strcmp(var.value, "3x") == 0)
         m68kClockScalePct = 300;
   }
   /* Drop any carried sub-cycle remainder when the scale (possibly)
    * changed, so a new scale starts from a clean accumulator. */
   M68KClockScaleReset();

   var.key = "virtualjaguar_risc_clock_scale";
   var.value = NULL;
   riscClockScalePct = 100;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "0.5x") == 0)
         riscClockScalePct = 50;
      else if (strcmp(var.value, "1.5x") == 0)
         riscClockScalePct = 150;
      else if (strcmp(var.value, "2x") == 0)
         riscClockScalePct = 200;
   }
   /* Drop the GPU's sub-cycle bus-stall remainder when the RISC scale
    * (possibly) changed — mirrors M68KClockScaleReset() above. */
   GPUClockScaleReset();

   if (m68kClockScalePct != 100 || riscClockScalePct != 100)
      LOG_INF("[CLOCK] Non-stock clock scales active: M68K %u%%, RISC %u%% (enhancement mode; timing-sensitive bug reports are only valid at 1x)\n",
              (unsigned)m68kClockScalePct, (unsigned)riscClockScalePct);

   var.key = "virtualjaguar_netlink";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      int mode = JLINK_MODE_DISABLED;
      if (strcmp(var.value, "loopback") == 0)
         mode = JLINK_MODE_LOOPBACK;
      else if (strcmp(var.value, "tcp_server") == 0)
         mode = JLINK_MODE_TCP_SERVER;
      else if (strcmp(var.value, "tcp_client") == 0)
         mode = JLINK_MODE_TCP_CLIENT;
      netlink_apply(mode);
   }
   else
      netlink_apply(JLINK_MODE_DISABLED);

   var.key = "virtualjaguar_bios";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useJaguarBIOS = true;
      else
         vjs.useJaguarBIOS = false;
   }

   var.key = "virtualjaguar_pal";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.hardwareTypeNTSC = false;
      else
         vjs.hardwareTypeNTSC = true;
   }

   var.key = "virtualjaguar_memory_track";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
      opt_memory_track = (strcmp(var.value, "disabled") != 0);

   /* Jaguar GameDrive: mode is latched here; activation happens at
    * content load (JGDLoadROM), so mid-game toggles apply on restart. */
   var.key = "virtualjaguar_jgd";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         JGDSetMode(JGD_MODE_ENABLED);
      else if (strcmp(var.value, "disabled") == 0)
         JGDSetMode(JGD_MODE_DISABLED);
      else
         JGDSetMode(JGD_MODE_AUTO);
   }
   else
      JGDSetMode(JGD_MODE_AUTO);

   var.key = "virtualjaguar_cd_bios_type";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "dev") == 0)
         vjs.cdBiosType = CDBIOS_DEV;
      else
         vjs.cdBiosType = CDBIOS_RETAIL;
   }

   var.key = "virtualjaguar_cd_boot_mode";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "hle") == 0)
         vjs.cdBootMode = CDBOOT_HLE;
      else if (strcmp(var.value, "bios") == 0)
         vjs.cdBootMode = CDBOOT_BIOS;
      else
         vjs.cdBootMode = CDBOOT_AUTO;
   }

   var.key = "virtualjaguar_cd_read_speed";
   var.value = NULL;

   if (get_variable_pertitle(&var) && var.value)
   {
      if (strcmp(var.value, "1x") == 0)
         vjs.cdReadSpeed = CDSPEED_1X;
      else if (strcmp(var.value, "4x") == 0)
         vjs.cdReadSpeed = CDSPEED_4X;
      else if (strcmp(var.value, "8x") == 0)
         vjs.cdReadSpeed = CDSPEED_8X;
      else if (strcmp(var.value, "instant") == 0)
         vjs.cdReadSpeed = CDSPEED_INSTANT;
      else
         vjs.cdReadSpeed = CDSPEED_2X;
   }
   else
      vjs.cdReadSpeed = CDSPEED_2X;

   var.key = "virtualjaguar_alt_inputs";
   var.value = NULL;
   if (get_variable_pertitle(&var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         enable_alt_inputs = true;
      else
         enable_alt_inputs = false;
   }

   for (i = 0; i < 2; i++)
   {
      unsigned j;
      char key[64];

      /* Initialize all retropad mappings to BUTTON_NONE so unmapped
       * entries don't accidentally trigger BUTTON_U (index 0). */
      for (j = 0; j < RETROPAD_INPUT_COUNT; j++)
         jag_retropad[i][j] = BUTTON_NONE;

      build_port_option_key(key, sizeof(key), i, "_numpad_to_kb");
      var.key   = key;
      var.value = NULL;
      if (get_variable_pertitle(&var) && var.value)
      {
         if (!strcmp(var.value, "disabled"))
            numpad_to_kb[i] = 0;
         else if (!strcmp(var.value, "numbers"))
            numpad_to_kb[i] = 1;
         else if (!strcmp(var.value, "keypad"))
            numpad_to_kb[i] = 2;
      }

      for (j = 0; j < ARRAY_SIZE(retropad_option_map); j++)
      {
         build_port_option_key(key, sizeof(key), i, retropad_option_map[j].suffix);
         var.key   = key;
         var.value = NULL;
         if (get_variable_pertitle(&var) && var.value)
            jag_retropad[i][retropad_option_map[j].id] = get_button_id(var.value);
      }
   }

   update_option_visibility();
}

static void update_input(void)
{
   unsigned i;
   int16_t ret[2];
   unsigned player;
   if (!input_poll_cb)
      return;

   ret[0] = ret[1] = 0;
   input_poll_cb();

   for(i=BUTTON_FIRST;i<=BUTTON_LAST;i++){
       joypad0Buttons[i] = 0x00;
       joypad1Buttons[i] = 0x00;
   }

   if (libretro_supports_bitmasks)
   {
      for (player = 0; player < 2; player++)
         ret[player] = input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
   }
   else
   {
      for (player = 0; player < 2; player++)
      {
         for (i=RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_R3; ++i)
            if (input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, i))
               ret[player] |= (1 << i);
      }
   }

   if (enable_alt_inputs)
   {
      int16_t analog_val[2][4];

      for (player = 0; player < 2; player++)
      {
         // for buttons remapped to analogs
         analog_val[player][0] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
         analog_val[player][1] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
         analog_val[player][2] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
         analog_val[player][3] = input_state_cb(player, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);

         for (i = RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_RR; i++)
         {
            if (jag_retropad[player][i] == BUTTON_NONE)
               continue;

            if (i < RETRO_DEVICE_ID_JOYPAD_LU) // dpad, buttons and triggers
            {
               if (ret[player] & (1 << i))
                  joypad_buttons[player][jag_retropad[player][i]] = 0xff;
            }
            else if (i > RETRO_DEVICE_ID_JOYPAD_R3) // analogs
            {
               switch (i)
               {
                  case RETRO_DEVICE_ID_JOYPAD_LU:
                     if (analog_val[player][0] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LU]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_LD:
                     if (analog_val[player][0] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LD]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_LL:
                     if (analog_val[player][1] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LL]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_LR:
                     if (analog_val[player][1] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_LR]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RU:
                     if (analog_val[player][2] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RU]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RD:
                     if (analog_val[player][2] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RD]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RL:
                     if (analog_val[player][3] < -ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RL]] = 0xff;
                     break;
                  case RETRO_DEVICE_ID_JOYPAD_RR:
                     if (analog_val[player][3] > ANALOG_THRESHOLD)
                        joypad_buttons[player][jag_retropad[player][RETRO_DEVICE_ID_JOYPAD_RR]] = 0xff;
                     break;
               }
            }
         }

         // numpad buttons to keyboard
         if (numpad_to_kb[player] == 1)
         {
            for (i = 0; i < 12; i++)
               if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, number_keys[i]))
                  joypad_buttons[player][i + 4] = 0xff; // i + 4 because numpad enums start at 4
         }
         else if (numpad_to_kb[player] == 2)
         {
            for (i = 0; i < 12; i++)
               if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keypad_keys[i]))
                  joypad_buttons[player][i + 4] = 0xff;
         }
      }
   }
   else
   {
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
         joypad0Buttons[BUTTON_U] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
         joypad0Buttons[BUTTON_D] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
         joypad0Buttons[BUTTON_L] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
         joypad0Buttons[BUTTON_R] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_A))
         joypad0Buttons[BUTTON_A] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_B))
         joypad0Buttons[BUTTON_B] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
         joypad0Buttons[BUTTON_C] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))
         joypad0Buttons[BUTTON_PAUSE] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_START))
         joypad0Buttons[BUTTON_OPTION] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_X) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_0)? 1 : 0))
         joypad0Buttons[BUTTON_0] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_1)? 1 : 0))
         joypad0Buttons[BUTTON_1] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_2)? 1 : 0))
         joypad0Buttons[BUTTON_2] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_3)? 1 : 0))
         joypad0Buttons[BUTTON_3] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_4)? 1 : 0))
         joypad0Buttons[BUTTON_4] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_5)? 1 : 0))
         joypad0Buttons[BUTTON_5] = 0xff;
      if (ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_6)? 1 : 0))
         joypad0Buttons[BUTTON_6] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_7)? 1 : 0))
         joypad0Buttons[BUTTON_7] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_8)? 1 : 0))
         joypad0Buttons[BUTTON_8] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_9)? 1 : 0))
         joypad0Buttons[BUTTON_9] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_MINUS)? 1 : 0))
         joypad0Buttons[BUTTON_s] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_EQUALS)? 1 : 0))
         joypad0Buttons[BUTTON_d] = 0xff;

      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
         joypad1Buttons[BUTTON_U] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
         joypad1Buttons[BUTTON_D] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
         joypad1Buttons[BUTTON_L] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
         joypad1Buttons[BUTTON_R] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_A))
         joypad1Buttons[BUTTON_A] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_B))
         joypad1Buttons[BUTTON_B] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
         joypad1Buttons[BUTTON_C] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))
         joypad1Buttons[BUTTON_PAUSE] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_START))
         joypad1Buttons[BUTTON_OPTION] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_X) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_p)? 1 : 0))
         joypad1Buttons[BUTTON_0] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_L) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_q)? 1 : 0))
         joypad1Buttons[BUTTON_1] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_R) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_w)? 1 : 0))
         joypad1Buttons[BUTTON_2] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_L2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_e)? 1 : 0))
         joypad1Buttons[BUTTON_3] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_R2) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_r)? 1 : 0))
         joypad1Buttons[BUTTON_4] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_L3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_t)? 1 : 0))
         joypad1Buttons[BUTTON_5] = 0xff;
      if (ret[1] & (1 << RETRO_DEVICE_ID_JOYPAD_R3) || (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_y)? 1 : 0))
         joypad1Buttons[BUTTON_6] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_u)? 1 : 0))
         joypad1Buttons[BUTTON_7] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_i)? 1 : 0))
         joypad1Buttons[BUTTON_8] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_o)? 1 : 0))
         joypad1Buttons[BUTTON_9] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LEFTBRACKET)? 1 : 0))
         joypad1Buttons[BUTTON_s] = 0xff;
      if ((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RIGHTBRACKET)? 1 : 0))
         joypad1Buttons[BUTTON_d] = 0xff;
   }
}

/************************************
 * libretro implementation
 ************************************/

static struct retro_system_av_info g_av_info;

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Virtual Jaguar";
   info->library_version  = CORE_VERSION;
   info->need_fullpath    = false;
   info->valid_extensions = JAGUAR_VALID_EXTENSIONS;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = vjs.hardwareTypeNTSC ? 60 : 50;
   info->timing.sample_rate    = SAMPLERATE;
   info->geometry.base_width   = game_width;
   info->geometry.base_height  = game_height;
   /* Hi-res: the maxima scale by the (load-time-fixed) internal
    * resolution factor; shadowHiresN is 1 when the option is off. */
   info->geometry.max_width    = 652 * shadowHiresN; // Highest value encountered during testing
   /* Must bound every height the core can emit, not the nominal active
    * display.  VDB/VDE are game-programmable, so a title can open a window
    * taller than the nominal 240 NTSC lines (yarc programs VDB=25/VDE=507 =
    * 241 lines), and TOMGetVideoModeHeight() accepts anything up to 256.
    * Advertising 240 for NTSC let the core submit frames taller than the
    * declared maximum, which some video drivers clip or drop.  The nominal
    * size is carried by base_height above; this is the allocation bound. */
   info->geometry.max_height   = 256 * shadowHiresN;
   /* Aspect ratio stays 4/3: Nx changes pixel count, not picture shape. */
   info->geometry.aspect_ratio = 4.0 / 3.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

size_t retro_serialize_size(void)
{
   return STATE_SIZE;
}

bool retro_serialize(void *data, size_t size)
{
   uint8_t *buf, *start;
   size_t written;
   uint32_t magic, version, flags, reserved;
   extern uint8_t jerry_ram_8[];
   extern bool lowerField;

   if (!data || size < STATE_SIZE)
      return false;

   start = (uint8_t *)data;
   buf   = start;

   /* Header */
   magic    = STATE_MAGIC;
   version  = STATE_VERSION;
   flags    = 0;
   reserved = 0;
   STATE_SAVE_VAR(buf, magic);
   STATE_SAVE_VAR(buf, version);
   STATE_SAVE_VAR(buf, flags);
   STATE_SAVE_VAR(buf, reserved);

   /* Large memory blocks */
   STATE_SAVE_BUF(buf, jaguarMainRAM, 0x200000);  /* 2 MB main RAM */
   STATE_SAVE_BUF(buf, tomRam8, 0x4000);           /* 16 KB TOM registers */

   STATE_SAVE_BUF(buf, jerry_ram_8, 0x10000);      /* 64 KB JERRY registers */

   /* Jaguar misc state */
   STATE_SAVE_VAR(buf, lowerField);

   /* Module state */
   buf += M68KStateSave(buf);
   buf += GPUStateSave(buf);
   buf += DSPStateSave(buf);
   buf += BlitterStateSave(buf);
   buf += EventStateSave(buf);
   buf += EepromStateSave(buf);
   buf += JERRYStateSave(buf);
   buf += TOMStateSave(buf);
   buf += CDROMStateSave(buf);
   buf += JoystickStateSave(buf);
   buf += MTStateSave(buf);
   buf += NVMBiosStateSave(buf);
   buf += DACStateSave(buf);
   buf += UARTStateSave(buf);

   /* v7: bus-arbiter accumulators (68K DRAM self-cost carry, refresh
    * carry, OP occupancy accumulator, 68K pending-stall deduction). */
   STATE_SAVE_VAR(buf, busArbiter.m68k_sysclk_carry);
   STATE_SAVE_VAR(buf, busArbiter.refresh_clk_carry);
   STATE_SAVE_VAR(buf, busArbiter.op_clk_accum);
   STATE_SAVE_VAR(buf, busArbiter.m68k_pending_stall);
   {
      uint32_t blitterBusy = BlitterTimingGetBusy();
      STATE_SAVE_VAR(buf, blitterBusy);
   }

   /* v8: Jaguar GameDrive chunk (bank pages + SPI engine; all-zero for
    * non-GD content). */
   buf += JGDStateSave(buf);

   written = (size_t)(buf - start);
   if (written > STATE_SIZE)
      return false;

   /* Zero-fill remaining bytes for deterministic save states */
   if (written < STATE_SIZE)
      memset(buf, 0, STATE_SIZE - written);

   return true;
}

bool retro_unserialize(const void *data, size_t size)
{
   const uint8_t *buf;
   uint32_t magic, version, flags, reserved;
   extern uint8_t jerry_ram_8[];
   extern bool lowerField;

   /* Floor at the smallest layout any accepted version can occupy; the
    * exact floor for the declared version is enforced below, once the
    * header has been read.  A flat `size < STATE_SIZE` here would
    * reject every state written before the v9 size increase. */
   if (!data || size < STATE_SIZE_V8)
      return false;

   buf = (const uint8_t *)data;

   /* Validate header */
   STATE_LOAD_VAR(buf, magic);
   STATE_LOAD_VAR(buf, version);
   STATE_LOAD_VAR(buf, flags);
   STATE_LOAD_VAR(buf, reserved);

   /* Accept older layouts down to STATE_MIN_VERSION so a core update does
    * not invalidate the user's existing states; per-module loaders skip
    * fields the older version did not carry.  We always WRITE
    * STATE_VERSION, and states newer than we understand are refused. */
   if (magic != STATE_MAGIC
       || version < STATE_MIN_VERSION || version > STATE_VERSION)
      return false;

   if (version >= STATE_VERSION_DAC_I2S_RING && size < STATE_SIZE)
      return false;

   /* Large memory blocks */
   STATE_LOAD_BUF(buf, jaguarMainRAM, 0x200000);
   STATE_LOAD_BUF(buf, tomRam8, 0x4000);

   STATE_LOAD_BUF(buf, jerry_ram_8, 0x10000);

   /* Jaguar misc state */
   STATE_LOAD_VAR(buf, lowerField);

   /* Module state */
   buf += M68KStateLoad(buf);
   buf += GPUStateLoad(buf);
   buf += DSPStateLoad(buf);
   buf += BlitterStateLoad(buf);
   buf += EventStateLoad(buf);
   buf += EepromStateLoad(buf);
   buf += JERRYStateLoad(buf);
   buf += TOMStateLoad(buf);
   buf += CDROMStateLoad(buf, version);
   buf += JoystickStateLoad(buf);
   buf += MTStateLoad(buf, version);
   if (version >= STATE_VERSION_MEMTRACK_OVERRIDE)
      buf += NVMBiosStateLoad(buf);
   else
      NVMBiosReset();
   buf += DACStateLoad(buf, version);
   if (version >= STATE_VERSION_JERRY_UART)
      buf += UARTStateLoad(buf, version);

   if (version >= STATE_VERSION_BUS_ARBITER)
   {
      STATE_LOAD_VAR(buf, busArbiter.m68k_sysclk_carry);
      STATE_LOAD_VAR(buf, busArbiter.refresh_clk_carry);
      STATE_LOAD_VAR(buf, busArbiter.op_clk_accum);
      STATE_LOAD_VAR(buf, busArbiter.m68k_pending_stall);

      if (version >= STATE_VERSION_BLITTER_TIMING)
      {
         uint32_t blitterBusy;
         STATE_LOAD_VAR(buf, blitterBusy);
         BlitterTimingSetBusy(blitterBusy);
      }
      else
         BlitterTimingSetBusy(0);
   }
   else
   {
      busArbiter.m68k_sysclk_carry = 0;
      busArbiter.refresh_clk_carry = 0;
      busArbiter.op_clk_accum = 0;
      busArbiter.m68k_pending_stall = 0;
      BlitterTimingSetBusy(0);
   }

   if (version >= STATE_VERSION_JAGGD)
      buf += JGDStateLoad(buf);
   else
      /* Pre-v8 states carry no GameDrive chunk: reset mapping (identity
       * pages, write protect, idle SPI) — the game re-installs. */
      JGDReset();
   /* tomRam8 was restored raw above; recompute the DRAM/refresh timing
    * that bus_arbiter derives from MEMCON1/MEMCON2 so it matches the
    * loaded state (dram_row_miss/rom_clocks/dram_refresh_clks from
    * MEMCON1, refrate from MEMCON2 — none of those derived fields are
    * themselves serialized, see bus_arbiter.h). */
   bus_arbiter_update_memcon(TOMGetMEMCON1());
   bus_arbiter_update_memcon2(TOMGetMEMCON2());

   JaguarApplyHLEBIOSState();

   /* The true-color shadow framebuffer is a derived cache over RAM
    * contents that were just replaced wholesale; drop every entry
    * (never serialized -- see shadowfb.h).  Ditto the hi-res shadow
    * surface: invalidation cost is per stock word, independent of N. */
   ShadowFBInvalidate();
   ShadowHiresInvalidate();

   /* The 68K->RISC-RAM 16-bit-port latch is deliberately not serialized
    * (see jaguar.c): dropping an unpaired low word is what hardware does.
    * But it must actually be dropped here, or a pending pre-load word
    * could commit against a partner write issued after the load. */
   M68KResetRiscWordLatch();

   return true;
}

/* Cheat codes — the parser and list management live in src/core/cheat.c so
 * they can be unit-tested without the rest of the emulator. Here we just
 * bind them to the Jaguar memory bus and re-apply every frame so games
 * that continuously overwrite the patched location are held to the
 * cheat value. */
static cheat_list_t cheat_list;

static void cheat_write_jaguar(uint32_t addr, uint32_t value,
                               uint8_t size, void *user)
{
   (void)user;
   switch (size)
   {
      case 1: JaguarWriteByte(addr, (uint8_t)value,  UNKNOWN); break;
      case 2: JaguarWriteWord(addr, (uint16_t)value, UNKNOWN); break;
      case 4: JaguarWriteLong(addr, value,           UNKNOWN); break;
      default:
         LOG_WRN("[Virtual Jaguar] cheat: unsupported write size %u at 0x%06X\n",
               (unsigned)size, (unsigned)(addr & 0xFFFFFFU));
         break;
   }
}

void retro_cheat_reset(void)
{
   cheat_list_reset(&cheat_list);
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   cheat_list_set(&cheat_list, index, enabled, code);
}

static void cheat_apply_all(void)
{
   cheat_list_apply(&cheat_list, cheat_write_jaguar, NULL);
}

/* Case-insensitive extension test on a path. */
static bool has_extension(const char *path, const char *ext)
{
   const char *dot;
   if (!path || !ext)
      return false;
   dot = strrchr(path, '.');
   if (!dot)
      return false;
   return strcasecmp(dot + 1, ext) == 0;
}

/* CRC32s of known-good 256 KB CD BIOS dumps.  The developer BIOS has
 * $FFFFFFFF at the $404 run-address slot, so it can only be recognized
 * by checksum -- a header check alone always rejects it. */
#define CD_BIOS_CRC_RETAIL 0x687068D5u
#define CD_BIOS_CRC_DEV    0x55A0669Cu

/* Try to load a 256 KB CD BIOS image from the given path.
 * Returns true on success and sets cd_bios_loaded_externally.
 * A file that exists but fails validation bumps *rejected so the
 * caller's final warning can distinguish "no file" from "bad file". */
static bool try_load_cd_bios_file(const char *path, int *rejected)
{
   RFILE   *f;
   int64_t  size;
   uint32_t run_addr;
   uint32_t crc;

   f = rfopen(path, "rb");
   if (!f)
      return false;

   rfseek(f, 0, SEEK_END);
   size = rftell(f);
   rfseek(f, 0, SEEK_SET);

   if (size != 0x40000)
   {
      LOG_DBG("[CD-BIOS]   wrong size (%lld, need 262144): %s\n",
              (long long)size, path);
      rfclose(f);
      (*rejected)++;
      return false;
   }

   if (rfread(external_cd_bios, 1, 0x40000, f) != 0x40000)
   {
      LOG_DBG("[CD-BIOS]   short read (need 262144): %s\n", path);
      rfclose(f);
      (*rejected)++;
      return false;
   }
   rfclose(f);

   /* Known dumps are accepted by checksum, no header check needed. */
   crc = (uint32_t)crc32_calcCheckSum(external_cd_bios, 0x40000);
   if (crc == CD_BIOS_CRC_RETAIL)
   {
      LOG_INF("[CD-BIOS] using external %s (recognized retail CD BIOS, crc32=%08X)\n",
              path, (unsigned)crc);
      cd_bios_loaded_externally = true;
      return true;
   }
   if (crc == CD_BIOS_CRC_DEV)
   {
      LOG_INF("[CD-BIOS] using external %s (recognized developer CD BIOS, crc32=%08X)\n",
              path, (unsigned)crc);
      cd_bios_loaded_externally = true;
      return true;
   }

   /* Unknown checksum: fall back to the header sanity check so genuine
    * revisions/regions we don't have CRCs for still load. */
   run_addr = ((uint32_t)external_cd_bios[0x404] << 24)
            | ((uint32_t)external_cd_bios[0x405] << 16)
            | ((uint32_t)external_cd_bios[0x406] <<  8)
            |  (uint32_t)external_cd_bios[0x407];

   if (run_addr < 0x800000 || run_addr > 0x840000)
   {
      LOG_DBG("[CD-BIOS]   bad run addr $%08X (crc32=%08X): %s\n",
              (unsigned)run_addr, (unsigned)crc, path);
      (*rejected)++;
      return false;
   }

   LOG_WRN("[CD-BIOS] %s is an unrecognized CD BIOS revision "
           "(crc32=%08X, plausible run=$%06X) -- accepting it, but if boot "
           "black-screens right after this line, this file is the prime "
           "suspect: verify it is a genuine Jaguar CD BIOS dump\n",
           path, (unsigned)crc, (unsigned)run_addr);
   LOG_INF("[CD-BIOS] using external %s (run=$%06X)\n",
           path, (unsigned)run_addr);
   cd_bios_loaded_externally = true;
   return true;
}

/* Search common CD BIOS filenames in the system directory (and a handful
 * of well-known sub-directories used by Provenance/RetroArch front-ends). */
static bool load_external_cd_bios(void)
{
   /* Filenames conventionally used for each 'CD BIOS Type', plus generic
    * names that could hold either image.  The name drives the SEARCH ORDER
    * only; the contents are validated by try_load_cd_bios_file(), which
    * checksums the image and logs which revision it actually is.  So a
    * mislabelled file is still loaded (its name only decided when it was
    * tried), but the log names the real revision rather than the label.
    *
    * The selected type's names are searched FIRST.  The previous code used
    * one flat list with the retail names ahead of the developer ones, so a
    * user with both files installed always got retail no matter what the
    * option said. */
   static const char *retail_names[] = {
      "[BIOS] Atari Jaguar CD (World).j64",
      "[BIOS] Atari Jaguar CD (World).rom",
      "[BIOS] Atari Jaguar CD (World).bin",
      NULL
   };
   static const char *dev_names[] = {
      "[BIOS] Atari Jaguar Developer CD (World).j64",
      "[BIOS] Atari Jaguar Developer CD (World).rom",
      "[BIOS] Atari Jaguar Developer CD (World).bin",
      NULL
   };
   static const char *generic_names[] = {
      "jaguarcd_bios.bin",
      "jagcd_bios.bin",
      "jaguarcd.bin",
      "jagcd.bin",
      "Jaguar CD BIOS.rom",
      "Jaguar CD BIOS.bin",
      NULL
   };
   const char **name_groups[3];
   static const char *sub_dirs[] = {
      "",
      "Atari - Jaguar",
      "Atari - Jaguar CD",
      "jaguar",
      "jaguarcd",
      NULL
   };
   const char *system_dir = NULL;
   int s, i, g;
   int rejected = 0;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)
       || !system_dir)
   {
      LOG_WRN("[CD-BIOS] No system directory available\n");
      return false;
   }

   /* Selected type first, generic names second, the other type last (a
    * lone file of the "wrong" type still beats falling back to embedded —
    * the user put it there on purpose). */
   if (vjs.cdBiosType == CDBIOS_DEV)
   {
      name_groups[0] = dev_names;
      name_groups[2] = retail_names;
   }
   else
   {
      name_groups[0] = retail_names;
      name_groups[2] = dev_names;
   }
   name_groups[1] = generic_names;

   LOG_INF("[CD-BIOS] Searching for CD BIOS in: %s (preferring %s)\n",
           system_dir, (vjs.cdBiosType == CDBIOS_DEV) ? "developer" : "retail");

   for (g = 0; g < 3; g++)
   {
      for (s = 0; sub_dirs[s]; s++)
      {
         for (i = 0; name_groups[g][i]; i++)
         {
            char path[4096];
            if (sub_dirs[s][0])
               snprintf(path, sizeof(path), "%s/%s/%s",
                        system_dir, sub_dirs[s], name_groups[g][i]);
            else
               snprintf(path, sizeof(path), "%s/%s",
                        system_dir, name_groups[g][i]);

            if (try_load_cd_bios_file(path, &rejected))
               return true;
         }
      }
   }

   if (rejected > 0)
      LOG_WRN("[CD-BIOS] no usable CD BIOS in %s: %d candidate file(s) "
              "rejected (size or header); using embedded BIOS\n",
              system_dir, rejected);
   else
      LOG_WRN("[CD-BIOS] CD BIOS not found in %s\n", system_dir);
   return false;
}

/* Stage the CD BIOS for the real-BIOS boot path: prefer an external ROM
 * file from the system directory (users may carry a different BIOS
 * revision), else fall back to an embedded CD BIOS so real-BIOS boot
 * works with zero files.  Which embedded image is used follows the
 * 'CD BIOS Type' option: retail (default) or the developer BIOS, which
 * skips some of the retail BIOS's disc checks.
 *
 * An external file always wins over both — it is the user explicitly
 * supplying a revision, so the type selection does not override it. */
static void stage_cd_bios(void)
{
   if (load_external_cd_bios())
      return;

   if (vjs.cdBiosType == CDBIOS_DEV)
   {
      memcpy(external_cd_bios, jaguarDevCDBootROM, 0x40000);
      cd_bios_loaded_externally = true;
      LOG_INF("[CD-BIOS] using embedded developer CD BIOS\n");
      return;
   }

   memcpy(external_cd_bios, jaguarCDBootROM, 0x40000);
   cd_bios_loaded_externally = true;
   LOG_INF("[CD-BIOS] using embedded retail CD BIOS\n");
}

/* Fill the entire framebuffer allocation with opaque black.
 *
 * TOM only writes the rows of the presented frame that fall inside its
 * visible window, and the border-fill path in TOMExecHalfline writes
 * `tomWidth` pixels -- which is 0 until the game programs a TOM video
 * register ($F00028-$F0004F).  Until then, any presented row above VDB gets
 * no pixels written at all, so whatever this buffer holds is what the
 * frontend displays.  It has to be a defined value, and opaque black is the
 * correct one: it is both what a display shows with no signal and what the
 * border colour resolves to in that window (TOMReset zeroes BORD1/BORD2, and
 * any write that makes them non-zero also makes tomWidth non-zero, so the
 * border path is live by then).
 *
 * Fill the entire allocation rather than videoWidth * videoHeight: the
 * geometry can grow to a wider stride later (320x240 -> 326x240), and rows
 * re-laid-out at the larger pitch reach past the smaller region.  Covering
 * all of it keeps every presentable pixel defined and the X byte consistent
 * no matter which geometry is in force.
 *
 * Both retro_load_game and retro_reset go through here: a reset re-enters
 * exactly the same window (TOMReset puts tomWidth back to 0), so the two
 * must agree on the value. */
static void video_buffer_blank(void)
{
   int i;

   if (!videoBuffer)
      return;

   for (i = 0; i < video_buffer_alloc_pixels; ++i)
      videoBuffer[i] = 0xFF000000;
}

bool retro_load_game(const struct retro_game_info *info)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Numpad 0" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "C" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Numpad 1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Numpad 3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "Numpad 5" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Numpad 2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Numpad 4" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "Numpad 6" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Pause" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Option" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,  "Left Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,  "Left Analog Y" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Numpad 0" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "C" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Numpad 1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Numpad 3" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "Numpad 5" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Numpad 2" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Numpad 4" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "Numpad 6" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Pause" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Option" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,  "Left Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,  "Left Analog Y" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 0 },
   };

   if (!info)
      return false;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   /* Report that save states are deterministic (no quirks).
    * This enables run-ahead and netplay in RetroArch. */
   {
      uint64_t serialization_quirks = 0;
      environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks);
   }

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      //fprintf(stderr, "Pixel format XRGB8888 not supported by platform, cannot use.\n");
      return false;
   }

   /* Feed the per-title DB the loaded content so option reads below (and in
    * check_variables()) can match by CRC (issue #368). info->data is NULL
    * for path-loaded content (CD) -- that correctly clears any match, since
    * v1 only covers cartridge CRCs. */
   if (info->data)
   {
      TitleDBSetContent((const uint8_t *)info->data, info->size);
      /* A patched ROM (RetroArch soft patching, or a pre-patched dump)
       * hashes differently from its retail base, so it matches no row and
       * silently loses that title's enhancement defaults.  Say so (#409). */
      if (!TitleDBTitleName())
         LOG_INF("[titledb] no per-title entry for CRC32 $%08X -- patched or "
                 "unlisted content; enhancement defaults not applied (see "
                 "docs/rom-patches.md)\n", (unsigned)TitleDBContentCRC());
   }
   else
      TitleDBSetContent(NULL, 0);

   /* Raw gate read (never through get_variable_pertitle()) so the hires
    * read just below -- which runs before check_variables() -- is already
    * gated correctly even on a frontend that hasn't called
    * check_variables() yet. */
   {
      struct retro_variable gate_var;
      gate_var.key = "virtualjaguar_pertitle_defaults";
      gate_var.value = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gate_var) && gate_var.value)
         pertitle_enabled = (strcmp(gate_var.value, "disabled") != 0);
      else
         pertitle_enabled = true;
   }

   /* Internal resolution (hi-res Stage 1, see shadowfb.h): read ONCE at
    * content load -- SET_GEOMETRY cannot grow past the advertised maximum,
    * so N is fixed for the session and mid-game option changes only apply
    * on restart (design section 7.1). */
   {
      struct retro_variable hires_var;
      int hires_n = 1;
      hires_var.key = "virtualjaguar_internal_resolution";
      hires_var.value = NULL;
      if (get_variable_pertitle(&hires_var)
          && hires_var.value && strcmp(hires_var.value, "2x") == 0)
         hires_n = 2;
      ShadowHiresSetN(hires_n);
      hires_restart_notice_logged = 0;
   }

   videoWidth           = 320;
   videoHeight          = 240;
   video_buffer_alloc_pixels =
      VIDEO_BUFFER_PIXELS * shadowHiresN * shadowHiresN;
   videoBuffer  = (uint32_t *)calloc(sizeof(uint32_t), video_buffer_alloc_pixels);
   if (!videoBuffer && shadowHiresN > 1)
   {
      /* Nx framebuffer allocation failed: log and run at 1x (scope
       * fence: allocation failure -> log + run at 1x). */
      LOG_WRN("[HIRES] %dx framebuffer allocation failed; running at 1x\n",
              shadowHiresN);
      ShadowHiresShutdown();
      video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
      videoBuffer = (uint32_t *)calloc(sizeof(uint32_t), video_buffer_alloc_pixels);
   }
   sampleBuffer = (uint16_t *)malloc(BUFMAX * sizeof(uint16_t));

   if (!videoBuffer || !sampleBuffer)
   {
      free(videoBuffer);
      free(sampleBuffer);
      videoBuffer = NULL;
      sampleBuffer = NULL;
      return false;
   }
   memset(sampleBuffer, 0, BUFMAX * sizeof(uint16_t));

   game_width           = 320 * shadowHiresN;
   game_height          = 240 * shadowHiresN;

   // Emulate BIOS
   vjs.hardwareTypeNTSC = true;
   vjs.useJaguarBIOS    = false;
   vjs.cdBiosType       = CDBIOS_RETAIL;
   vjs.cdBootMode       = CDBOOT_HLE;
   vjs.cdReadSpeed      = CDSPEED_2X;

   check_variables();

   /* Always identify the exact build in the frontend log: version, short
    * git rev (+"-dirty" for uncommitted trees), and timestamp for DEBUG
    * builds.  Answers "which binary is this?" in every bug report and
    * stops test runs against a stale core from going unnoticed. */
   LOG_INF("[Virtual Jaguar] build: %s\n", CORE_VERSION);

   /* Register EEPROM dirty callback so the save buffer stays in sync */
   eeprom_dirty_cb = eeprom_pack_save_buf;
   mt_dirty_cb     = mt_pack_save_buf;

   /* Detect CD content (CUE/CDI/ISO) and stage a CD BIOS (external file
    * if present, embedded otherwise) so ResolveBootConfig can pick the
    * right boot strategy. */
   jaguar_cd_mode            = false;
   jaguarMemTrackInserted    = false;
   cd_image_path[0]          = '\0';
   cd_bios_loaded_externally = false;

   if (info && info->path && (has_extension(info->path, "cue")
                              || has_extension(info->path, "cdi")
                              || has_extension(info->path, "iso")))
   {
      jaguar_cd_mode = true;
      /* Hardware has the Memory Track cart plugged in alongside the CD
       * unit (user-selectable; some titles behave differently with one
       * present). */
      jaguarMemTrackInserted = opt_memory_track;
      strncpy(cd_image_path, info->path, sizeof(cd_image_path) - 1);
      cd_image_path[sizeof(cd_image_path) - 1] = '\0';

      if (vjs.cdBootMode != CDBOOT_HLE)
         stage_cd_bios();
   }

   /* Resolve boot configuration — single source of truth for which
    * strategy (cart / HLE / real BIOS) we will dispatch to below. */
   ResolveBootConfig(&bootConfig, jaguar_cd_mode, cd_bios_loaded_externally,
                     vjs.cdBootMode, vjs.useJaguarBIOS);
   vjs.useJaguarBIOS = bootConfig.showBootROM;

   /* Open the disc image BEFORE JaguarInit() so CDROMInit -> CDIntfInit ->
    * CDIntfIsImageLoaded sees the disc and haveCDGoodness is set correctly. */
   if (jaguar_cd_mode)
   {
      LOG_INF("[CD] Opening disc image: %s\n", cd_image_path);
      if (!CDIntfOpenImage(cd_image_path))
      {
         LOG_ERR("[CD] CDIntfOpenImage failed for: %s\n", cd_image_path);
         free(videoBuffer);
         videoBuffer = NULL;
         free(sampleBuffer);
         sampleBuffer = NULL;
         return false;
      }
      LOG_INF("[CD] Disc image opened OK\n");
   }

   JaguarInit();                                             // set up hardware
   CrashDetectReset();                                       // zero per-game watchdog state
   memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000); // Use the stock BIOS

   JaguarSetScreenPitch(videoWidth * shadowHiresN);
   JaguarSetScreenBuffer(videoBuffer);

   /* Seed the framebuffer.  See video_buffer_blank() for why opaque black
    * and why the whole allocation. */
   video_buffer_blank();

   /* Dispatch through the selected boot strategy (cart / HLE / real BIOS).
    * The cart strategy handles the existing JaguarLoadFile + JaguarReset
    * flow; the CD strategies handle their own boot sequencing. */
   if (!bootConfig.strategy || !bootConfig.strategy->boot(info))
   {
      LOG_ERR("[Virtual Jaguar] unsupported or invalid content format\n");
      if (jaguar_cd_mode)
         CDIntfCloseImage();
      JaguarDone();
      free(videoBuffer);
      videoBuffer = NULL;
      free(sampleBuffer);
      sampleBuffer = NULL;
      return false;
   }

   /* For RAM-loaded executables (.abs/.cof/JagServer), JaguarReset()
    * randomizes RAM and destroys the loaded program.  The cart and CD
    * boot strategies handle their own JaguarReset() ordering internally
    * so the post-boot state is preserved.  We only need to do an extra
    * reset+reload here for the RAM-loaded path. */
   if (!jaguarCartInserted && !jaguar_cd_mode)
   {
      JaguarReset();
      if (!JaguarLoadFile((uint8_t*)info->data, info->size))
      {
         LOG_ERR("[Virtual Jaguar] failed to reload RAM-loaded content\n");
         JaguarDone();
         free(videoBuffer);
         videoBuffer = NULL;
         free(sampleBuffer);
         sampleBuffer = NULL;
         return false;
      }
   }

   /* Advertise the Jaguar memory map so frontends (RetroArch, etc.) can
    * resolve emulated addresses to host buffers. Required for rcheevos.
    *
    * rcheevos defines one logical system-RAM region for RC_CONSOLE_ATARI_JAGUAR:
    * $000000-$1FFFFF (see rcheevos consoleinfo). RetroAchievements addresses for
    * Jaguar are authored in that space. GPU-style paths mirror 2 MiB within
    * $000000-$7FFFFF in JaguarReadByte, but M68K direct access in this core is
    * only linear $000000-$1FFFFF without those mirrors (m68k_read_memory_*). */
   {
      static struct retro_memory_descriptor descs[1];
      static struct retro_memory_map memmap;

      memset(descs, 0, sizeof(descs));
      descs[0].flags     = RETRO_MEMDESC_SYSTEM_RAM | RETRO_MEMDESC_BIGENDIAN;
      descs[0].ptr       = jaguarMainRAM;
      descs[0].start     = 0x000000;
      descs[0].len       = 0x200000;
      descs[0].addrspace = "RAM";

      memmap.descriptors     = descs;
      memmap.num_descriptors = 1;
      environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &memmap);
   }

   /* The frontend will load .srm data into our save buffer (returned by
    * retro_get_memory_data) after this function returns but before the
    * first retro_run(). We unpack it on the first frame. */
   save_data_needs_unpack = true;

   /* Content type is now known — refresh which options apply to it. */
   content_loaded = true;
   update_option_visibility();

   /* Memory Track NVM BIOS module: on hardware the CD BIOS boot installs
    * it in RAM before the game runs; do the same after the boot strategy
    * has set RAM up. */
   NVMBiosReset();
   if (jaguarMemTrackInserted)
      NVMBiosInstall();

   return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   (void)game_type;
   (void)info;
   (void)num_info;
   return false;
}

void retro_unload_game(void)
{
   retro_cheat_reset();
   CDIntfCloseImage();
   jaguar_cd_mode    = false;
   jaguarMemTrackInserted = false;
   cd_image_path[0]  = '\0';
   /* Content type is unknown again — restore the full option list. */
   content_loaded    = false;
   update_option_visibility();
   JaguarDone();

   if (videoBuffer)
      free(videoBuffer);
   videoBuffer = NULL;
   if (sampleBuffer)
      free(sampleBuffer);
   sampleBuffer = NULL;

   /* Reset all module state so a subsequent retro_load_game in the same
    * process (iOS cannot dlclose cores) starts clean. */
   videoWidth = 0;
   videoHeight = 0;
   game_width = 0;
   game_height = 0;

   /* Hi-res: N is per-load; drop the shadow surface so the next load
    * re-reads the option from scratch (see shadowfb.h). */
   ShadowHiresShutdown();
   video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
   hires_restart_notice_logged = 0;

   eeprom_dirty_cb = NULL;
   mt_dirty_cb     = NULL;
   save_data_needs_unpack = false;
   memset(eeprom_save_buf, 0, sizeof(eeprom_save_buf));

   memset(jag_retropad, 0, sizeof(jag_retropad));
   memset(jag_numpad, 0, sizeof(jag_numpad));
   numpad_to_kb[0] = 0;
   numpad_to_kb[1] = 0;
   show_input_options = true;
   enable_alt_inputs = false;
   content_loaded = false;
   show_cd_options = true;
   show_cart_bios_option = true;
}

unsigned retro_get_region(void)
{
   return vjs.hardwareTypeNTSC ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

/* Pack eeprom_ram[] and cdrom_eeprom_ram[] into the save buffer
 * (big-endian byte order).  Called on every EEPROM write via
 * eeprom_dirty_cb so the buffer is always up-to-date for frontends
 * that cache the pointer. */
static void eeprom_pack_save_buf(void)
{
   unsigned i;
   for (i = 0; i < 64; i++)
   {
      eeprom_save_buf[(i * 2) + 0] = eeprom_ram[i] >> 8;
      eeprom_save_buf[(i * 2) + 1] = eeprom_ram[i] & 0xFF;
   }
   /* CD EEPROM follows cart EEPROM in the save buffer */
   for (i = 0; i < 64; i++)
   {
      eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 0] = cdrom_eeprom_ram[i] >> 8;
      eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 1] = cdrom_eeprom_ram[i] & 0xFF;
   }
   /* Memory Track NVRAM follows both EEPROM banks (CD content only). */
   if (jaguar_cd_mode)
      memcpy(eeprom_save_buf + MT_SAVE_OFFSET, mtMem, MT_SAVE_SIZE);
}

/* Mirror the Memory Track into the save buffer without repacking the EEPROMs
 * -- MT writes are frequent enough during a save that the full pack would be
 * wasteful, and the EEPROM banks are unaffected by them. */
static void mt_pack_save_buf(void)
{
   memcpy(eeprom_save_buf + MT_SAVE_OFFSET, mtMem, MT_SAVE_SIZE);
}

/* Unpack the save buffer back into eeprom_ram[] and cdrom_eeprom_ram[].
 * Called once after the frontend loads .srm data. */
static void eeprom_unpack_save_buf(void)
{
   unsigned i;
   for (i = 0; i < 64; i++)
      eeprom_ram[i] = ((uint16_t)eeprom_save_buf[(i * 2) + 0] << 8)
                    | eeprom_save_buf[(i * 2) + 1];
   for (i = 0; i < 64; i++)
      cdrom_eeprom_ram[i] =
            ((uint16_t)eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 0] << 8)
          |  eeprom_save_buf[EEPROM_SAVE_SIZE + (i * 2) + 1];
   if (jaguar_cd_mode)
      memcpy(mtMem, eeprom_save_buf + MT_SAVE_OFFSET, MT_SAVE_SIZE);
}

void *retro_get_memory_data(unsigned type)
{
   if (type == RETRO_MEMORY_SYSTEM_RAM)
      return jaguarMainRAM;
   if (type == RETRO_MEMORY_SAVE_RAM)
   {
      /* Memory Track cart uses 128K NVRAM directly */
      if (jaguarMainROMCRC32 == 0xFDF37F47)
         return mtMem;
      /* Regular carts: return the pre-packed save buffer */
      return eeprom_save_buf;
   }
   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   if (type == RETRO_MEMORY_SYSTEM_RAM)
      return 0x200000;
   if (type == RETRO_MEMORY_SAVE_RAM)
   {
      if (jaguarMainROMCRC32 == 0xFDF37F47)
         return MT_SAVE_SIZE;
      /* CD discs share the cart EEPROM with their CD-side EEPROM bank
       * (128 + 128 = 256 bytes).  Cart-only loads expose just the cart
       * EEPROM so existing per-game saves remain compatible. */
      /* CD: cart EEPROM + CD EEPROM + Memory Track NVRAM. */
      if (jaguar_cd_mode)
         return CD_SAVE_SIZE;
      return EEPROM_SAVE_SIZE;
   }
   return 0;
}

void retro_init(void)
{
   unsigned level = 18;

   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   /* Reset all bus-arbiter state (iOS cannot dlclose cores, so statics
    * persist across loads).  Must run before check_variables() applies
    * the core option — retro_load_game calls that after retro_init. */
   bus_arbiter_init();

   CrashDetectInit();
}

void retro_deinit(void)
{
   libretro_supports_bitmasks = false;

   /* Belt-and-suspenders: shut down emulator subsystems if the frontend
    * calls deinit without unload first (videoBuffer != NULL means a game
    * was loaded and never unloaded). */
   if (videoBuffer)
   {
      retro_cheat_reset();
      JaguarDone();
      free(videoBuffer);
   }
   videoBuffer = NULL;
   if (sampleBuffer)
      free(sampleBuffer);
   sampleBuffer = NULL;

   /* Free the true-color and hi-res shadow buffers and reset all module
    * statics (iOS cannot dlclose cores, so statics persist across
    * loads). */
   ShadowFBShutdown();
   ShadowHiresShutdown();
   video_buffer_alloc_pixels = VIDEO_BUFFER_PIXELS;
   hires_restart_notice_logged = 0;

   /* Per-title enhancement defaults DB (#368): clear the cached CRC match
    * and re-arm the gate for the next load. */
   TitleDBSetContent(NULL, 0);
   pertitle_enabled = true;

   eeprom_dirty_cb = NULL;
   mt_dirty_cb     = NULL;
   save_data_needs_unpack = false;
   memset(eeprom_save_buf, 0, sizeof(eeprom_save_buf));
   videoWidth = 0;
   videoHeight = 0;
   game_width = 0;
   game_height = 0;
   memset(jag_retropad, 0, sizeof(jag_retropad));
   memset(jag_numpad, 0, sizeof(jag_numpad));
   numpad_to_kb[0] = 0;
   numpad_to_kb[1] = 0;
   show_input_options = true;
   enable_alt_inputs = false;
   content_loaded = false;
   show_cd_options = true;
   show_cart_bios_option = true;
}

void retro_reset(void)
{
   JaguarReset();

   /* Console reset re-runs the CD BIOS boot on hardware, which reinstalls
    * the Memory Track NVM module. */
   NVMBiosReset();
   if (jaguarMemTrackInserted)
      NVMBiosInstall();

   /* Re-blank the framebuffer, or the reset presents the PREVIOUS session's
    * pixels.  TOMReset puts tomWidth back to 0, and the border-fill path in
    * TOMExecHalfline writes tomWidth pixels -- so until the game reprograms
    * a TOM video register ($F00028-$F0004F) the rows above VDB get no pixels
    * written at all and keep what was on screen before the reset.  That is
    * the same window, and the same seed, retro_load_game establishes on a
    * fresh load; a reset has to go through it too. */
   video_buffer_blank();
}

#ifdef DEBUG_PRESENTATION
static unsigned dbg_frame_counter = 0;

static void dbg_dump_frame(void)
{
   const uint32_t *fb = videoBuffer;
   unsigned nb = 0;
   unsigned i;
   uint32_t row0_first = 0, row_mid_first = 0, row_last_first = 0;
   if (!fb) { LOG_INF("[DBG] frame %u videoBuffer=NULL\n", dbg_frame_counter); return; }
   /* Sample 3 row starts and count nonblack across whole framebuffer */
   row0_first = fb[0];
   if (game_height > 0)
   {
      row_mid_first  = fb[(game_height / 2) * game_width];
      row_last_first = fb[(game_height - 1) * game_width];
   }
   for (i = 0; i < (unsigned)(game_width * game_height); i++)
      if (fb[i] & 0x00FFFFFF) nb++;
   LOG_INF("[DBG] frame %u: tom=%ux%u game=%ux%u screenPitch=%u videoBuffer=%p\n"
           "      pixels[0]=0x%08X mid=0x%08X last=0x%08X nonblack=%u/%u\n"
           "      ltxd=0x%04X rtxd=0x%04X dsp_running=%d\n",
           dbg_frame_counter, tomWidth, tomHeight, game_width, game_height,
           screenPitch, (void *)fb, row0_first, row_mid_first, row_last_first,
           nb, (unsigned)(game_width * game_height),
           ltxd ? *ltxd : 0xFFFF, rtxd ? *rtxd : 0xFFFF,
           DSPIsRunning() ? 1 : 0);
}
#endif

void retro_run(void)
{
   bool updated = false;

   /* On the first frame, unpack save data that the frontend loaded
    * into our RETRO_MEMORY_SAVE_RAM buffer after retro_load_game(). */
   if (save_data_needs_unpack)
   {
      save_data_needs_unpack = false;
      if (jaguarMainROMCRC32 != 0xFDF37F47)
         eeprom_unpack_save_buf();
      /* Memory Track: mtMem was written directly, no unpack needed. */
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   /* Apply pending geometry change BEFORE rendering this frame.  TOM's
    * scanline renderer reads tomWidth (pixels per row) and screenPitch
    * (line stride) live; if tomWidth grew but screenPitch is stale, later
    * rows overwrite the tail of earlier rows and the framebuffer comes out
    * scrambled.  Frontends that re-allocate the texture on SET_GEOMETRY
    * (iOS Metal) can also drop the next video_cb if the geometry change
    * arrives after the frame is already submitted at the wrong size.
    * Latching pitch + advertising new geometry up front keeps tomWidth and
    * screenPitch in sync for the entire frame. */
   if ((tomWidth != videoWidth || tomHeight != videoHeight) && tomWidth > 0 && tomHeight > 0)
   {
#ifdef DEBUG_PRESENTATION
      LOG_INF("[DBG] frame %u: GEOMETRY CHANGE %ux%u -> %ux%u (applied pre-render)\n",
              dbg_frame_counter, videoWidth, videoHeight, tomWidth, tomHeight);
#endif
      /* videoWidth/videoHeight track TOM in STOCK units (the change
       * detector above must keep firing on stock transitions); the
       * presented geometry and pitch are the stock size scaled by the
       * load-time internal resolution factor (1 when off). */
      videoWidth = tomWidth, videoHeight = tomHeight;
      game_width = tomWidth * shadowHiresN, game_height = tomHeight * shadowHiresN;

      JaguarSetScreenPitch(game_width);

      retro_get_system_av_info(&g_av_info);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g_av_info);
   }

   /* Service the network link: progress TCP connect/accept, drain the
    * socket into the transport ring, then let the UART start an RX
    * frame for anything that arrived.  FrameTick refills the per-frame
    * reply-wait budget. */
   JLinkFrameTick();
   JLinkPoll();
   UARTPoll();

   update_input();

   /* Hi-res: advance the shadow surface's frame epoch (no-op when off;
    * see shadowfb.h, design section 3.4). */
   ShadowHiresFrameTick();

   DACPrepareFrame(vjs.hardwareTypeNTSC == 1 ? BUFNTSC : BUFPAL);
   JaguarExecuteNew();
   cheat_apply_all();
   SoundCallback(NULL, sampleBuffer, vjs.hardwareTypeNTSC == 1 ? BUFNTSC : BUFPAL);

   /* Give every presented row defined content.
    *
    * TOM renders one row per even halfline in [topVisible, bottomVisible),
    * but the presented height comes from TOMGetVideoModeHeight(), derived
    * independently from VDB/VDE.  When the two disagree the tail rows are
    * never written and keep whatever was last drawn there -- frozen stale
    * pixels that persist while the rest of the screen animates.  Alien vs
    * Predator in-game is the reported case (#178): VDB=40, VDE=2047, VP=523
    * gives 236 rendered rows against a presented height of 240, so rows
    * 236-239 held a brown bar left over from an earlier VDB=28 frame.
    *
    * Opaque black, not the border colour: these rows sit BELOW the visible
    * field (bottomVisible is the field bottom), so they are blanking lines,
    * not border lines.  The border branch in TOMExecHalfline already covers
    * rows that are inside the field but outside the active display window.
    *
    * The reverse mismatch also exists -- a narrow window (e.g. VDB=38,
    * VDE=100 -> 34 rendered rows, presented height 31) writes past the
    * presented height.  That is harmless here (the allocation is 1024x512
    * and the extra rows are simply not shown) and is left alone. */
   {
      uint32_t written = TOMGetWrittenRowExtent();
      /* Hi-res: TOM reports rows in STOCK units.  The decision below is
       * made in stock units so it fires on exactly the same frames as a
       * 1x run (box-replication identity, design section 7.3); only the
       * blanked row range is scaled to Nx. */
      uint32_t hires_n = (uint32_t)shadowHiresN;
      uint32_t stock_height = (uint32_t)game_height / hires_n;

      /* Only blank a genuine TAIL.  A large shortfall does not mean "these
       * rows are stale", it means TOM and the presented geometry disagree
       * about what mode we are in -- our window model has failed, and
       * erasing the last good frame is worse than leaving it up.
       *
       * The bound is measured, not a guess.  Across the 115-ROM corpus every
       * SUSTAINED gap is small: 4 rows (Evolution, and Alien vs Predator --
       * the reported case), 7 (Cannon Fodder, Gorf 2000), 8 (Bubsy), 14
       * (Pitfall), 15, 17 (Sensible Soccer).  The one pathological case is
       * DEMO1C (PD) at 102 rows on a single frame as it switches to a
       * 328x135 mode and then stops rendering entirely -- blanking there
       * erased a visible colour field and left a black screen.  32 leaves
       * generous headroom over the observed maximum while excluding that
       * class by a wide margin.
       *
       * The coverage clause is what makes this hold at any frame height: in
       * a short mode a shortfall well inside 32 rows can still be most of
       * the screen (height 40 with 8 rows written is the DEMO1C shape at
       * small scale), and requiring the frame to be >= 3/4 rendered rejects
       * it.  Measured coverage separates the two classes by a wide margin --
       * every real tail above is 93-98% rendered, DEMO1C's transition frame
       * is 24%.
       *
       * written == 0 (TOM addressed no rows at all) is the degenerate form
       * of the same thing; the coverage test already rejects it, but keep it
       * explicit so the intent survives a future tweak of the bounds. */
      if (written > 0 && written < stock_height
          && stock_height - written <= MAX_BLANK_TAIL_ROWS
          && written * MIN_BLANK_COVERAGE_DEN
             >= stock_height * MIN_BLANK_COVERAGE_NUM)
      {
         uint32_t row;
         uint32_t col;
         for (row = written * hires_n; row < (uint32_t)game_height; row++)
         {
            uint32_t *line = videoBuffer + (row * (uint32_t)game_width);
            for (col = 0; col < (uint32_t)game_width; col++)
               line[col] = 0xFF000000;
         }
      }
   }

   /* Runtime watchdog: looks for GPU/DSP PC escape, GPU/DSP wedge,
    * and video stall. Fires LOG_WRN/LOG_ERR via vj_log_cb so the
    * signature shows up in the RetroArch log without extra wiring.
    * Off-mode short-circuits in CrashDetectFrameTick, so the cost
    * when disabled is one indirect function call per frame. */
   CrashDetectFrameTick(videoBuffer, (unsigned)game_width, (unsigned)game_height);

   video_cb(videoBuffer, game_width, game_height, game_width << 2);

#ifdef DEBUG_PRESENTATION
   if (dbg_frame_counter < 5
       || dbg_frame_counter == 60
       || dbg_frame_counter == 600
       || dbg_frame_counter == 1200
       || dbg_frame_counter == 1800
       || dbg_frame_counter == 3600
       || (dbg_frame_counter % 120) == 0)
      dbg_dump_frame();
   dbg_frame_counter++;
#endif
}
