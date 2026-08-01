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
