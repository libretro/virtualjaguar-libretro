/* jlink_tcp.c — nonblocking TCP endpoint for the JagLink transport.
 *
 * Server mode is a CatNet-style hub: up to JLINK_TCP_MAX_PEERS clients
 * share the wire.  Local TX goes to every peer; a byte received from
 * one peer is delivered locally AND forwarded to every other peer, so
 * all nodes hear all traffic — the electrical semantics of a shared
 * multi-drop bus.  Client mode is a single connection to the hub, whose
 * address may be an IP or a name (DNS or Bonjour ".local").
 *
 * POSIX and winsock share the code behind a thin macro layer;
 * platforms without BSD-style sockets (bare console targets) compile
 * the stub tail of this file instead.
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1   /* glibc: expose POSIX (getaddrinfo) under c99 */
#endif
#include <string.h>
#include "jlink_tcp.h"

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__ANDROID__)
#define JLINK_HAVE_TCP 1
#endif

#ifdef JLINK_HAVE_TCP

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0501)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501   /* getaddrinfo/freeaddrinfo live behind this */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET jlink_sock_t;
#define JLINK_INVALID_SOCK  INVALID_SOCKET
#define jlink_sock_valid(s) ((s) != INVALID_SOCKET)
#define jlink_closesock(s)  closesocket(s)
#define jlink_sockerr()     WSAGetLastError()
#define JLINK_EWOULDBLOCK   WSAEWOULDBLOCK
#define JLINK_EINPROGRESS   WSAEWOULDBLOCK
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>   /* select/fd_set: implicit via socket.h on BSD/macOS, NOT on glibc */
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>        /* getaddrinfo */
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int jlink_sock_t;
#define JLINK_INVALID_SOCK  (-1)
#define jlink_sock_valid(s) ((s) >= 0)
#define jlink_closesock(s)  close(s)
#define jlink_sockerr()     errno
#define JLINK_EWOULDBLOCK   EWOULDBLOCK
#define JLINK_EINPROGRESS   EINPROGRESS
#endif

#define JLINK_TCP_MAX_PEERS   7
#define JLINK_TCP_TXPEND_SIZE 1024

typedef struct
{
   jlink_sock_t sock;
   uint8_t txPend[JLINK_TCP_TXPEND_SIZE];
   uint32_t txHead;
   uint32_t txCount;
} jlink_peer_t;

static jlink_sock_t tcpListen = JLINK_INVALID_SOCK;
static jlink_peer_t tcpPeers[JLINK_TCP_MAX_PEERS];
static int tcpPeersInit = 0;     /* one-time array init guard */
static int tcpIsServer = 0;
static int tcpIsClient = 0;
static int tcpConnecting = 0;    /* client connect in flight (slot 0) */
static int tcpRetryTimer = 0;    /* polls until next client reconnect */
static char tcpHost[128] = "127.0.0.1";
static int tcpPort = 0;
static struct sockaddr_in tcpAddr;   /* tcpHost resolved (client mode) */
static int tcpAddrValid = 0;

/* A refused/dropped client connect retries after this many polls
   (one poll per frame => roughly half a second). */
#define JLINK_TCP_RETRY_POLLS 30

/* A *failed name lookup* backs off further than a refused connect: a
   refusal costs one nonblocking syscall, an unresolvable name can cost
   the resolver's full timeout, and retrying that every half second
   would stutter the frame loop. */
#define JLINK_TCP_RESOLVE_RETRY_POLLS 300

static void jlink_peers_reset(void)
{
   int i;
   for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
   {
      tcpPeers[i].sock = JLINK_INVALID_SOCK;
      tcpPeers[i].txHead = 0;
      tcpPeers[i].txCount = 0;
   }
   tcpPeersInit = 1;
}

