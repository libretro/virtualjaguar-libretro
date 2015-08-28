#ifndef __JAGDASM__
#define __JAGDASM__

#ifdef __cplusplus
extern "C" {
#endif

#define JAGUAR_GPU 0
#define JAGUAR_DSP 1

unsigned dasmjag(int dsp_type, char * buffer, unsigned pc);

#ifdef __cplusplus
}
#endif

#endif
