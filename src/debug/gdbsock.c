/*
 * gdbsock.c -- loopback-only TCP transport for the GDB stub.
 *
 * The socket layer mirrors src/jerry/jlink_tcp.c, which already solved
 * the winsock/POSIX split for this codebase.
 * Design: docs/gdb-stub-design.md (issue #652).
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <string.h>
#include "gdbstub.h"
#include "log.h"   /* refused-peer warning; same logger gdbtarget.c uses */

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__ANDROID__)
#define GDB_HAVE_TCP 1
#endif

#ifdef GDB_HAVE_TCP

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET gdb_sock_t;
#define GDB_INVALID_SOCK  INVALID_SOCKET
#define gdb_sock_valid(s) ((s) != INVALID_SOCKET)
#define gdb_closesock(s)  closesocket(s)
#define gdb_sockerr()     WSAGetLastError()
#define GDB_EWOULDBLOCK   WSAEWOULDBLOCK
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int gdb_sock_t;
#define GDB_INVALID_SOCK  (-1)
#define gdb_sock_valid(s) ((s) >= 0)
#define gdb_closesock(s)  close(s)
#define gdb_sockerr()     errno
#define GDB_EWOULDBLOCK   EWOULDBLOCK
#endif

static gdb_sock_t gdbListen = GDB_INVALID_SOCK;
static gdb_sock_t gdbClient = GDB_INVALID_SOCK;