static void jlink_set_nonblock(jlink_sock_t s)
{
#ifdef _WIN32
   u_long one = 1;
   ioctlsocket(s, FIONBIO, &one);
#else
   int fl = fcntl(s, F_GETFL, 0);
   fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

static void jlink_set_nodelay(jlink_sock_t s)
{
   int one = 1;
   setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
}

#ifdef _WIN32
static int wsaStarted = 0;
static void jlink_wsa_init(void)
{
   WSADATA wd;
   if (!wsaStarted)
   {
      if (WSAStartup(MAKEWORD(2, 2), &wd) == 0)
         wsaStarted = 1;
   }
}
#endif

int JLinkTCPAvailable(void)
{
   return 1;
}

static void jlink_tcp_drop_peer(int idx)
{
   if (jlink_sock_valid(tcpPeers[idx].sock))
      jlink_closesock(tcpPeers[idx].sock);
   tcpPeers[idx].sock = JLINK_INVALID_SOCK;
   tcpPeers[idx].txHead = 0;
   tcpPeers[idx].txCount = 0;
   if (tcpIsClient && idx == 0)
   {
      tcpConnecting = 0;
      tcpRetryTimer = JLINK_TCP_RETRY_POLLS;
      /* Drop the cached address too, so the next attempt re-resolves.
         The whole point of naming the hub is that its address may
         change -- a new DHCP lease moves the ".local" name to a new IP,
         and a client that kept dialling the old one would never come
         back without a core reload.  Re-resolving is cheap where it
         matters: a dotted quad never leaves AI_NUMERICHOST, and a name
         whose owner is up answers from the OS resolver cache.  When the
         name does NOT resolve, the lookup path's own 5 s backoff paces
         the retries. */
      tcpAddrValid = 0;
   }
}

/* Resolve tcpHost into tcpAddr, once, and cache it.
 *
 * Dotted-quad addresses are matched first with AI_NUMERICHOST, which
 * cannot block, so the common 192.168.x.y path never reaches a
 * resolver.  Anything else is a name and gets a real lookup — which is
 * what makes "jaghub.local" (Bonjour/mDNS) and plain DNS names usable
 * as the hub address.  Only IPv4 results are taken: the listener binds
 * AF_INET, so an AAAA-only peer could not be talked to anyway.
 *
 * Returns 1 when tcpAddr is usable. */
static int jlink_tcp_resolve(void)
{
   struct addrinfo hints;
   struct addrinfo *res = NULL;
   struct addrinfo *ai;
   int rc;

   if (tcpAddrValid)
      return 1;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family   = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags    = AI_NUMERICHOST;

   rc = getaddrinfo(tcpHost, NULL, &hints, &res);
   if (rc != 0)
   {
      hints.ai_flags = 0;
      rc = getaddrinfo(tcpHost, NULL, &hints, &res);
   }
   if (rc != 0 || !res)
   {
      /* "localhost" is the one name whose meaning is fixed even when
         the resolver cannot answer (no /etc/hosts entry, no DNS on a
         locked-down box).  Same-machine play must not depend on that. */
      if (strcmp(tcpHost, "localhost") == 0)
      {
         memset(&tcpAddr, 0, sizeof(tcpAddr));
         tcpAddr.sin_family = AF_INET;
         tcpAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
         tcpAddrValid = 1;
         return 1;
      }
      return 0;
   }

   for (ai = res; ai; ai = ai->ai_next)
   {
      if (ai->ai_family == AF_INET
          && (size_t)ai->ai_addrlen >= sizeof(struct sockaddr_in))
      {
         memcpy(&tcpAddr, ai->ai_addr, sizeof(struct sockaddr_in));
         tcpAddrValid = 1;
         break;
      }
   }
   freeaddrinfo(res);
   return tcpAddrValid;
}

/* Kick off (or re-kick) a nonblocking client connect to tcpHost:tcpPort. */
static void jlink_tcp_start_connect(void)
{
   struct sockaddr_in sa;
   int rc;

   if (!jlink_tcp_resolve())
   {
      tcpRetryTimer = JLINK_TCP_RESOLVE_RETRY_POLLS;
      return;
   }

   sa = tcpAddr;
   sa.sin_family = AF_INET;
   sa.sin_port = htons((unsigned short)tcpPort);

   tcpPeers[0].sock = socket(AF_INET, SOCK_STREAM, 0);
   if (!jlink_sock_valid(tcpPeers[0].sock))
      return;
   jlink_set_nonblock(tcpPeers[0].sock);
   rc = connect(tcpPeers[0].sock, (struct sockaddr *)&sa, sizeof(sa));
   if (rc != 0)
   {
      int err = jlink_sockerr();
      if (err != JLINK_EINPROGRESS && err != JLINK_EWOULDBLOCK)
      {
         jlink_closesock(tcpPeers[0].sock);
         tcpPeers[0].sock = JLINK_INVALID_SOCK;
         tcpAddrValid = 0;        /* re-resolve; the host may have moved */
         tcpRetryTimer = JLINK_TCP_RETRY_POLLS;
         return;
      }
      tcpConnecting = 1;
   }
   else
      jlink_set_nodelay(tcpPeers[0].sock);
}

int JLinkTCPOpen(int is_server, const char *host, int port)
{
   struct sockaddr_in sa;

   JLinkTCPClose();
#ifdef _WIN32
   jlink_wsa_init();
#endif

   memset(&sa, 0, sizeof(sa));
   sa.sin_family = AF_INET;
   sa.sin_port = htons((unsigned short)port);

   if (is_server)
   {
      int one = 1;
      tcpListen = socket(AF_INET, SOCK_STREAM, 0);
      if (!jlink_sock_valid(tcpListen))
         return 0;
      setsockopt(tcpListen, SOL_SOCKET, SO_REUSEADDR,
                 (const char *)&one, sizeof(one));
      sa.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(tcpListen, (struct sockaddr *)&sa, sizeof(sa)) != 0
          || listen(tcpListen, JLINK_TCP_MAX_PEERS) != 0)
      {
         jlink_closesock(tcpListen);
         tcpListen = JLINK_INVALID_SOCK;
         return 0;
      }
      jlink_set_nonblock(tcpListen);
   }
   else
   {
      if (!host || !host[0])
         host = "127.0.0.1";
      strncpy(tcpHost, host, sizeof(tcpHost) - 1);
      tcpHost[sizeof(tcpHost) - 1] = '\0';
      tcpPort = port;
      tcpIsClient = 1;
      tcpAddrValid = 0;
      /* First attempt now; refusals AND failed lookups retry from
         JLinkTCPPoll.  Neither is fatal at open time: the peer instance
         may not be listening yet, and a ".local" name does not resolve
         until its owner is actually on the network. */
      jlink_tcp_start_connect();
   }

   tcpIsServer = is_server;
   return 1;
}

