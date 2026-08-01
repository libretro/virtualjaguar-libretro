/* jlink_netpacket.h — libretro netpacket transport for the JagLink seam.
 *
 * The frontend (RetroArch >= 1.16) drives this: libretro.c registers a
 * retro_netpacket_callback whose members call into here.  When a netplay
 * session starts, this backend takes over the link (saving whatever mode
 * the core option had configured); when it stops, the previous mode is
 * restored.  Multi-peer is native: TX batches are broadcast, so CatNet
 * titles work over netplay too.
 */
#ifndef __JLINK_NETPACKET_H__
#define __JLINK_NETPACKET_H__

#include <stdint.h>
#include <stddef.h>
#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
