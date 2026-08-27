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

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__ANDROID__)
#define GDB_HAVE_TCP 1
#endif

#ifdef GDB_HAVE_TCP

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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
   /* Loopback ONLY. Never INADDR_ANY -- this is a security invariant. */
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

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

   incoming = accept(gdbListen, NULL, NULL);
   if (!gdb_sock_valid(incoming))
      return 0;

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

#else /* no BSD sockets on this target */

int  GDBSockOpen(int port) { (void)port; return -1; }
void GDBSockClose(void)    { }
int  GDBSockPoll(void)     { return 0; }
int  GDBSockRecv(char *buf, int max) { (void)buf; (void)max; return 0; }
int  GDBSockSend(const char *buf, int len) { (void)buf; (void)len; return -1; }

#endif /* GDB_HAVE_TCP */
