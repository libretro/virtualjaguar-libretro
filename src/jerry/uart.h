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

/* Enhancement (issue #498/#552, NOT authentic): divide the emulated
   character frame time on an active link so a lockstep pad exchange
   finishes inside one video frame.  1 = stock hardware timing; only a
   value > 1 changes anything, and only while a link transport is
   selected.  UART_WIRE_SPEEDUP_MAX is now the single non-stock divisor
   "auto" can ever produce -- #552 replaced the old 2x/4x value list with
   just disabled/auto, so there is no longer a magnitude for the option
   layer to pick; the two live JLink instances agree only on whether to
   use this one compile-time constant at all (see jlink.c's negotiation
   state machine).  Keep this in step with UART_WIRE_SPEEDUP_MAX if it
   is ever raised.

   Two views of the value, split because they change on different
   triggers and one of them is machine-visible state (see #552):

     Intent  -- config-derived (virtualjaguar_netlink_speed == "auto"),
                reapplied every check_variables() regardless of link
                state, and deliberately OUTSIDE the savestate -- same
                as NTSC/PAL, this is a runtime setting, not part of the
                emulated machine.
     Effective -- what UARTFrameUsec() actually uses right now.  Only
                becomes > 1 after jlink.c's out-of-band negotiation
                confirms the peer is ALSO in "auto"; falls back to 1
                the instant the link drops, the option is turned off,
                or a fresh connection has not yet negotiated.  This DOES
                change UARTFrameUsec()'s scheduling of the UART TX/RX
                callbacks, so unlike Intent it belongs in the savestate
                (STATE_VERSION_TEAMTAP / v13, extended in place) --
                otherwise a state saved mid-negotiation would reload
                running stock timing while the option still reads
                "auto", silently diverging from a peer that kept the
                negotiated rate. */
#define UART_WIRE_SPEEDUP_MAX 4u
void     UARTSetWireSpeedupIntent(unsigned wantAuto);   /* config layer */
unsigned UARTWireSpeedupIntent(void);
void     UARTSetWireSpeedupEffective(unsigned divisor);  /* jlink.c + loader */
unsigned UARTWireSpeedup(void);                           /* effective value */

size_t UARTWireSpeedupStateSave(uint8_t *buf);
size_t UARTWireSpeedupStateLoad(const uint8_t *buf);

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
