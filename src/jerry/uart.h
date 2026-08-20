/* uart.h — JERRY asynchronous serial interface (ComLynx / JagLink UART). */
#ifndef __UART_H__
#define __UART_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void UARTInit(void);
void UARTReset(void);
void UARTDone(void);
void UARTSetLinkMode(int mode);   /* JLINK_MODE_* from jlink.h */
void UARTPoll(void);              /* per frame, after JLinkPoll */

/* Enhancement (issue #498, NOT authentic): divide the emulated character
   frame time on an active link so a lockstep pad exchange finishes inside
   one video frame.  1 = stock hardware timing (the default); only values
   > 1 change anything, and only while a link transport is selected.
   Clamped to [1, UART_WIRE_SPEEDUP_MAX] -- keep the max in step with the
   virtualjaguar_netlink_speed option's value list. */
#define UART_WIRE_SPEEDUP_MAX 4u
void     UARTSetWireSpeedup(unsigned divisor);
unsigned UARTWireSpeedup(void);

uint16_t UARTReadWord(uint32_t offset);
void UARTWriteWord(uint32_t offset, uint16_t data);

/* Event-system callbacks (registered in src/core/event.c). */
void UARTTXCallback(void);
void UARTRXCallback(void);

size_t UARTStateSave(uint8_t *buf);
size_t UARTStateLoad(const uint8_t *buf, uint32_t stateVersion);

#ifdef __cplusplus
}
#endif

#endif