void JLinkTCPClose(void)
{
   int i;
   if (!tcpPeersInit)
      jlink_peers_reset();
   for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
   {
      if (jlink_sock_valid(tcpPeers[i].sock))
         jlink_closesock(tcpPeers[i].sock);
   }
   if (jlink_sock_valid(tcpListen))
      jlink_closesock(tcpListen);
   jlink_peers_reset();
   tcpListen = JLINK_INVALID_SOCK;
   tcpIsServer = 0;
   tcpIsClient = 0;
   tcpConnecting = 0;
   tcpRetryTimer = 0;
   tcpAddrValid = 0;
}

int JLinkTCPConnected(void)
{
   int i;
   if (!tcpPeersInit)
      return 0;
   if (tcpIsClient)
      return jlink_sock_valid(tcpPeers[0].sock) && !tcpConnecting;
   for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
      if (jlink_sock_valid(tcpPeers[i].sock))
         return 1;
   return 0;
}

static void jlink_tcp_queue_pending(jlink_peer_t *p, uint8_t b)
{
   uint32_t tail;
   if (p->txCount >= JLINK_TCP_TXPEND_SIZE)
      return;                    /* full: drop newest */
   tail = (p->txHead + p->txCount) % JLINK_TCP_TXPEND_SIZE;
   p->txPend[tail] = b;
   p->txCount++;
}

/* Returns 0 if the peer died. */
static int jlink_tcp_flush_pending(int idx)
{
   jlink_peer_t *p = &tcpPeers[idx];
   while (p->txCount > 0)
   {
      char c = (char)p->txPend[p->txHead];
      long n = (long)send(p->sock, &c, 1, 0);
      if (n == 1)
      {
         p->txHead = (p->txHead + 1) % JLINK_TCP_TXPEND_SIZE;
         p->txCount--;
      }
      else
      {
         int err = jlink_sockerr();
         if (n < 0 && err == JLINK_EWOULDBLOCK)
            return 1;            /* kernel buffer full; retry next poll */
         jlink_tcp_drop_peer(idx);
         return 0;
      }
   }
   return 1;
}

static void jlink_tcp_send_to_peer(int idx, uint8_t b)
{
   jlink_peer_t *p = &tcpPeers[idx];
   if (!jlink_sock_valid(p->sock))
      return;
   if (tcpIsClient && tcpConnecting)
      return;
   if (p->txCount > 0)
   {
      /* Preserve ordering behind already-pending bytes. */
      jlink_tcp_queue_pending(p, b);
      jlink_tcp_flush_pending(idx);
      return;
   }
   {
      char c = (char)b;
      long n = (long)send(p->sock, &c, 1, 0);
      if (n == 1)
         return;
      if (n < 0 && jlink_sockerr() == JLINK_EWOULDBLOCK)
      {
         jlink_tcp_queue_pending(p, b);
         return;
      }
      jlink_tcp_drop_peer(idx);
   }
}

void JLinkTCPSend(uint8_t b)
{
   int i;
   if (!tcpPeersInit)
      return;
   for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
      jlink_tcp_send_to_peer(i, b);
}

int JLinkTCPRecv(uint8_t *b)
{
   int i;
   if (!tcpPeersInit)
      return 0;
   for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
   {
      jlink_peer_t *p = &tcpPeers[i];
      char c;
      long n;
      if (!jlink_sock_valid(p->sock))
         continue;
      if (tcpIsClient && tcpConnecting)
         continue;
      n = (long)recv(p->sock, &c, 1, 0);
      if (n == 1)
      {
         int j;
         *b = (uint8_t)c;
         /* Shared-bus forward: every other peer hears this byte too. */
         if (tcpIsServer)
         {
            for (j = 0; j < JLINK_TCP_MAX_PEERS; j++)
               if (j != i)
                  jlink_tcp_send_to_peer(j, (uint8_t)c);
         }
         return 1;
      }
      if (n == 0)
      {
         /* Orderly shutdown by this peer. */
         jlink_tcp_drop_peer(i);
         continue;
      }
      if (jlink_sockerr() != JLINK_EWOULDBLOCK)
         jlink_tcp_drop_peer(i);
   }
   return 0;
}

void JLinkTCPPoll(void)
{
   int i;
   if (!tcpPeersInit)
      jlink_peers_reset();

   /* Client: retry a refused or dropped connection — the partner
      instance may not have been listening yet. */
   if (tcpIsClient && !jlink_sock_valid(tcpPeers[0].sock))
   {
      if (tcpRetryTimer > 0)
         tcpRetryTimer--;
      else
         jlink_tcp_start_connect();
   }

   /* Server: accept new peers while slots remain. */
   if (tcpIsServer && jlink_sock_valid(tcpListen))
   {
      for (;;)
      {
         jlink_sock_t s;
         int slot = -1;
         for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
            if (!jlink_sock_valid(tcpPeers[i].sock)) { slot = i; break; }
         if (slot < 0)
            break;
         s = accept(tcpListen, NULL, NULL);
         if (!jlink_sock_valid(s))
            break;
         jlink_set_nonblock(s);
         jlink_set_nodelay(s);
         tcpPeers[slot].sock = s;
         tcpPeers[slot].txHead = 0;
         tcpPeers[slot].txCount = 0;
      }
   }

   if (tcpConnecting && jlink_sock_valid(tcpPeers[0].sock))
   {
      /* A nonblocking connect has completed when the socket reports
         writability; poll cheaply via getsockopt(SO_ERROR) after a
         zero-timeout select. */
      fd_set wfds;
      struct timeval tv;
      int rc;
      FD_ZERO(&wfds);
      FD_SET(tcpPeers[0].sock, &wfds);
      tv.tv_sec = 0;
      tv.tv_usec = 0;
      rc = select((int)(tcpPeers[0].sock + 1), NULL, &wfds, NULL, &tv);
      if (rc > 0)
      {
         int soerr = 0;
         socklen_t slen = (socklen_t)sizeof(soerr);
         if (getsockopt(tcpPeers[0].sock, SOL_SOCKET, SO_ERROR,
                        (char *)&soerr, &slen) == 0 && soerr == 0)
         {
            tcpConnecting = 0;
            jlink_set_nodelay(tcpPeers[0].sock);
         }
         else
            jlink_tcp_drop_peer(0);
      }
   }

   for (i = 0; i < JLINK_TCP_MAX_PEERS; i++)
      if (jlink_sock_valid(tcpPeers[i].sock) && tcpPeers[i].txCount > 0)
         jlink_tcp_flush_pending(i);
}

#else /* !JLINK_HAVE_TCP — socketless console targets get inert stubs */

int JLinkTCPAvailable(void) { return 0; }
int JLinkTCPOpen(int is_server, const char *host, int port)
{
   (void)is_server;
   (void)host;
   (void)port;
   return 0;
}
void JLinkTCPClose(void) {}
int JLinkTCPConnected(void) { return 0; }
void JLinkTCPSend(uint8_t b) { (void)b; }
int JLinkTCPRecv(uint8_t *b) { (void)b; return 0; }
void JLinkTCPPoll(void) {}

#endif /* JLINK_HAVE_TCP */
