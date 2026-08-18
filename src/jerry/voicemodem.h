/* voicemodem.h — Jaguar Voice Modem (JVM) emulation for Ultra Vortek.
 *
 * A virtual modem state machine sitting between the JERRY UART and the
 * jlink byte transport (issue #481).  The console-facing protocol is the
 * one Ultra Vortek's own driver speaks (docs/voice-modem.md); "dial"
 * resolves to the existing netlink TCP/netpacket/loopback session.  No
 * voice, no DTMF audio, no real telephony — game data only.
 *
 * Consumed by jlink.c only; not a public core API.
 */
#ifndef __VOICEMODEM_H__
#define __VOICEMODEM_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full reset (power-on / retro_deinit / transport close). */
void VMReset(void);

/* Byte transmitted by the console over the UART. */
void VMConsoleTx(uint8_t b);

/* Modem->console byte stream (replies and async messages). */
int  VMConsoleRecv(uint8_t *b);
int  VMConsoleRxPending(void);

/* Byte received from the far modem over the jlink transport. */
void VMWireInput(uint8_t b);

/* UART finished a TX burst (shift register drained, nothing queued). */
void VMTxBurstEnd(void);

/* Once per video frame: ring-indicate pacing. */
void VMFrameTick(void);

/* Deliberately no savestate hooks: the whole modem session (call state,
 * in-flight replies) is host-side, exactly like jlink's sockets.  Loading
 * a state mid-call behaves as a pulled phone line; Ultra Vortek shows
 * LOST PHONE CONNECTION and both sides can redial.  See PR #481. */

#ifdef __cplusplus
}
#endif

#endif
