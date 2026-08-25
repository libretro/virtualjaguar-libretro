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

/* Wire-speedup negotiation (#552) -- a second, unrelated small protocol
   sharing this module's already-bound UDP socket/port rather than opening
   a socket of its own (no second Local Network permission prompt, no new
   port to firewall/forward).  Distinct magic AND a length far shorter
   than JLINK_DISC_PKT_LEN, so it can never be mistaken for a beacon
   packet by JLinkDiscDecode(), in either direction: a build that only
   knows the beacon format sees buf[0..3] != "VJAG" (or, if it ever did
   collide on magic, len < JLINK_DISC_PKT_LEN) and takes the existing
   "not a valid packet, ignore it" path -- the same path an out-of-spec
   or hostile packet already takes today.  Nothing negotiation-related
   ever reaches the emulated UART ring; that ring is fed exclusively by
   jlink.c's TCP/netpacket byte transports, never by this socket. */
#define JLINK_NEG_MAGIC_0      'V'
#define JLINK_NEG_MAGIC_1      'J'
#define JLINK_NEG_MAGIC_2      'N'
#define JLINK_NEG_MAGIC_3      'G'
#define JLINK_NEG_VERSION      1
#define JLINK_NEG_PKT_LEN      12  /* magic(4) + version(1) + reserved(3) + senderId(4) */

/* Pure codec, no sockets -- unit-testable like JLinkDiscEncode/Decode.
   The packet carries almost no payload -- "this is a #552 auto-negotiate
   hello/ack" -- because there is exactly one non-stock divisor to agree
   on (UART_WIRE_SPEEDUP_MAX), so the wire only needs to prove the sender
   exists and understands the protocol -- see uart.h and jlink.c.

   senderId is the one exception, and it is NOT part of the protocol
   semantics -- it exists purely so a receiver can tell "this is my own
   packet, looped back to me" from "this is a real peer".  Two cores on
   ONE machine sharing the discovery port via SO_REUSEPORT is a normal,
   documented topology here (jlink_discover.c's own JLinkDiscStart()
   comment); on that topology a socket can receive its OWN outbound
   datagram back, and without a self-check a lone core would "confirm"
   wire-speedup negotiation against itself.  The beacon protocol already
   solved the analogous problem for itself (JLinkDiscPoll()'s self-beacon
   filter, matched on name+port); this is the same fix for negotiation,
   just cheaper -- a random per-process token instead of a hostname
   comparison. Random, not sequential: it exists to be unpredictable
   enough that two independent processes essentially never collide, not
   to identify a peer or survive a restart. */
size_t JLinkNegEncode(uint8_t *buf, size_t cap, uint32_t senderId);
int    JLinkNegDecode(const uint8_t *buf, size_t len, uint32_t *senderId);

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

/* Hook for the #552 negotiation protocol (or any future second protocol)
   to ride this module's socket without jlink_discover.c knowing anything
   about its semantics.  JLinkDiscPoll() calls the registered function for
   every datagram JLinkDiscDecode() does NOT recognize as a beacon --
   packets that were silently dropped before this existed, so registering
   a handler cannot regress beacon behavior.  from_addr is a NUL-terminated
   dotted-quad, valid only for the duration of the call. */
typedef void (*JLinkDiscRawFn)(const uint8_t *buf, size_t len,
                               const char *from_addr, int from_port);
void JLinkDiscSetRawHandler(JLinkDiscRawFn fn);

/* Unicast send on the same socket, independent of the beacon/listen role
   (listen-only clients may still send negotiation packets).  to_addr may
   be a dotted-quad or a hostname (resolved via getaddrinfo, like
   jlink_tcp.c's client connect).  Returns 1 on a successful send, 0 if
   the discovery socket is not open or the address failed to resolve. */
int  JLinkDiscSendTo(const uint8_t *buf, size_t len,
                     const char *to_addr, int to_port);

/* Effective discovery port -- JLINK_DISC_PORT unless VJ_DISC_PORT
   overrides it (tests only).  jlink.c's negotiation hello/ack targets
   this, not the raw JLINK_DISC_PORT macro, so it always dials whatever
   port this module actually bound. */
int  JLinkDiscPort(void);

#endif
