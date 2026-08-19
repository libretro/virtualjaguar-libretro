/* jlink_discover.c -- LAN discovery beacon.  See jlink_discover.h. */
#include "jlink_discover.h"
#include <stdlib.h>   /* getenv, atoi */
#include <string.h>

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

/* Guard mirrors jlink_tcp.c verbatim: discovery has exactly the same
   platform surface as the TCP transport, and divergent guards are how one
   builds and the other does not. */
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__ANDROID__)
#define JLINK_DISC_HAVE_NET 1
#endif

#ifdef JLINK_DISC_HAVE_NET

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

static int      discSock       = -1;
static int      discListenOnly = 1;
static int      discDevice     = 0;
static int      discLinkPort   = 0;
static uint32_t discLastBeacon = 0;
static char     discSelfName[JLINK_DISC_NAME_MAX];

/* Effective discovery port.  Defaults to the protocol's fixed rendezvous
   port; VJ_DISC_PORT overrides it for tests only.
   42170 sits inside Linux's default ephemeral range (32768-60999), and the
   listener sets SO_REUSEPORT so two cores on one machine can share it.  That
   combination means two concurrent `make test` runs do NOT collide with a
   clean EADDRINUSE -- the kernel load-balances datagrams between them and one
   run's beacon is silently consumed by the other run's listener.  The repo hit
   the same class before with a fixed 42171 (see netlink_pair_test.sh) and
   settled on PID-spread ports below 32768; this override is how the discovery
   tests do the same without moving the shipped protocol port. */
static int JLinkDiscPort(void)
{
   const char *e;
   int p;

   e = getenv("VJ_DISC_PORT");
   if (e && e[0])
   {
      p = atoi(e);
      if (p > 0 && p < 65536)
         return p;
   }
   return JLINK_DISC_PORT;
}

int JLinkDiscStart(int listen_only, int device, int link_port)
{
   struct sockaddr_in sa;
   int one = 1;

   /* Idempotent when nothing changed.  libretro.c re-applies the netlink
      option (and this call along with it) on EVERY frontend
      variable-update flag, not just when netlink's own key changed --
      that flag is a single dirty bit shared by all options.  Without this
      guard, tweaking an unrelated option would silently wipe the
      discovered peer table a host picker reads from. */
   if (discSock >= 0 && discListenOnly == listen_only
       && discDevice == device && discLinkPort == link_port)
      return 1;

   JLinkDiscStop();
   JLinkDiscPeersReset();

   discSock = (int)socket(AF_INET, SOCK_DGRAM, 0);
   if (discSock < 0)
      return 0;

   /* Two cores on ONE machine is the normal dev/test layout (see
      netlink_pair_test.sh, uv_modem_game_test.sh).  Without these the
      second instance cannot bind and discovery silently does nothing on
      exactly the setup used to test it. */
   setsockopt(discSock, SOL_SOCKET, SO_REUSEADDR,
              (const char *)&one, sizeof(one));
#ifdef SO_REUSEPORT
   setsockopt(discSock, SOL_SOCKET, SO_REUSEPORT,
              (const char *)&one, sizeof(one));
#endif
   setsockopt(discSock, SOL_SOCKET, SO_BROADCAST,
              (const char *)&one, sizeof(one));

   memset(&sa, 0, sizeof(sa));
   sa.sin_family      = AF_INET;
   sa.sin_addr.s_addr = htonl(INADDR_ANY);
   sa.sin_port        = htons((unsigned short)JLinkDiscPort());
   if (bind(discSock, (struct sockaddr *)&sa, sizeof(sa)) != 0)
   {
      JLinkDiscStop();
      return 0;
   }

#ifdef _WIN32
   { u_long nb = 1; ioctlsocket(discSock, FIONBIO, &nb); }
#else
   fcntl(discSock, F_SETFL, fcntl(discSock, F_GETFL, 0) | O_NONBLOCK);
#endif

   discListenOnly = listen_only;
   discDevice     = device;
   discLinkPort   = link_port;
   discLastBeacon = 0;

   discSelfName[0] = '\0';
#ifdef _WIN32
   { DWORD n = JLINK_DISC_NAME_MAX - 1; GetComputerNameA(discSelfName, &n); }
#else
   if (gethostname(discSelfName, JLINK_DISC_NAME_MAX - 1) != 0)
      discSelfName[0] = '\0';
   discSelfName[JLINK_DISC_NAME_MAX - 1] = '\0';
#endif
   if (!discSelfName[0])
      strcpy(discSelfName, "jaguar");
   return 1;
}

void JLinkDiscStop(void)
{
   if (discSock >= 0)
   {
#ifdef _WIN32
      closesocket(discSock);
#else
      close(discSock);
#endif
   }
   discSock = -1;
}

int JLinkDiscActive(void)
{
   return discSock >= 0;
}

int JLinkDiscPoll(uint32_t now_ms)
{
   uint8_t  pkt[JLINK_DISC_PKT_LEN];
   struct sockaddr_in from;
   char     addr[JLINK_DISC_ADDR_MAX];
   char     name[JLINK_DISC_NAME_MAX];
   int      dev, port, changed = 0;
   socklen_t flen;
   int      n;

   if (discSock < 0)
      return 0;

   if (!discListenOnly
       && (discLastBeacon == 0 || (uint32_t)(now_ms - discLastBeacon) >= 1000))
   {
      struct sockaddr_in to;
      uint8_t out[JLINK_DISC_PKT_LEN];
      memset(&to, 0, sizeof(to));
      to.sin_family      = AF_INET;
      to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
      to.sin_port        = htons((unsigned short)JLinkDiscPort());
      if (JLinkDiscEncode(out, sizeof(out), discDevice, discLinkPort,
                          discSelfName))
         sendto(discSock, (const char *)out, JLINK_DISC_PKT_LEN, 0,
                (struct sockaddr *)&to, sizeof(to));
      discLastBeacon = now_ms;
   }

   for (;;)
   {
      flen = sizeof(from);
      memset(&from, 0, sizeof(from));
      n = (int)recvfrom(discSock, (char *)pkt, sizeof(pkt), 0,
                        (struct sockaddr *)&from, &flen);
      if (n <= 0)
         break;
      if (!JLinkDiscDecode(pkt, (size_t)n, &dev, &port, name, sizeof(name)))
         continue;
      /* Ignore our own beacon.  Matched on name+port, not source IP:
         the same machine appears under different addresses depending on
         which interface the broadcast came back through.
         Caveat: this is a false-positive risk, not just a false-negative
         fix. Two distinct hosts on the LAN that happen to share a
         hostname (common for cloned VM images or default hostnames like
         "raspberrypi") AND the default link port will filter each
         other's beacons out as "self", and neither will ever appear in
         the other's peer table. */
      if (!discListenOnly && port == discLinkPort
          && strcmp(name, discSelfName) == 0)
         continue;
      addr[0] = '\0';
      strncpy(addr, inet_ntoa(from.sin_addr), JLINK_DISC_ADDR_MAX - 1);
      addr[JLINK_DISC_ADDR_MAX - 1] = '\0';
      if (JLinkDiscPeerSeen(addr, name, dev, port, now_ms))
         changed = 1;
   }

   if (JLinkDiscPeerExpire(now_ms))
      changed = 1;
   return changed;
}

#else  /* no networking */

int  JLinkDiscStart(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
void JLinkDiscStop(void) {}
int  JLinkDiscPoll(uint32_t t) { (void)t; return 0; }
int  JLinkDiscActive(void) { return 0; }

#endif
