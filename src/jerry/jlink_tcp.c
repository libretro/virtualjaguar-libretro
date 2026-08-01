/* jlink_tcp.c — nonblocking TCP endpoint for the JagLink transport.
 *
 * Deliberately minimal: two Jaguars on a wire, so one listener + one
 * peer socket.  POSIX and winsock share the code behind a thin macro
 * layer; platforms without BSD-style sockets (bare console targets)
 * compile the stub tail of this file instead.
 */
#include <string.h>
#include "jlink_tcp.h"

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__) || \
    defined(__linux__) || defined(__ANDROID__)
#define JLINK_HAVE_TCP 1
#endif

#ifdef JLINK_HAVE_TCP

#ifdef _WIN32
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
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
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

#define JLINK_TCP_TXPEND_SIZE 1024

static jlink_sock_t tcpListen = JLINK_INVALID_SOCK;
static jlink_sock_t tcpPeer = JLINK_INVALID_SOCK;
static int tcpIsServer = 0;
static int tcpConnecting = 0;    /* client connect in flight */
static uint8_t tcpTxPend[JLINK_TCP_TXPEND_SIZE];
static uint32_t tcpTxHead = 0;
static uint32_t tcpTxCount = 0;

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
          || listen(tcpListen, 1) != 0)
      {
         jlink_closesock(tcpListen);
         tcpListen = JLINK_INVALID_SOCK;
         return 0;
      }
      jlink_set_nonblock(tcpListen);
   }
   else
   {
      int rc;
      if (!host || !host[0])
         host = "127.0.0.1";
      sa.sin_addr.s_addr = inet_addr(host);
      if (sa.sin_addr.s_addr == INADDR_NONE)
         return 0;
      tcpPeer = socket(AF_INET, SOCK_STREAM, 0);
      if (!jlink_sock_valid(tcpPeer))
         return 0;
      jlink_set_nonblock(tcpPeer);
      rc = connect(tcpPeer, (struct sockaddr *)&sa, sizeof(sa));
      if (rc != 0)
      {
         int err = jlink_sockerr();
         if (err != JLINK_EINPROGRESS && err != JLINK_EWOULDBLOCK)
         {
            jlink_closesock(tcpPeer);
            tcpPeer = JLINK_INVALID_SOCK;
            return 0;
         }
         tcpConnecting = 1;
      }
      else
         jlink_set_nodelay(tcpPeer);
   }

   tcpIsServer = is_server;
   return 1;
}

void JLinkTCPClose(void)
{
   if (jlink_sock_valid(tcpPeer))
      jlink_closesock(tcpPeer);
   if (jlink_sock_valid(tcpListen))
      jlink_closesock(tcpListen);
   tcpPeer = JLINK_INVALID_SOCK;
   tcpListen = JLINK_INVALID_SOCK;
   tcpIsServer = 0;
   tcpConnecting = 0;
   tcpTxHead = 0;
   tcpTxCount = 0;
}

int JLinkTCPConnected(void)
{
   return jlink_sock_valid(tcpPeer) && !tcpConnecting;
}

static void jlink_tcp_drop_peer(void)
{
   if (jlink_sock_valid(tcpPeer))
      jlink_closesock(tcpPeer);
   tcpPeer = JLINK_INVALID_SOCK;
   tcpConnecting = 0;
   tcpTxHead = 0;
   tcpTxCount = 0;
}

static void jlink_tcp_queue_pending(uint8_t b)
{
   uint32_t tail;
   if (tcpTxCount >= JLINK_TCP_TXPEND_SIZE)
      return;                    /* full: drop newest */
   tail = (tcpTxHead + tcpTxCount) % JLINK_TCP_TXPEND_SIZE;
   tcpTxPend[tail] = b;
   tcpTxCount++;
}

static void jlink_tcp_flush_pending(void)
{
   while (tcpTxCount > 0)
   {
      char c = (char)tcpTxPend[tcpTxHead];
      long n = (long)send(tcpPeer, &c, 1, 0);
      if (n == 1)
      {
         tcpTxHead = (tcpTxHead + 1) % JLINK_TCP_TXPEND_SIZE;
         tcpTxCount--;
      }
      else
      {
         int err = jlink_sockerr();
         if (n < 0 && err == JLINK_EWOULDBLOCK)
            break;               /* kernel buffer full; retry next poll */
         jlink_tcp_drop_peer();
         break;
      }
   }
}

void JLinkTCPSend(uint8_t b)
{
   if (!JLinkTCPConnected())
      return;
   if (tcpTxCount > 0)
   {
      /* Preserve ordering behind already-pending bytes. */
      jlink_tcp_queue_pending(b);
      jlink_tcp_flush_pending();
      return;
   }
   {
      char c = (char)b;
      long n = (long)send(tcpPeer, &c, 1, 0);
      if (n == 1)
         return;
      if (n < 0 && jlink_sockerr() == JLINK_EWOULDBLOCK)
      {
         jlink_tcp_queue_pending(b);
         return;
      }
      jlink_tcp_drop_peer();
   }
}

int JLinkTCPRecv(uint8_t *b)
{
   char c;
   long n;
   if (!JLinkTCPConnected())
      return 0;
   n = (long)recv(tcpPeer, &c, 1, 0);
   if (n == 1)
   {
      *b = (uint8_t)c;
      return 1;
   }
   if (n == 0)
   {
      /* Orderly shutdown by the peer. */
      jlink_tcp_drop_peer();
      return 0;
   }
   if (jlink_sockerr() != JLINK_EWOULDBLOCK)
      jlink_tcp_drop_peer();
   return 0;
}

void JLinkTCPPoll(void)
{
   if (tcpIsServer && jlink_sock_valid(tcpListen) && !jlink_sock_valid(tcpPeer))
   {
      jlink_sock_t s = accept(tcpListen, NULL, NULL);
      if (jlink_sock_valid(s))
      {
         jlink_set_nonblock(s);
         jlink_set_nodelay(s);
         tcpPeer = s;
         tcpTxHead = 0;
         tcpTxCount = 0;
      }
   }

   if (tcpConnecting && jlink_sock_valid(tcpPeer))
   {
      /* A nonblocking connect has completed when the socket reports
         writability; poll cheaply via getsockopt(SO_ERROR) after a
         zero-timeout select. */
      fd_set wfds;
      struct timeval tv;
      int rc;
      FD_ZERO(&wfds);
      FD_SET(tcpPeer, &wfds);
      tv.tv_sec = 0;
      tv.tv_usec = 0;
      rc = select((int)(tcpPeer + 1), NULL, &wfds, NULL, &tv);
      if (rc > 0)
      {
         int soerr = 0;
         socklen_t slen = (socklen_t)sizeof(soerr);
         if (getsockopt(tcpPeer, SOL_SOCKET, SO_ERROR,
                        (char *)&soerr, &slen) == 0 && soerr == 0)
         {
            tcpConnecting = 0;
            jlink_set_nodelay(tcpPeer);
         }
         else
            jlink_tcp_drop_peer();
      }
   }

   if (JLinkTCPConnected() && tcpTxCount > 0)
      jlink_tcp_flush_pending();
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
