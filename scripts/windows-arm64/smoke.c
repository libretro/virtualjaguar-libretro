/* Native Windows libretro ABI and public-ROM smoke test. */
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
typedef void *HMODULE;
#define LoadLibraryA(path) dlopen(path, RTLD_NOW)
#define GetProcAddress(handle, name) dlsym(handle, name)
#define FreeLibrary(handle) dlclose(handle)
#define GetLastError() ((unsigned long)errno)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

static unsigned frames;
static bool environment(unsigned cmd, void *data)
{
   if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)
      return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
   if (cmd == RETRO_ENVIRONMENT_GET_CAN_DUPE) { *(bool *)data = true; return true; }
   if (cmd == RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE) { *(bool *)data = false; return true; }
   return false;
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{ if (data && w && h && pitch) frames++; }
static void audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t batch(const int16_t *data, size_t count) { (void)data; return count; }
static void poll(void) {}
static int16_t input(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }
#define LOAD(name, type) name = (type)GetProcAddress(dll, #name); if (!name) return 3
int main(int argc, char **argv)
{
   HMODULE dll;
   FILE *file;
   long size;
   void *rom;
   unsigned i;
   struct retro_game_info game;
   struct retro_system_info info;
   unsigned (*retro_api_version)(void);
   void (*retro_get_system_info)(struct retro_system_info *);
   void (*retro_set_environment)(retro_environment_t);
   void (*retro_set_video_refresh)(retro_video_refresh_t);
   void (*retro_set_audio_sample)(retro_audio_sample_t);
   void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*retro_set_input_poll)(retro_input_poll_t);
   void (*retro_set_input_state)(retro_input_state_t);
   void (*retro_init)(void);
   bool (*retro_load_game)(const struct retro_game_info *);
   void (*retro_run)(void);
   void (*retro_unload_game)(void);
   void (*retro_deinit)(void);
   if (argc != 3) return 2;
   dll = LoadLibraryA(argv[1]);
   if (!dll) { fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 3; }
   LOAD(retro_api_version, unsigned (*)(void));
   LOAD(retro_get_system_info, void (*)(struct retro_system_info *));
   LOAD(retro_set_environment, void (*)(retro_environment_t));
   LOAD(retro_set_video_refresh, void (*)(retro_video_refresh_t));
   LOAD(retro_set_audio_sample, void (*)(retro_audio_sample_t));
   LOAD(retro_set_audio_sample_batch, void (*)(retro_audio_sample_batch_t));
   LOAD(retro_set_input_poll, void (*)(retro_input_poll_t));
   LOAD(retro_set_input_state, void (*)(retro_input_state_t));
   LOAD(retro_init, void (*)(void));
   LOAD(retro_load_game, bool (*)(const struct retro_game_info *));
   LOAD(retro_run, void (*)(void));
   LOAD(retro_unload_game, void (*)(void));
   LOAD(retro_deinit, void (*)(void));
   if (retro_api_version() != RETRO_API_VERSION) return 4;
   memset(&info, 0, sizeof(info)); retro_get_system_info(&info);
   printf("Core: %s %s\n", info.library_name, info.library_version);
   file = fopen(argv[2], "rb"); if (!file) return 5;
   fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
   if (size <= 0) return 5;
   rom = malloc((size_t)size); if (!rom) return 5;
   if (fread(rom, 1, (size_t)size, file) != (size_t)size) return 5;
   fclose(file);
   retro_set_environment(environment); retro_set_video_refresh(video);
   retro_set_audio_sample(audio); retro_set_audio_sample_batch(batch);
   retro_set_input_poll(poll); retro_set_input_state(input);
   retro_init(); memset(&game, 0, sizeof(game));
   game.path = argv[2]; game.data = rom; game.size = (size_t)size;
   if (!retro_load_game(&game)) return 6;
   for (i = 0; i < 60; i++) retro_run();
   retro_unload_game(); retro_deinit(); FreeLibrary(dll); free(rom);
   printf("Rendered %u frames\n", frames);
   return frames ? 0 : 7;
}
