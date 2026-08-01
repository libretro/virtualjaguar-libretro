/* jlink.h — byte-transport seam for the JERRY UART (JagLink/CatBox link).
   Phase 1 backends: disabled, loopback.  TCP and libretro-netpacket
   backends arrive in later phases behind this same interface. */
#ifndef __JLINK_H__
#define __JLINK_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   JLINK_MODE_DISABLED = 0,
   JLINK_MODE_LOOPBACK = 1
};

int  JLinkOpen(int mode);
void JLinkClose(void);
int  JLinkMode(void);
int  JLinkConnected(void);
void JLinkSendByte(uint8_t b);
int  JLinkRecvByte(uint8_t *b);
int  JLinkRxPending(void);
size_t JLinkStateSave(uint8_t *buf);
size_t JLinkStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif
