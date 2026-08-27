//
// TOM Header file
//

#ifndef __TOM_H__
#define __TOM_H__

#include <boolean.h>

#include "vjag_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_MODE_16BPP_CRY	0
#define VIDEO_MODE_24BPP_RGB	1
#define VIDEO_MODE_16BPP_DIRECT 2
#define VIDEO_MODE_16BPP_RGB	3

// Virtual screen size stuff

// NB: This virtual width is for PWIDTH = 4
//#define VIRTUAL_SCREEN_WIDTH            320
//was:340, 330
#define VIRTUAL_SCREEN_WIDTH            326
#define VIRTUAL_SCREEN_HEIGHT_NTSC      240
#define VIRTUAL_SCREEN_HEIGHT_PAL       256

// 68000 Interrupt bit positions (enabled at $F000E0)

enum { IRQ_VIDEO = 0, IRQ_GPU, IRQ_OPFLAG, IRQ_TIMER, IRQ_DSP };

void TOMInit(void);
void TOMReset(void);
void TOMDone(void);

uint8_t TOMReadByte(uint32_t offset, uint32_t who);
uint16_t TOMReadWord(uint32_t offset, uint32_t who);
void TOMWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void TOMWriteWord(uint32_t offset, uint16_t data, uint32_t who);

void TOMExecHalfline(uint16_t halfline, bool render);
uint32_t TOMGetVideoModeWidth(void);
uint32_t TOMGetVideoModeHeight(void);
uint32_t TOMGetWrittenRowExtent(void);
uint8_t TOMGetVideoMode(void);
uint8_t * TOMGetRamPointer(void);
uint16_t TOMGetHDB(void);
uint16_t TOMGetVDB(void);
uint16_t TOMGetHC(void);
uint16_t TOMGetVP(void);
uint16_t TOMGetMEMCON1(void);
uint16_t TOMGetMEMCON2(void);

/* Texture-pack substitution for one presented pixel on the RGB16-direct
 * scanline paths (issue #528).  The single seam both the 1x and the Nx
 * RGB16 renderers use -- see the comment on the definition in tom.c. */
int TomLinePackRGB(int idx, uint16_t color, uint32_t *out);

int TOMIRQEnabled(int irq);
uint16_t TOMIRQControlReg(void);
void TOMSetIRQLatch(int irq, int enabled);
void TOMExecPIT(uint32_t cycles);
void TOMSetPendingJERRYInt(void);
void TOMSetPendingTimerInt(void);
void TOMSetPendingObjectInt(void);
void TOMSetPendingGPUInt(void);
void TOMSetPendingVideoInt(void);
void TOMResetPIT(void);

// Exported variables

extern uint32_t tomWidth;
extern uint32_t tomHeight;
extern uint8_t tomRam8[];
extern uint32_t tomTimerPrescaler;
extern uint32_t tomTimerDivider;
extern int32_t tomTimerCounter;

extern uint32_t screenPitch;
extern uint32_t * screenBuffer;

/* RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE presentation-skip.
 *
 * Nonzero when the frontend has hinted (RETRO_AV_ENABLE_VIDEO clear) that it
 * will discard this frame's video output -- set once per retro_run() in
 * libretro.c, read by TOMExecHalfline().  It gates ONLY the final
 * CRY/RGB16 -> host XRGB8888 store into screenBuffer (the
 * scanline_render[]/tom_render_scanline_hires call and the border-colour
 * fill), never the OP object-list dispatch or line-buffer writes that
 * precede it in the same halfline: those touch tomRam8, which is real
 * addressable Jaguar state (the OP line buffer at $F01800-$F01D9E, GPU
 * synchronisation via OBF) a title could observe.  screenBuffer is a
 * host-only presentation target no emulated processor can read back, so
 * skipping stores into it changes nothing the machine can see. Default 0
 * (render as normal), so a frontend that never calls the environment call
 * -- or one that does and reports the bit set -- behaves exactly as
 * before. */
extern int tomSkipVideoPresent;

#ifdef __cplusplus
}
#endif

#endif	// __TOM_H__
