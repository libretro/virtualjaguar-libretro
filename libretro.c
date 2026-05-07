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
#include "file.h"
#include "jagbios.h"
#include "jaguar.h"
#include "cdintf.h"
#include "jagcd_boot.h"
#include "jagcd_hle.h"
#include "dac.h"
#include "dsp.h"
#include "joystick.h"
#include "settings.h"
#include "tom.h"
#include "eeprom.h"
#include "memtrack.h"
#include "vjag_memory.h"
#include "state.h"
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
 * Add `cdi`, `cue`, `iso`, and `chd` here when CD-image support
 * lands on a future PR. */
#define JAGUAR_VALID_EXTENSIONS "j64|jag|rom|abs|cof|bin|prg|cue|cdi|iso"

int videoWidth               = 0;
int videoHeight              = 0;
uint32_t *videoBuffer        = NULL;
int game_width               = 0;
int game_height              = 0;

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
static uint8_t eeprom_save_buf[EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE];
static void eeprom_pack_save_buf(void);
static void eeprom_unpack_save_buf(void);

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

   return updated;
}

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   struct retro_core_options_update_display_callback update_display_cb;
   bool option_categories = false;
   bool achievements = true;
   environ_cb = cb;

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

static void check_variables(void)
{
   unsigned i;
   struct retro_variable var;
   var.key = "virtualjaguar_usefastblitter";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useFastBlitter = true;
      else
         vjs.useFastBlitter = false;
   }

   var.key = "virtualjaguar_bios";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useJaguarBIOS = true;
      else
         vjs.useJaguarBIOS = false;
   }

   var.key = "virtualjaguar_pal";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.hardwareTypeNTSC = false;
      else
         vjs.hardwareTypeNTSC = true;
   }

   var.key = "virtualjaguar_cd_bios_type";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "dev") == 0)
         vjs.cdBiosType = CDBIOS_DEV;
      else
         vjs.cdBiosType = CDBIOS_RETAIL;
   }

   var.key = "virtualjaguar_cd_boot_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "hle") == 0)
         vjs.cdBootMode = CDBOOT_HLE;
      else if (strcmp(var.value, "bios") == 0)
         vjs.cdBootMode = CDBOOT_BIOS;
      else
         vjs.cdBootMode = CDBOOT_AUTO;
   }

   var.key = "virtualjaguar_alt_inputs";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
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
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
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
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
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
   info->geometry.max_width    = 652; // Highest value encountered during testing
   info->geometry.max_height   = vjs.hardwareTypeNTSC ? 240 : 256;
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
   buf += DACStateSave(buf);

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

   if (!data || size < STATE_SIZE)
      return false;

   buf = (const uint8_t *)data;

   /* Validate header */
   STATE_LOAD_VAR(buf, magic);
   STATE_LOAD_VAR(buf, version);
   STATE_LOAD_VAR(buf, flags);
   STATE_LOAD_VAR(buf, reserved);

   if (magic != STATE_MAGIC || version != STATE_VERSION)
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
   buf += CDROMStateLoad(buf);
   buf += JoystickStateLoad(buf);
   buf += MTStateLoad(buf);
   buf += DACStateLoad(buf);

   JaguarApplyHLEBIOSState();

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

/* Try to load a 256 KB CD BIOS image from the given path.
 * Returns true on success and sets cd_bios_loaded_externally. */
static bool try_load_cd_bios_file(const char *path)
{
   RFILE   *f;
   int64_t  size;
   uint32_t run_addr;

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
      return false;
   }

   if (rfread(external_cd_bios, 1, 0x40000, f) != 0x40000)
   {
      rfclose(f);
      return false;
   }
   rfclose(f);

   run_addr = ((uint32_t)external_cd_bios[0x404] << 24)
            | ((uint32_t)external_cd_bios[0x405] << 16)
            | ((uint32_t)external_cd_bios[0x406] <<  8)
            |  (uint32_t)external_cd_bios[0x407];

   if (run_addr < 0x800000 || run_addr > 0x840000)
   {
      LOG_DBG("[CD-BIOS]   bad run addr $%08X: %s\n",
              (unsigned)run_addr, path);
      return false;
   }

   LOG_INF("[CD-BIOS] Loaded CD BIOS: %s (run=$%06X)\n",
           path, (unsigned)run_addr);
   cd_bios_loaded_externally = true;
   return true;
}

/* Search common CD BIOS filenames in the system directory (and a handful
 * of well-known sub-directories used by Provenance/RetroArch front-ends). */
static bool load_external_cd_bios(void)
{
   static const char *bios_names[] = {
      "jaguarcd_bios.bin",
      "jagcd_bios.bin",
      "jaguarcd.bin",
      "jagcd.bin",
      "Jaguar CD BIOS.rom",
      "Jaguar CD BIOS.bin",
      "[BIOS] Atari Jaguar CD (World).j64",
      "[BIOS] Atari Jaguar CD (World).rom",
      "[BIOS] Atari Jaguar CD (World).bin",
      "[BIOS] Atari Jaguar Developer CD (World).j64",
      "[BIOS] Atari Jaguar Developer CD (World).rom",
      "[BIOS] Atari Jaguar Developer CD (World).bin",
      NULL
   };
   static const char *sub_dirs[] = {
      "",
      "Atari - Jaguar",
      "Atari - Jaguar CD",
      "jaguar",
      "jaguarcd",
      NULL
   };
   const char *system_dir = NULL;
   int s, i;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)
       || !system_dir)
   {
      LOG_WRN("[CD-BIOS] No system directory available\n");
      return false;
   }

   LOG_INF("[CD-BIOS] Searching for CD BIOS in: %s\n", system_dir);

   for (s = 0; sub_dirs[s]; s++)
   {
      for (i = 0; bios_names[i]; i++)
      {
         char path[4096];
         if (sub_dirs[s][0])
            snprintf(path, sizeof(path), "%s/%s/%s",
                     system_dir, sub_dirs[s], bios_names[i]);
         else
            snprintf(path, sizeof(path), "%s/%s",
                     system_dir, bios_names[i]);

         if (try_load_cd_bios_file(path))
            return true;
      }
   }

   LOG_WRN("[CD-BIOS] CD BIOS not found in %s\n", system_dir);
   return false;
}

bool retro_load_game(const struct retro_game_info *info)
{
   unsigned i;
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

   videoWidth           = 320;
   videoHeight          = 240;
   videoBuffer  = (uint32_t *)calloc(sizeof(uint32_t), 1024 * 512);
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

   game_width           = 320;
   game_height          = 240;

   // Emulate BIOS
   vjs.hardwareTypeNTSC = true;
   vjs.useJaguarBIOS    = false;
   vjs.cdBiosType       = CDBIOS_RETAIL;
   vjs.cdBootMode       = CDBOOT_HLE;

   check_variables();

#ifdef BUILD_TIMESTAMP
   LOG_INF("[Virtual Jaguar] build: %s\n", CORE_VERSION);
#endif

   /* Register EEPROM dirty callback so the save buffer stays in sync */
   eeprom_dirty_cb = eeprom_pack_save_buf;

   /* Detect CD content (CUE/CDI/ISO) and locate an external CD BIOS so
    * ResolveBootConfig can pick the right boot strategy. */
   jaguar_cd_mode            = false;
   cd_image_path[0]          = '\0';
   cd_bios_loaded_externally = false;

   if (info && info->path && (has_extension(info->path, "cue")
                              || has_extension(info->path, "cdi")
                              || has_extension(info->path, "iso")))
   {
      jaguar_cd_mode = true;
      strncpy(cd_image_path, info->path, sizeof(cd_image_path) - 1);
      cd_image_path[sizeof(cd_image_path) - 1] = '\0';

      if (vjs.cdBootMode != CDBOOT_HLE)
         load_external_cd_bios();
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
   memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000); // Use the stock BIOS

   JaguarSetScreenPitch(videoWidth);
   JaguarSetScreenBuffer(videoBuffer);

   /* Init video */
   for (i = 0; i < videoWidth * videoHeight; ++i)
      videoBuffer[i] = 0xFF00FFFF;

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
   cd_image_path[0]  = '\0';
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

   eeprom_dirty_cb = NULL;
   save_data_needs_unpack = false;
   memset(eeprom_save_buf, 0, sizeof(eeprom_save_buf));

   memset(jag_retropad, 0, sizeof(jag_retropad));
   memset(jag_numpad, 0, sizeof(jag_numpad));
   numpad_to_kb[0] = 0;
   numpad_to_kb[1] = 0;
   show_input_options = true;
   enable_alt_inputs = false;
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
      if (jaguar_cd_mode)
         return EEPROM_SAVE_SIZE + CD_EEPROM_SAVE_SIZE;
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

   eeprom_dirty_cb = NULL;
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
}

void retro_reset(void)
{
   JaguarReset();
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
      videoWidth = tomWidth, videoHeight = tomHeight;
      game_width = tomWidth, game_height = tomHeight;

      JaguarSetScreenPitch(game_width);

      retro_get_system_av_info(&g_av_info);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g_av_info);
   }

   update_input();

   DACPrepareFrame(vjs.hardwareTypeNTSC == 1 ? BUFNTSC : BUFPAL);
   JaguarExecuteNew();
   cheat_apply_all();
   SoundCallback(NULL, sampleBuffer, vjs.hardwareTypeNTSC == 1 ? BUFNTSC : BUFPAL);

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
