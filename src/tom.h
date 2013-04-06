//
// TOM Header file
//

#ifndef __TOM_H__
#define __TOM_H__

#include "memory.h"

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

uint8 TOMReadByte(uint32 offset, uint32 who = UNKNOWN);
uint16 TOMReadWord(uint32 offset, uint32 who = UNKNOWN);
void TOMWriteByte(uint32 offset, uint8 data, uint32 who = UNKNOWN);
void TOMWriteWord(uint32 offset, uint16 data, uint32 who = UNKNOWN);

void TOMExecHalfline(uint16 halfline, bool render);
uint32 TOMGetVideoModeWidth(void);
uint32 TOMGetVideoModeHeight(void);
uint8 TOMGetVideoMode(void);
uint8 * TOMGetRamPointer(void);
uint16 TOMGetHDB(void);
uint16 TOMGetVDB(void);

int TOMIRQEnabled(int irq);
uint16 TOMIRQControlReg(void);
void TOMSetIRQLatch(int irq, int enabled);
void TOMExecPIT(uint32 cycles);
void TOMSetPendingJERRYInt(void);
void TOMSetPendingTimerInt(void);
void TOMSetPendingObjectInt(void);
void TOMSetPendingGPUInt(void);
void TOMSetPendingVideoInt(void);
void TOMResetPIT(void);

// Exported variables

extern uint32 tomWidth;
extern uint32 tomHeight;
extern uint8 tomRam8[];
extern uint32 tomTimerPrescaler;
extern uint32 tomTimerDivider;
extern int32 tomTimerCounter;

extern uint32 screenPitch;
extern uint32 * screenBuffer;

#endif	// __TOM_H__
