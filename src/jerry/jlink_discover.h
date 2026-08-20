/* jlink_discover.h -- LAN discovery beacon for the Jaguar network link.
   Split in two halves on purpose: the codec + peer table below are pure
   logic with no sockets, so every awkward case (truncated packet, wrong
   magic, expiry, capacity) is unit-testable without a network.  The
   socket layer lives in the same .c file behind JLINK_DISC_HAVE_NET. */
#ifndef __JLINK_DISCOVER_H__
#define __JLINK_DISCOVER_H__

#include <stdint.h>
#include <stddef.h>

/* Outside the virtualjaguar_netlink_port option range (42171-42174) so a
   discovery socket can never collide with a link socket. */
#define JLINK_DISC_PORT        42170
#define JLINK_DISC_VERSION     1
#define JLINK_DISC_PKT_LEN     40
#define JLINK_DISC_NAME_MAX    32   /* 31 chars + NUL */
#define JLINK_DISC_ADDR_MAX    46   /* INET6_ADDRSTRLEN */
#define JLINK_DISC_MAX_PEERS   8
#define JLINK_DISC_EXPIRE_MS   10000

#define JLINK_DISC_DEV_JAGLINK    0
#define JLINK_DISC_DEV_VOICEMODEM 1

typedef struct
{
   char     name[JLINK_DISC_NAME_MAX];
   char     addr[JLINK_DISC_ADDR_MAX];
   int      device;
   int      port;
   uint32_t last_seen_ms;
} JLinkPeer;

size_t JLinkDiscEncode(uint8_t *buf, size_t cap, int device, int port,
                       const char *name);
int    JLinkDiscDecode(const uint8_t *buf, size_t len, int *device,
                       int *port, char *name, size_t name_cap);

void   JLinkDiscPeersReset(void);
int    JLinkDiscPeerSeen(const char *addr, const char *name, int device,
                         int port, uint32_t now_ms);
int    JLinkDiscPeerExpire(uint32_t now_ms);
int    JLinkDiscPeerCount(void);
const JLinkPeer *JLinkDiscPeerAt(int i);

/* Socket layer.  listen_only = 1 for client/auto (listen but never
   beacon); 0 for a host (beacon AND listen, so a host still sees peers).
   link_port is the TCP port advertised in the beacon. */
int  JLinkDiscStart(int listen_only, int device, int link_port);
void JLinkDiscStop(void);
int  JLinkDiscPoll(uint32_t now_ms);
int  JLinkDiscActive(void);

#endif
