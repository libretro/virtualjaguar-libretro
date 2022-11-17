#include <stdlib.h>
#include <string.h>
#include <libretro.h>
#include <streams/file_stream.h>
#include "file.h"
#include "jagbios.h"
#include "jagbios2.h"
#include "jaguar.h"
#include "dac.h"
#include "dsp.h"
#include "joystick.h"
#include "settings.h"
#include "tom.h"

#define SAMPLERATE 48000
#define BUFPAL  1920
#define BUFNTSC 1600
#define BUFMAX 2048

int videoWidth               = 0;
int videoHeight              = 0;
uint32_t *videoBuffer        = NULL;
int game_width               = 0;
int game_height              = 0;

extern uint16_t eeprom_ram[];

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
retro_audio_sample_batch_t audio_batch_cb;

static bool libretro_supports_bitmasks = false;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

int doom_res_hack=0; // Doom Hack to double pixel if pwidth==8 (163*2)

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   struct retro_variable variables[] = {
      {
         "virtualjaguar_usefastblitter",
         "Fast Blitter; disabled|enabled",

      },
      {
         "virtualjaguar_doom_res_hack",
         "Doom Res Hack; disabled|enabled",

      },
      {
         "virtualjaguar_bios",
         "Bios; disabled|enabled",
      },
      {
         "virtualjaguar_pal",
         "Pal (Restart); disabled|enabled",
      },
      { NULL, NULL },
   };

   environ_cb = cb;
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);
}

static void check_variables(void)
{
   struct retro_variable var;
   var.key = "virtualjaguar_usefastblitter";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useFastBlitter=1;
      if (strcmp(var.value, "disabled") == 0)
         vjs.useFastBlitter=0;
   }
   else
      vjs.useFastBlitter=0;

   var.key = "virtualjaguar_doom_res_hack";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         doom_res_hack=1;
      if (strcmp(var.value, "disabled") == 0)
         doom_res_hack=0;
   }
   else
      doom_res_hack=0;

   var.key = "virtualjaguar_bios";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.useJaguarBIOS = true;
      if (strcmp(var.value, "disabled") == 0)
         vjs.useJaguarBIOS = false;
   }
   else
      vjs.useJaguarBIOS = false;

   var.key = "virtualjaguar_pal";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         vjs.hardwareTypeNTSC=0;
      if (strcmp(var.value, "disabled") == 0)
         vjs.hardwareTypeNTSC=1;
   }
   else
      vjs.hardwareTypeNTSC=1;

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
   joypad0Buttons[BUTTON_U]      = 0x00;
   joypad0Buttons[BUTTON_D]      = 0x00;
   joypad0Buttons[BUTTON_L]      = 0x00;
   joypad0Buttons[BUTTON_R]      = 0x00;
   joypad0Buttons[BUTTON_A]      = 0x00;
   joypad0Buttons[BUTTON_B]      = 0x00;
   joypad0Buttons[BUTTON_C]      = 0x00;
   joypad0Buttons[BUTTON_PAUSE]  = 0x00;
   joypad0Buttons[BUTTON_OPTION] = 0x00;
   joypad0Buttons[BUTTON_0]      = 0x00;
   joypad0Buttons[BUTTON_1]      = 0x00;
   joypad0Buttons[BUTTON_2]      = 0x00;
   joypad0Buttons[BUTTON_3]      = 0x00;
   joypad0Buttons[BUTTON_4]      = 0x00;
   joypad0Buttons[BUTTON_5]      = 0x00;
   joypad0Buttons[BUTTON_6]      = 0x00;
   joypad0Buttons[BUTTON_7]      = 0x00;
   joypad0Buttons[BUTTON_8]      = 0x00;
   joypad0Buttons[BUTTON_9]      = 0x00;
   joypad0Buttons[BUTTON_s]      = 0x00;
   joypad0Buttons[BUTTON_d]      = 0x00;

   joypad1Buttons[BUTTON_U]      = 0x00;
   joypad1Buttons[BUTTON_D]      = 0x00;
   joypad1Buttons[BUTTON_L]      = 0x00;
   joypad1Buttons[BUTTON_R]      = 0x00;
   joypad1Buttons[BUTTON_A]      = 0x00;
   joypad1Buttons[BUTTON_B]      = 0x00;
   joypad1Buttons[BUTTON_C]      = 0x00;
   joypad1Buttons[BUTTON_PAUSE]  = 0x00;
   joypad1Buttons[BUTTON_OPTION] = 0x00;
   joypad1Buttons[BUTTON_0]      = 0x00;
   joypad1Buttons[BUTTON_1]      = 0x00;
   joypad1Buttons[BUTTON_2]      = 0x00;
   joypad1Buttons[BUTTON_3]      = 0x00;
   joypad1Buttons[BUTTON_4]      = 0x00;
   joypad1Buttons[BUTTON_5]      = 0x00;
   joypad1Buttons[BUTTON_6]      = 0x00;
   joypad1Buttons[BUTTON_7]      = 0x00;
   joypad1Buttons[BUTTON_8]      = 0x00;
   joypad1Buttons[BUTTON_9]      = 0x00;
   joypad1Buttons[BUTTON_s]      = 0x00;
   joypad1Buttons[BUTTON_d]      = 0x00;

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
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_7)? 1 : 0))
      joypad0Buttons[BUTTON_7] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_8)? 1 : 0))
      joypad0Buttons[BUTTON_8] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_9)? 1 : 0))
      joypad0Buttons[BUTTON_9] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_MINUS)? 1 : 0))
	  joypad0Buttons[BUTTON_s] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_EQUALS)? 1 : 0))
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
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_u)? 1 : 0))
      joypad1Buttons[BUTTON_7] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_i)? 1 : 0))
      joypad1Buttons[BUTTON_8] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_o)? 1 : 0))
      joypad1Buttons[BUTTON_9] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LEFTBRACKET)? 1 : 0))
	  joypad1Buttons[BUTTON_s] = 0xff;
   if((input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RIGHTBRACKET)? 1 : 0))
	  joypad1Buttons[BUTTON_d] = 0xff;
}

