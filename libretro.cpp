#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string.h>
#include "libretro.h"
#include "file.h"
#include "jagbios.h"
#include "jagbios2.h"
#include "jaguar.h"
#include "dac.h"
#include "dsp.h"
#include "joystick.h"
#include "log.h"
#include "settings.h"
#include "tom.h"

static bool failed_init;
int videoWidth, videoHeight;
uint32_t *videoBuffer = NULL;
int game_width;
int game_height;

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_batch_cb;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

int doom_res_hack=0; // Doom Hack to double pixel if pwidth==8 (163*2)

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      {
         "virtualjaguar_usefastblitter",
         "Fast Blitter; disabled|enabled",

      },
      {
         "virtualjaguar_doom_res_hack",
         "Doom Res Hack; disabled|enabled",

      },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
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
} 

static void update_input(void)
{
   if (!input_poll_cb)
      return;

   input_poll_cb();

   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) ? joypad0Buttons[BUTTON_U] = 0xff : joypad0Buttons[BUTTON_U] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN) ? joypad0Buttons[BUTTON_D] = 0xff : joypad0Buttons[BUTTON_D] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) ? joypad0Buttons[BUTTON_L] = 0xff : joypad0Buttons[BUTTON_L] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT) ? joypad0Buttons[BUTTON_R] = 0xff : joypad0Buttons[BUTTON_R] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) ? joypad0Buttons[BUTTON_A] = 0xff : joypad0Buttons[BUTTON_A] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) ? joypad0Buttons[BUTTON_B] = 0xff : joypad0Buttons[BUTTON_B] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y) ? joypad0Buttons[BUTTON_C] = 0xff : joypad0Buttons[BUTTON_C] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT) ? joypad0Buttons[BUTTON_PAUSE] = 0xff : joypad0Buttons[BUTTON_PAUSE] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) ? joypad0Buttons[BUTTON_OPTION] = 0xff : joypad0Buttons[BUTTON_OPTION] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) ? joypad0Buttons[BUTTON_0] = 0xff : joypad0Buttons[BUTTON_0] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L) ? joypad0Buttons[BUTTON_1] = 0xff : joypad0Buttons[BUTTON_1] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) ? joypad0Buttons[BUTTON_2] = 0xff : joypad0Buttons[BUTTON_2] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2) ? joypad0Buttons[BUTTON_3] = 0xff : joypad0Buttons[BUTTON_3] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2) ? joypad0Buttons[BUTTON_4] = 0xff : joypad0Buttons[BUTTON_4] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3) ? joypad0Buttons[BUTTON_5] = 0xff : joypad0Buttons[BUTTON_5] = 0x00;
   input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3) ? joypad0Buttons[BUTTON_6] = 0xff : joypad0Buttons[BUTTON_6] = 0x00;

}

/************************************
 * libretro implementation
 ************************************/

static struct retro_system_av_info g_av_info;

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "Virtual Jaguar";
   info->library_version = "v2.1.0";
   info->need_fullpath = true;
   info->valid_extensions = "j64|jag";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = 60;
   info->timing.sample_rate    = 48000;
   info->geometry.base_width   = game_width;
   info->geometry.base_height  = game_height;
   info->geometry.max_width    = 320;
   info->geometry.max_height   = 240;
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
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "Pixel format XRGB8888 not supported by platform, cannot use.\n");
      return false;
   }

   const char *full_path;

   if(failed_init)
      return false;

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

      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   full_path = info->path;

   // Emulate BIOS
   vjs.GPUEnabled = true;
   vjs.audioEnabled = true;
   vjs.DSPEnabled = true;
   vjs.hardwareTypeNTSC = true;
   vjs.useJaguarBIOS = false;
   vjs.renderType = 0;

   check_variables();

   //strcpy(vjs.EEPROMPath, "/path/to/eeproms/");   // battery saves
   JaguarInit();                                             // set up hardware
   memcpy(jagMemSpace + 0xE00000, (vjs.biosType == BT_K_SERIES ? jaguarBootROM : jaguarBootROM2), 0x20000); // Use the stock BIOS

   JaguarSetScreenPitch(videoWidth);
   JaguarSetScreenBuffer(videoBuffer);
   //Init video
   for (int i = 0; i < videoWidth * videoHeight; ++i)
      videoBuffer[i] = 0xFF00FFFF;

   SET32(jaguarMainRAM, 0, 0x00200000);                      // set up stack
   JaguarLoadFile((char *)full_path);                // load rom
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
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

void retro_init(void)
{
   unsigned level = 18;

   videoWidth = 320;
   videoHeight = 240;
   videoBuffer = (uint32_t *)calloc(sizeof(uint32_t), 1024 * 512);
   sampleBuffer = (uint16_t *)malloc(2048 * sizeof(uint16_t)); //found in dac.h
   memset(sampleBuffer, 0, 2048 * sizeof(uint16_t));

   //game_width = TOMGetVideoModeWidth();
   //game_height = TOMGetVideoModeHeight();
   game_width = 320;
   game_height = 240;

   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_deinit(void)
{
   JaguarDone();
   free(videoBuffer);
   free(sampleBuffer); //found in dac.h
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
   
   SDLSoundCallback(NULL, sampleBuffer, 1600);

   video_cb(videoBuffer, game_width, game_height, game_width << 2);
   audio_batch_cb((int16_t *)sampleBuffer, 1600/2);
}
