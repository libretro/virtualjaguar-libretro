/* jlink.c — byte-transport seam for the JERRY UART.
   Loopback: bytes sent come back on the receive queue, modeling a
   console whose UARTO is wired to its own UARTI. */
#include <string.h>
#include "jlink.h"
#include "state.h"

#define JLINK_RING_SIZE 256

static int jlinkMode = JLINK_MODE_DISABLED;
static uint8_t jlinkRing[JLINK_RING_SIZE];
static uint32_t jlinkHead = 0;   /* next byte to pop */
static uint32_t jlinkCount = 0;

int JLinkOpen(int mode)
{
   JLinkClose();
   if (mode == JLINK_MODE_LOOPBACK)
   {
      jlinkMode = mode;
      return 1;
   }
   return 0;
}

void JLinkClose(void)
{
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
   return jlinkMode != JLINK_MODE_DISABLED;
}

void JLinkSendByte(uint8_t b)
{
   uint32_t tail;
   if (jlinkMode != JLINK_MODE_LOOPBACK)
      return;
   if (jlinkCount >= JLINK_RING_SIZE)
      return;   /* full: drop newest */
   tail = (jlinkHead + jlinkCount) % JLINK_RING_SIZE;
   jlinkRing[tail] = b;
   jlinkCount++;
}

int JLinkRecvByte(uint8_t *b)
{
   if (jlinkCount == 0)
      return 0;
   *b = jlinkRing[jlinkHead];
   jlinkHead = (jlinkHead + 1) % JLINK_RING_SIZE;
   jlinkCount--;
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
