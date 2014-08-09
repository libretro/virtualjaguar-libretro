#ifndef __SDL_H__
#define __SDL_H__

// SDL stubs

#define AUDIO_S16SYS 0

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

//inline int SDL_OpenAudio(SDL_AudioSpec*, SDL_AudioSpec*);
//inline void SDL_PauseAudio(int number);
//inline void SDL_CloseAudio(void);

//int SDL_OpenAudio(SDL_AudioSpec*, SDL_AudioSpec*) {return 1;}
//void SDL_PauseAudio(int thenumber) {}
//void SDL_CloseAudio() {}
#endif
