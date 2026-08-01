/* jlink.c — byte-transport seam for the JERRY UART.
   Loopback: bytes sent come back on the receive queue, modeling a
   console whose UARTO is wired to its own UARTI. */
#include <string.h>
#include "jlink.h"
#include "jlink_tcp.h"
#include "jlink_netpacket.h"
#include "state.h"

#define JLINK_RING_SIZE 256

static int jlinkMode = JLINK_MODE_DISABLED;
static uint8_t jlinkRing[JLINK_RING_SIZE];
static uint32_t jlinkHead = 0;   /* next byte to pop */
static uint32_t jlinkCount = 0;

static char jlinkTCPHost[128] = "127.0.0.1";
static int jlinkTCPPort = 42171;

static uint32_t jlinkTxTotal = 0;
static uint32_t jlinkRxTotal = 0;

static void JLinkRingPush(uint8_t b)
{
   uint32_t tail;
   if (jlinkCount >= JLINK_RING_SIZE)
      return;   /* full: drop newest */
   tail = (jlinkHead + jlinkCount) % JLINK_RING_SIZE;
   jlinkRing[tail] = b;
   jlinkCount++;
}

void JLinkSetTCPEndpoint(const char *host, int port)
{
   if (host && host[0])
   {
      strncpy(jlinkTCPHost, host, sizeof(jlinkTCPHost) - 1);
      jlinkTCPHost[sizeof(jlinkTCPHost) - 1] = '\0';
   }
   if (port > 0 && port < 65536)
      jlinkTCPPort = port;
}

int JLinkOpen(int mode)
{
   JLinkClose();
   if (mode == JLINK_MODE_LOOPBACK)
   {
      jlinkMode = mode;
      return 1;
   }
   if (mode == JLINK_MODE_TCP_SERVER || mode == JLINK_MODE_TCP_CLIENT)
   {
      if (!JLinkTCPOpen(mode == JLINK_MODE_TCP_SERVER,
                        jlinkTCPHost, jlinkTCPPort))
         return 0;
      jlinkMode = mode;
      return 1;
   }
   if (mode == JLINK_MODE_NETPACKET)
   {
      jlinkMode = mode;
      return 1;
   }
   return 0;
}

void JLinkClose(void)
{
   JLinkTCPClose();
   jlinkMode = JLINK_MODE_DISABLED;
   jlinkHead = 0;
   jlinkCount = 0;
}

int JLinkMode(void)
{
   return jlinkMode;
}

int JLinkConnected(void)
{
   if (jlinkMode == JLINK_MODE_TCP_SERVER || jlinkMode == JLINK_MODE_TCP_CLIENT)
      return JLinkTCPConnected();
   if (jlinkMode == JLINK_MODE_NETPACKET)
      return JLinkNPActive();
   return jlinkMode != JLINK_MODE_DISABLED;
}

void JLinkNPDeliver(const uint8_t *buf, size_t len)
{
   size_t i;
   for (i = 0; i < len; i++)
      JLinkRingPush(buf[i]);
}

void JLinkSendByte(uint8_t b)
{
   if (jlinkMode == JLINK_MODE_DISABLED)
      return;
   jlinkTxTotal++;
   if (jlinkMode == JLINK_MODE_LOOPBACK)
      JLinkRingPush(b);
   else if (jlinkMode == JLINK_MODE_TCP_SERVER
            || jlinkMode == JLINK_MODE_TCP_CLIENT)
      JLinkTCPSend(b);
   else if (jlinkMode == JLINK_MODE_NETPACKET)
      JLinkNPQueueByte(b);
}

uint32_t JLinkTxTotal(void)
{
   return jlinkTxTotal;
}

uint32_t JLinkRxTotal(void)
{
   return jlinkRxTotal;
}

void JLinkPoll(void)
{
   if (jlinkMode == JLINK_MODE_NETPACKET)
   {
      /* Flush the per-frame TX batch; RX arrives via the frontend's
         receive callback into the ring. */
      JLinkNPFlush();
      return;
   }
   if (jlinkMode != JLINK_MODE_TCP_SERVER && jlinkMode != JLINK_MODE_TCP_CLIENT)
      return;
   JLinkTCPPoll();
   /* Drain socket bytes into the RX ring while space remains; the
      kernel socket buffer holds anything beyond that (backpressure). */
   while (jlinkCount < JLINK_RING_SIZE)
   {
      uint8_t b;
      if (!JLinkTCPRecv(&b))
         break;
      JLinkRingPush(b);
   }
}

int JLinkRecvByte(uint8_t *b)
{
   if (jlinkCount == 0)
      return 0;
   *b = jlinkRing[jlinkHead];
   jlinkHead = (jlinkHead + 1) % JLINK_RING_SIZE;
   jlinkCount--;
   jlinkRxTotal++;
   return 1;
}

int JLinkRxPending(void)
{
   return (int)jlinkCount;
}

size_t JLinkStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   STATE_SAVE_VAR(buf, jlinkHead);
   STATE_SAVE_VAR(buf, jlinkCount);
   STATE_SAVE_BUF(buf, jlinkRing, JLINK_RING_SIZE);
   return (size_t)(buf - start);
}

size_t JLinkStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   STATE_LOAD_VAR(buf, jlinkHead);
   STATE_LOAD_VAR(buf, jlinkCount);
   STATE_LOAD_BUF(buf, jlinkRing, JLINK_RING_SIZE);
   if (jlinkHead >= JLINK_RING_SIZE)
      jlinkHead = 0;
   if (jlinkCount > JLINK_RING_SIZE)
      jlinkCount = JLINK_RING_SIZE;
   return (size_t)(buf - start);
}