static void extract_basename(char *buf, const char *path, size_t size)
{
   char       *ext  = NULL;
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');
   if (!base)
      base = path;

   if (*base == '\\' || *base == '/')
      base++;

   strncpy(buf, base, size - 1);
   buf[size - 1] = '\0';

   ext = strrchr(buf, '.');
   if (ext)
      *ext = '\0';
}

/************************************
 * libretro implementation
 ************************************/

static struct retro_system_av_info g_av_info;

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Virtual Jaguar";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "v2.1.0" GIT_VERSION;
   info->need_fullpath    = false;
   info->valid_extensions = "j64|jag";
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
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   return false;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

bool retro_load_game(const struct retro_game_info *info)
{
   char slash;
   unsigned i;
   const char *save_dir = NULL;
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

      { 0 },
   };

   if (!info)
      return false;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      //fprintf(stderr, "Pixel format XRGB8888 not supported by platform, cannot use.\n");
      return false;
   }

   videoWidth           = 320;
   videoHeight          = 240;
   videoBuffer  = (uint32_t *)calloc(sizeof(uint32_t), 1024 * 512);
   sampleBuffer = (uint16_t *)malloc(BUFMAX * sizeof(uint16_t)); //found in dac.h
   memset(sampleBuffer, 0, BUFMAX * sizeof(uint16_t));

   game_width           = 320;
   game_height          = 240;

   // Emulate BIOS
   vjs.hardwareTypeNTSC = true;
   vjs.useJaguarBIOS    = false;

   check_variables();

   // Get eeprom path info
   // > Handle Windows nonsense...
#if defined(_WIN32)
   slash = '\\';
#else
   slash = '/';
#endif
   // > Get save path
   vjs.EEPROMPath[0] = '\0';
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
		if (strlen(save_dir) > 0)
		{
			sprintf(vjs.EEPROMPath, "%s%c", save_dir, slash);
		}
   }
   // > Get ROM name
   if (info->path != NULL)
   {
      extract_basename(vjs.romName, info->path, sizeof(vjs.romName));
   }
   else
   {
      vjs.romName[0] = '\0';
   }

   JaguarInit();                                             // set up hardware
   memcpy(jagMemSpace + 0xE00000,
         ((vjs.biosType == BT_K_SERIES) ? jaguarBootROM : jaguarBootROM2),
         0x20000); // Use the stock BIOS

   JaguarSetScreenPitch(videoWidth);
   JaguarSetScreenBuffer(videoBuffer);

   /* Init video */
   for (i = 0; i < videoWidth * videoHeight; ++i)
      videoBuffer[i] = 0xFF00FFFF;

   SET32(jaguarMainRAM, 0, 0x00200000);
   JaguarLoadFile((uint8_t*)info->data, info->size);
   JaguarReset();

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
   JaguarDone();
   if (videoBuffer)
      free(videoBuffer);
   videoBuffer = NULL;
   if (sampleBuffer)
      free(sampleBuffer); //found in dac.h
   sampleBuffer = NULL;
}

unsigned retro_get_region(void)
{
   return vjs.hardwareTypeNTSC ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned type)
{
   if(type == RETRO_MEMORY_SYSTEM_RAM)
      return jaguarMainRAM;
   else if (type == RETRO_MEMORY_SAVE_RAM)
      return eeprom_ram;
   else return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   if(type == RETRO_MEMORY_SYSTEM_RAM)
      return 0x200000;
   else if (type == RETRO_MEMORY_SAVE_RAM)
      return 128;
   else return 0;
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
}

void retro_reset(void)
{
   JaguarReset();
}

void retro_run(void)
{
   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   update_input();

   JaguarExecuteNew();
   SoundCallback(NULL, sampleBuffer, vjs.hardwareTypeNTSC==1?BUFNTSC:BUFPAL);

   // Resolution changed
   if ((tomWidth != videoWidth || tomHeight != videoHeight) && tomWidth > 0 && tomHeight > 0)
   {
      videoWidth = tomWidth, videoHeight = tomHeight;
      game_width = tomWidth, game_height = tomHeight;
      
      JaguarSetScreenPitch(game_width);

      retro_get_system_av_info(&g_av_info);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g_av_info);      
   }

   video_cb(videoBuffer, game_width, game_height, game_width << 2);
}
