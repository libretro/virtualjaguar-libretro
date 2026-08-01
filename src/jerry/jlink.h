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
   JLINK_MODE_DISABLED   = 0,
   JLINK_MODE_LOOPBACK   = 1,
   JLINK_MODE_TCP_SERVER = 2,
   JLINK_MODE_TCP_CLIENT = 3,
   JLINK_MODE_NETPACKET  = 4   /* frontend netplay session (env 78) */
};

/* Push transport-received bytes into the RX ring (netpacket backend). */
void JLinkNPDeliver(const uint8_t *buf, size_t len);

/* TCP endpoint config; call before JLinkOpen for the TCP modes.
   host is ignored in server mode (listens on INADDR_ANY). */
void JLinkSetTCPEndpoint(const char *host, int port);
const char *JLinkGetTCPHost(void);

int  JLinkOpen(int mode);
void JLinkClose(void);
int  JLinkMode(void);
int  JLinkConnected(void);
void JLinkSendByte(uint8_t b);
int  JLinkRecvByte(uint8_t *b);
int  JLinkRxPending(void);
/* Per-frame service: progress TCP connect/accept, drain socket into the
   RX ring (bounded by ring space), flush pending TX.  No-op for
   loopback/disabled. */
void JLinkPoll(void);
/* Mid-frame pump for latency-sensitive polling paths: fetch any bytes
   already available from the transport RIGHT NOW (netpacket: ask the
   frontend to deliver pending packets; TCP: drain the socket).  Called
   by the UART when a game polls for link data; callers rate-limit. */
void JLinkPump(void);
/* Lifetime traffic counters (diagnostics; reset by JLinkClose). */
uint32_t JLinkTxTotal(void);
uint32_t JLinkRxTotal(void);

size_t JLinkStateSave(uint8_t *buf);
size_t JLinkStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif
