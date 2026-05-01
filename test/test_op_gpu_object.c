/* test_op_gpu_object.c -- Object Processor GPU object IRQ behavior. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

#include "../libretro-common/include/libretro.h"

#ifdef __APPLE__
#define CORE_FILENAME "virtualjaguar_libretro.dylib"
#elif defined(_WIN32)
#define CORE_FILENAME "virtualjaguar_libretro.dll"
#else
#define CORE_FILENAME "virtualjaguar_libretro.so"
#endif

static void (*p_retro_init)(void);
static void (*p_retro_deinit)(void);
static void (*p_retro_set_environment)(retro_environment_t);
static void (*p_retro_set_video_refresh)(retro_video_refresh_t);
static void (*p_retro_set_audio_sample)(retro_audio_sample_t);
static void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_retro_set_input_poll)(retro_input_poll_t);
static void (*p_retro_set_input_state)(retro_input_state_t);
static bool (*p_retro_load_game)(const struct retro_game_info *);
static void (*p_retro_unload_game)(void);
static void (*p_OPProcessList)(int, bool);

static uint8_t **p_jaguarMainRAM;
static uint8_t *p_tomRam8;

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   (void)data;
   (void)width;
   (void)height;
   (void)pitch;
}

static void audio_sample(int16_t left, int16_t right)
{
   (void)left;
   (void)right;
}

static size_t audio_sample_batch(const int16_t *data, size_t frames)
{
   (void)data;
   return frames;
}

static void input_poll(void)
{
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port;
   (void)device;
   (void)index;
   (void)id;
   return 0;
}

static bool environment_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
      case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
         ((struct retro_variable *)data)->value = NULL;
         return false;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = false;
         return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "/tmp";
         return true;
      default:
         return false;
   }
}

static void *read_file(const char *path, size_t *out_size)
{
   FILE *f;
   long size;
   void *data;

   f = fopen(path, "rb");
   if (!f)
      return NULL;

   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);

   data = malloc((size_t)size);
   if (!data || fread(data, 1, (size_t)size, f) != (size_t)size)
   {
      free(data);
      fclose(f);
      return NULL;
   }

   fclose(f);
   *out_size = (size_t)size;
   return data;
}

static void write64(uint8_t *ram, uint32_t offset, uint64_t value)
{
   ram[offset + 0] = (uint8_t)(value >> 56);
   ram[offset + 1] = (uint8_t)(value >> 48);
   ram[offset + 2] = (uint8_t)(value >> 40);
   ram[offset + 3] = (uint8_t)(value >> 32);
   ram[offset + 4] = (uint8_t)(value >> 24);
   ram[offset + 5] = (uint8_t)(value >> 16);
   ram[offset + 6] = (uint8_t)(value >> 8);
   ram[offset + 7] = (uint8_t)value;
}

static uint64_t read64(const uint8_t *ram, uint32_t offset)
{
   return ((uint64_t)ram[offset + 0] << 56)
      | ((uint64_t)ram[offset + 1] << 48)
      | ((uint64_t)ram[offset + 2] << 40)
      | ((uint64_t)ram[offset + 3] << 32)
      | ((uint64_t)ram[offset + 4] << 24)
      | ((uint64_t)ram[offset + 5] << 16)
      | ((uint64_t)ram[offset + 6] << 8)
      | (uint64_t)ram[offset + 7];
}

static void set_olp(uint8_t *tom_ram, uint32_t address)
{
   tom_ram[0x20] = (uint8_t)(address >> 8);
   tom_ram[0x21] = (uint8_t)address;
   tom_ram[0x22] = (uint8_t)(address >> 24);
   tom_ram[0x23] = (uint8_t)(address >> 16);
}

int main(int argc, char **argv)
{
   const char *core_path;
   const char *rom_path;
   struct retro_game_info game;
   void *core;
   void *rom_data;
   size_t rom_size;
   uint8_t *ram;
   uint64_t gpu_object;
   uint64_t ob_object;

   core_path = (argc > 1) ? argv[1] : "./" CORE_FILENAME;
   rom_path = (argc > 2) ? argv[2] : "test/roms/jagniccc.j64";

   core = dlopen(core_path, RTLD_LAZY);
   if (!core)
   {
      fprintf(stderr, "dlopen failed: %s\n", dlerror());
      return 1;
   }

#define LOAD(name) do { \
   p_##name = dlsym(core, #name); \
   if (!p_##name) { fprintf(stderr, "Missing symbol: " #name "\n"); return 1; } \
} while (0)

   LOAD(retro_init);
   LOAD(retro_deinit);
   LOAD(retro_set_environment);
   LOAD(retro_set_video_refresh);
   LOAD(retro_set_audio_sample);
   LOAD(retro_set_audio_sample_batch);
   LOAD(retro_set_input_poll);
   LOAD(retro_set_input_state);
   LOAD(retro_load_game);
   LOAD(retro_unload_game);
   LOAD(OPProcessList);

   p_jaguarMainRAM = (uint8_t **)dlsym(core, "jaguarMainRAM");
   p_tomRam8 = (uint8_t *)dlsym(core, "tomRam8");
   if (!p_jaguarMainRAM || !p_tomRam8)
   {
      fprintf(stderr, "Missing RAM symbols\n");
      return 1;
   }

   rom_data = read_file(rom_path, &rom_size);
   if (!rom_data)
   {
      fprintf(stderr, "Could not read ROM: %s\n", rom_path);
      return 1;
   }

   memset(&game, 0, sizeof(game));
   game.data = rom_data;
   game.size = rom_size;
   game.path = rom_path;

   p_retro_set_environment(environment_cb);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_sample_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "retro_load_game failed\n");
      return 1;
   }

   ram = *p_jaguarMainRAM;
   gpu_object = 0x0000000000000002ULL;
   write64(ram, 0x1000, gpu_object);
   write64(ram, 0x1008, 0x0000000000000004ULL);
   set_olp(p_tomRam8, 0x1000);

   p_OPProcessList(0, false);
   ob_object = read64(p_tomRam8, 0x10);

   p_retro_unload_game();
   p_retro_deinit();
   free(rom_data);
   dlclose(core);

   if (ob_object != gpu_object)
   {
      fprintf(stderr, "FAIL: GPU object was overwritten in OB: %016llX\n",
            (unsigned long long)ob_object);
      return 1;
   }

   printf("PASS: GPU object remains in OB after IRQ\n");
   return 0;
}