static void GDBSetNonBlocking(gdb_sock_t s)
{
#ifdef _WIN32
   u_long nb = 1;
   ioctlsocket(s, FIONBIO, &nb);
#else
   int fl = fcntl(s, F_GETFL, 0);
   if (fl >= 0)
      fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* Default is the safe one, so a caller that never sets it -- a test, or a
 * frontend that does not know the option exists -- gets loopback. */
static int gdbBindMode = GDB_BIND_LOOPBACK;

void GDBSockSetBindMode(int mode)
{
   /* Anything that is not explicitly LAN resolves to loopback: fail
    * closed, so a garbled or future option value cannot widen the bind. */
   gdbBindMode = (mode == GDB_BIND_LAN) ? GDB_BIND_LAN : GDB_BIND_LOOPBACK;
}

int GDBSockGetBindMode(void)
{
   return gdbBindMode;
}

/* Is this peer on a network we are willing to accept from?
 *
 * RFC1918 (10/8, 172.16/12, 192.168/16), CGNAT 100.64/10, link-local
 * 169.254/16, and loopback.  Anything else -- i.e. a routable public
 * address -- is refused.  Deliberately conservative: a VPN peer on a
 * non-private range is refused too, which is visible in the log rather
 * than silent. */
static int gdb_peer_allowed(uint32_t hostorder)
{
   if ((hostorder & 0xFF000000u) == 0x7F000000u) return 1; /* 127/8      */
   if ((hostorder & 0xFF000000u) == 0x0A000000u) return 1; /* 10/8       */
   if ((hostorder & 0xFFF00000u) == 0xAC100000u) return 1; /* 172.16/12  */
   if ((hostorder & 0xFFFF0000u) == 0xC0A80000u) return 1; /* 192.168/16 */
   if ((hostorder & 0xFFC00000u) == 0x64400000u) return 1; /* 100.64/10  */
   if ((hostorder & 0xFFFF0000u) == 0xA9FE0000u) return 1; /* 169.254/16 */
   return 0;
}

int GDBSockOpen(int port)
{
   struct sockaddr_in addr;
   int yes = 1;

   if (gdb_sock_valid(gdbListen))
      return 0;

   gdbListen = socket(AF_INET, SOCK_STREAM, 0);
   if (!gdb_sock_valid(gdbListen))
      return -1;

   setsockopt(gdbListen, SOL_SOCKET, SO_REUSEADDR,
              (const char *)&yes, sizeof(yes));

   memset(&addr, 0, sizeof(addr));
   addr.sin_family      = AF_INET;
   addr.sin_port        = htons((unsigned short)port);
   /* Loopback unless the user deliberately opted into LAN for this
    * session (issue #652).  Loopback remains the DEFAULT and the
    * fallback for any unrecognised option value -- see gdbstub.h. */
   addr.sin_addr.s_addr = htonl(gdbBindMode == GDB_BIND_LAN
                                ? INADDR_ANY : INADDR_LOOPBACK);

   if (bind(gdbListen, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
       listen(gdbListen, 1) != 0)
   {
      gdb_closesock(gdbListen);
      gdbListen = GDB_INVALID_SOCK;
      return -1;
   }

   GDBSetNonBlocking(gdbListen);
   return 0;
}

void GDBSockClose(void)
{
   if (gdb_sock_valid(gdbClient))
      gdb_closesock(gdbClient);
   if (gdb_sock_valid(gdbListen))
      gdb_closesock(gdbListen);

   gdbClient = GDB_INVALID_SOCK;
   gdbListen = GDB_INVALID_SOCK;
}

int GDBSockPoll(void)
{
   gdb_sock_t incoming;

   if (!gdb_sock_valid(gdbListen))
      return 0;

   if (gdb_sock_valid(gdbClient))
      return 1;

   {
      struct sockaddr_in peer;
      socklen_t          plen = (socklen_t)sizeof(peer);

      memset(&peer, 0, sizeof(peer));
      incoming = accept(gdbListen, (struct sockaddr *)&peer, &plen);
      if (!gdb_sock_valid(incoming))
         return 0;

      /* Only meaningful when bound beyond loopback; harmless otherwise,
       * since a loopback bind can only ever yield a 127/8 peer. */
      if (gdbBindMode == GDB_BIND_LAN
          && !gdb_peer_allowed(ntohl(peer.sin_addr.s_addr)))
      {
         {
            uint32_t a = ntohl(peer.sin_addr.s_addr);
            LOG_WRN("[GDB] REFUSED connection from %u.%u.%u.%u -- not a "
                    "private/link-local address. virtualjaguar_gdb_bind=lan "
                    "accepts only "
                    "RFC1918, CGNAT, link-local and loopback peers.\n",
                    (unsigned)((a >> 24) & 0xFF), (unsigned)((a >> 16) & 0xFF),
                    (unsigned)((a >> 8) & 0xFF), (unsigned)(a & 0xFF));
         }
         gdb_closesock(incoming);
         return 0;
      }
   }

   GDBSetNonBlocking(incoming);
   gdbClient = incoming;
   return 1;
}

int GDBSockRecv(char *buf, int max)
{
   int n;

   if (!gdb_sock_valid(gdbClient))
      return 0;

   n = (int)recv(gdbClient, buf, (size_t)max, 0);
   if (n > 0)
      return n;

   if (n == 0)
   {
      gdb_closesock(gdbClient);
      gdbClient = GDB_INVALID_SOCK;
      return -1;
   }

   if (gdb_sockerr() == GDB_EWOULDBLOCK)
      return 0;

   gdb_closesock(gdbClient);
   gdbClient = GDB_INVALID_SOCK;
   return -1;
}

int GDBSockSend(const char *buf, int len)
{
   if (!gdb_sock_valid(gdbClient))
      return -1;

   return (int)send(gdbClient, buf, (size_t)len, 0);
}

int GDBSockHasClient(void)
{
   return gdb_sock_valid(gdbClient) ? 1 : 0;
}

#else /* no BSD sockets on this target */

int  GDBSockOpen(int port) { (void)port; return -1; }
void GDBSockSetBindMode(int mode) { (void)mode; }
int  GDBSockGetBindMode(void) { return GDB_BIND_LOOPBACK; }
void GDBSockClose(void)    { }
int  GDBSockPoll(void)     { return 0; }
int  GDBSockRecv(char *buf, int max) { (void)buf; (void)max; return 0; }
int  GDBSockSend(const char *buf, int len) { (void)buf; (void)len; return -1; }
int  GDBSockHasClient(void) { return 0; }

#endif /* GDB_HAVE_TCP */
