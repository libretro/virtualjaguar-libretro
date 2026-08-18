/* jlink_discover.c -- LAN discovery beacon.  See jlink_discover.h. */
#include "jlink_discover.h"
#include <string.h>
#include <stdio.h>

static JLinkPeer discPeers[JLINK_DISC_MAX_PEERS];
static int       discPeerCount = 0;

size_t JLinkDiscEncode(uint8_t *buf, size_t cap, int device, int port,
                       const char *name)
{
   size_t n;

   if (!buf || cap < JLINK_DISC_PKT_LEN)
      return 0;

   memset(buf, 0, JLINK_DISC_PKT_LEN);
   buf[0] = 'V'; buf[1] = 'J'; buf[2] = 'A'; buf[3] = 'G';
   buf[4] = (uint8_t)JLINK_DISC_VERSION;
   buf[5] = (uint8_t)(device ? JLINK_DISC_DEV_VOICEMODEM
                             : JLINK_DISC_DEV_JAGLINK);
   buf[6] = (uint8_t)((port >> 8) & 0xFF);
   buf[7] = (uint8_t)(port & 0xFF);

   if (name)
   {
      n = strlen(name);
      if (n > JLINK_DISC_NAME_MAX - 1)
         n = JLINK_DISC_NAME_MAX - 1;
      memcpy(buf + 8, name, n);
   }
   return JLINK_DISC_PKT_LEN;
}

int JLinkDiscDecode(const uint8_t *buf, size_t len, int *device,
                    int *port, char *name, size_t name_cap)
{
   size_t copy;

   if (!buf || len < JLINK_DISC_PKT_LEN)
      return 0;
   if (buf[0] != 'V' || buf[1] != 'J' || buf[2] != 'A' || buf[3] != 'G')
      return 0;
   if (buf[4] != JLINK_DISC_VERSION)
      return 0;

   if (device)
      *device = (buf[5] == JLINK_DISC_DEV_VOICEMODEM)
                ? JLINK_DISC_DEV_VOICEMODEM : JLINK_DISC_DEV_JAGLINK;
   if (port)
      *port = ((int)buf[6] << 8) | (int)buf[7];

   if (name && name_cap > 0)
   {
      /* The sender NUL-pads, but a hostile or corrupt packet need not:
         copy a bounded span and terminate ourselves, never strncpy from
         a field that may have no NUL in it. */
      copy = name_cap - 1;
      if (copy > JLINK_DISC_NAME_MAX - 1)
         copy = JLINK_DISC_NAME_MAX - 1;
      memcpy(name, buf + 8, copy);
      name[copy] = '\0';
   }
   return 1;
}

void JLinkDiscPeersReset(void)
{
   memset(discPeers, 0, sizeof(discPeers));
   discPeerCount = 0;
}

int JLinkDiscPeerSeen(const char *addr, const char *name, int device,
                      int port, uint32_t now_ms)
{
   int i;

   if (!addr || !addr[0])
      return 0;

   for (i = 0; i < discPeerCount; i++)
   {
      if (strcmp(discPeers[i].addr, addr) == 0
          && discPeers[i].port == port)
      {
         discPeers[i].last_seen_ms = now_ms;
         discPeers[i].device       = device;
         return 0;   /* known peer refreshed -- option list unchanged */
      }
   }

   if (discPeerCount >= JLINK_DISC_MAX_PEERS)
      return 0;

   memset(&discPeers[discPeerCount], 0, sizeof(JLinkPeer));
   strncpy(discPeers[discPeerCount].addr, addr, JLINK_DISC_ADDR_MAX - 1);
   if (name)
      strncpy(discPeers[discPeerCount].name, name, JLINK_DISC_NAME_MAX - 1);
   discPeers[discPeerCount].device       = device;
   discPeers[discPeerCount].port         = port;
   discPeers[discPeerCount].last_seen_ms = now_ms;
   discPeerCount++;
   return 1;
}

int JLinkDiscPeerExpire(uint32_t now_ms)
{
   int i = 0, changed = 0;

   while (i < discPeerCount)
   {
      /* Unsigned subtraction so a wrapped millisecond clock cannot make
         a fresh peer look ancient. */
      if ((uint32_t)(now_ms - discPeers[i].last_seen_ms)
          >= JLINK_DISC_EXPIRE_MS)
      {
         if (i < discPeerCount - 1)
            memmove(&discPeers[i], &discPeers[i + 1],
                    sizeof(JLinkPeer) * (size_t)(discPeerCount - i - 1));
         discPeerCount--;
         memset(&discPeers[discPeerCount], 0, sizeof(JLinkPeer));
         changed = 1;
         continue;
      }
      i++;
   }
   return changed;
}

int JLinkDiscPeerCount(void)
{
   return discPeerCount;
}

const JLinkPeer *JLinkDiscPeerAt(int i)
{
   if (i < 0 || i >= discPeerCount)
      return NULL;
   return &discPeers[i];
}
