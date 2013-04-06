#ifndef VirtualJaguar_SDL_h
#define VirtualJaguar_SDL_h

// SDL stub for now
// remove later

#include "types.h"

typedef struct{
    int freq;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void *userdata;
} SDL_AudioSpec;

#endif
