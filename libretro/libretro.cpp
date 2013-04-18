#include <iostream>
#include <cstring>
#include "libretro.h"
#import "jaguar.h"
#import "file.h"
#import "jagbios.h"
#include "memory.h"
#include "log.h"
#include "tom.h"
#include "dsp.h"
#include "settings.h"
#include "joystick.h"
#include "dac.h"

static bool failed_init;
int videoWidth, videoHeight;
double sampleRate;
uint32_t *videoBuffer = NULL;
signed short sound_buf[4096];
int game_width;
int game_height;

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_batch_cb;

void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

static void update_input()
{
    if (!input_poll_cb)
        return;
    
    input_poll_cb();
    
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) ? joypad_0_buttons[BUTTON_U] = 0xff : joypad_0_buttons[BUTTON_U] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN) ? joypad_0_buttons[BUTTON_D] = 0xff : joypad_0_buttons[BUTTON_D] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) ? joypad_0_buttons[BUTTON_L] = 0xff : joypad_0_buttons[BUTTON_L] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT) ? joypad_0_buttons[BUTTON_R] = 0xff : joypad_0_buttons[BUTTON_R] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) ? joypad_0_buttons[BUTTON_A] = 0xff : joypad_0_buttons[BUTTON_A] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) ? joypad_0_buttons[BUTTON_B] = 0xff : joypad_0_buttons[BUTTON_B] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y) ? joypad_0_buttons[BUTTON_C] = 0xff : joypad_0_buttons[BUTTON_C] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) ? joypad_0_buttons[BUTTON_PAUSE] = 0xff : joypad_0_buttons[BUTTON_PAUSE] = 0x00;
    input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT) ? joypad_0_buttons[BUTTON_OPTION] = 0xff : joypad_0_buttons[BUTTON_OPTION] = 0x00;
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
	info->valid_extensions = "j64|jag|JAG|J64";
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
    
    full_path = info->path;
    
	// Emulate BIOS
	vjs.GPUEnabled = true;
	vjs.audioEnabled = true;
	vjs.DSPEnabled = true;
	vjs.hardwareTypeNTSC = true;
	vjs.useJaguarBIOS = true;
	vjs.renderType = 0;
	
	//strcpy(vjs.EEPROMPath, "/path/to/eeproms/");   // battery saves
	JaguarInit();                                             // set up hardware
	memcpy(jagMemSpace + 0xE00000, jaguarBootROM, 0x20000);   // Use the stock BIOS
	
    JaguarSetScreenPitch(videoWidth);
    JaguarSetScreenBuffer(videoBuffer);
    //for (int i = 0; i < videoWidth * videoHeight; ++i)
    //    videoBuffer[i] = 0xFF00FFFF;
    
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
    videoWidth = 320;
    videoHeight = 240;
    videoBuffer = (uint32_t *)calloc(sizeof(uint32_t), 321 * 240);
    
	//game_width = TOMGetVideoModeWidth();
	//game_height = TOMGetVideoModeHeight();
    game_width = 320;
	game_height = 240;
    	
    unsigned level = 3;
    environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_deinit(void)
{
	JaguarDone();
	free(videoBuffer);
}

void retro_reset(void)
{
	JaguarReset();
}

void retro_run(void)
{
	update_input();
	
    //audio_size = SAMPLEFRAME;
    
    JaguarExecuteNew();
    
    // Virtual Jaguar outputs RGBA, so convert to XRGB8888
    for(int h=0;h<240;h++)
    {
        uint8_t *dst = (uint8_t*)videoBuffer + h*320*4;
        uint32_t *src = videoBuffer + h*320;
        
        for(int w=0;w<320;w++)
        {
            uint32_t pixel = *src++;;
            *dst++ = (pixel >> 8) & 0xff;
            *dst++ = (pixel >> 16) & 0xff;
            *dst++ = (pixel >> 24) & 0xff;
            *dst++ = (pixel >> 0) & 0xff;
        }
    }
    
	video_cb(videoBuffer, game_width, game_height, game_width << 2);
    //audio_batch_cb(sound_buf, num_samples);
}
