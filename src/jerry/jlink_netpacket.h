/* jlink_netpacket.h — libretro netpacket transport for the JagLink seam.
 *
 * The frontend (RetroArch >= 1.16) drives this: libretro.c registers a
 * retro_netpacket_callback whose members call into here.  When a netplay
 * session starts, this backend takes over the link (saving whatever mode
 * the core option had configured); when it stops, the previous mode is
 * restored.  Multi-peer is native: TX batches are broadcast, so CatNet
 * titles work over netplay too.
 *
 * Wire protocol vjag-netlink-2 (#585): every payload starts with a 1-byte
 * type (NP_UART / NP_VOICE / NP_VOICE_HELLO).  Voice is host-side only and
 * never enters the UART ring.
 */
#ifndef __JLINK_NETPACKET_H__
#define __JLINK_NETPACKET_H__

#include <stdint.h>
#include <stddef.h>
#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

/* vjag-netlink-2 packet type prefixes. */
#define JLINK_NP_TYPE_UART        0x01
#define JLINK_NP_TYPE_VOICE       0x02
#define JLINK_NP_TYPE_VOICE_HELLO 0x03

/* Voice-hello body after the type byte:
 *   'V','C', version, flags, senderId[4]
 * flags: bit0 = wants voice, bit1 = ack of a hello already received. */
#define JLINK_NP_HELLO_VERSION    1
#define JLINK_NP_HELLO_FLAG_WANT  0x01
#define JLINK_NP_HELLO_FLAG_ACK   0x02
#define JLINK_NP_HELLO_BODY_LEN   8   /* without the type byte */
#define JLINK_NP_HELLO_PKT_LEN    (1 + JLINK_NP_HELLO_BODY_LEN)

/* retro_netpacket_callback members (wired up in libretro.c). */
void JLinkNPStart(uint16_t client_id, retro_netpacket_send_t send_fn,
                  retro_netpacket_poll_receive_t poll_receive_fn);
void JLinkNPReceive(const void *buf, size_t len, uint16_t client_id);
void JLinkNPStop(void);
void JLinkNPPoll(void);

/* Internal API consumed by jlink.c's mode dispatch. */
int  JLinkNPActive(void);
void JLinkNPQueueByte(uint8_t b);
void JLinkNPFlush(void);
void JLinkNPPumpReceive(void);

/* Voice negotiate state, for the core's log lines. */
#define JLINK_NP_VOICE_OFF         0  /* no session, or voice not enabled */
#define JLINK_NP_VOICE_NEGOTIATING 1  /* hellos out, inside the fast window */
#define JLINK_NP_VOICE_READY       2  /* a peer confirmed; TX armed */
#define JLINK_NP_VOICE_DATA_ONLY   3  /* nobody confirmed yet; slow retry */

/* Voice overlay (#585): capability negotiate + unreliable voice TX. */
void JLinkNPSetVoiceWant(int want);
int  JLinkNPVoiceReady(void);
int  JLinkNPVoiceState(void);   /* JLINK_NP_VOICE_* */
void JLinkNPVoiceTick(void);
int  JLinkNPSendVoice(const uint8_t *pkt, size_t len);

#ifdef __cplusplus
}
#endif

#endif
