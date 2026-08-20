#define _DEFAULT_SOURCE 1   /* glibc/macOS: expose usleep() under -std=c99 */
#include <stdio.h>
#include <stdlib.h>      /* atoi */
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "jlink_discover.h"

#ifdef _WIN32
#include <windows.h>
static uint32_t now_ms(void) { return (uint32_t)GetTickCount(); }
static void pace(void) { Sleep(5); }
#else
#include <sys/time.h>
#include <unistd.h>
/* Wall clock, NOT clock(): clock() measures CPU time, and this probe
   spins, so a CPU-time clock would race ahead of the 10 s peer expiry
   this test exists to exercise. */
static uint32_t now_ms(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (uint32_t)((uint32_t)tv.tv_sec * 1000u
                     + (uint32_t)(tv.tv_usec / 1000));
}
static void pace(void) { usleep(5000); }
#endif


/* --selftest: can this HOST deliver a UDP broadcast between two sockets, as
   seen by THIS binary?  Discovery fans out by broadcast, and that is an
   environment capability: CI macOS runners drop it, and on macOS/iOS it is
   gated on the Local Network permission -- which is granted PER BINARY, so
   a freshly built test binary raises its own prompt and an unanswered one
   fails closed, looking exactly like broken code.
   Probing from a script interpreter would test the WRONG identity, which is
   why this lives in the same binary the pair test runs.
   Loopback unicast is not a substitute: measured, with SO_REUSEPORT a
   unicast datagram to 127.0.0.1 reaches exactly ONE socket in the group,
   while a broadcast reaches all of them.  Fan-out is the requirement.
   Exit 0 = broadcast works, 77 = it does not (caller should skip). */
static int selftest(void)
{
   int a, b, one = 1, got = 0, i;
   struct sockaddr_in sa, to;
   char buf[8];
   int port = 42170 + 977;

   a = (int)socket(AF_INET, SOCK_DGRAM, 0);
   b = (int)socket(AF_INET, SOCK_DGRAM, 0);
   if (a < 0 || b < 0)
      return 77;
   setsockopt(a, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
   setsockopt(b, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#ifdef SO_REUSEPORT
   setsockopt(a, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof(one));
   setsockopt(b, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof(one));
#endif
   setsockopt(a, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));

   memset(&sa, 0, sizeof(sa));
   sa.sin_family = AF_INET;
   sa.sin_addr.s_addr = htonl(INADDR_ANY);
   sa.sin_port = htons((unsigned short)port);
   if (bind(a, (struct sockaddr *)&sa, sizeof(sa)) != 0
       || bind(b, (struct sockaddr *)&sa, sizeof(sa)) != 0)
   {
      close(a); close(b);
      return 77;
   }
   fcntl(b, F_SETFL, fcntl(b, F_GETFL, 0) | O_NONBLOCK);

   memset(&to, 0, sizeof(to));
   to.sin_family = AF_INET;
   to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
   to.sin_port = htons((unsigned short)port);

   for (i = 0; i < 40 && !got; i++)
   {
      sendto(a, "P", 1, 0, (struct sockaddr *)&to, sizeof(to));
      pace();
      if (recvfrom(b, buf, sizeof(buf), 0, NULL, NULL) > 0)
         got = 1;
   }
   close(a); close(b);
   return got ? 0 : 77;
}

int main(int argc, char **argv)
{
   int listen_only = 1, expect = 0, i, spins;
   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "--selftest"))
         return selftest();
      if (!strcmp(argv[i], "--role") && i + 1 < argc)
         listen_only = strcmp(argv[++i], "beacon") ? 1 : 0;
      else if (!strcmp(argv[i], "--expect") && i + 1 < argc)
         expect = atoi(argv[++i]);
   }
   if (!JLinkDiscStart(listen_only, 0, 42171))
   {
      printf("discover_probe: start failed\n");
      return 2;
   }
   for (spins = 0; spins < 600; spins++)
   {
      JLinkDiscPoll(now_ms());
      if (expect && JLinkDiscPeerCount() >= expect)
      {
         printf("discover_probe: saw %d peer(s)\n", JLinkDiscPeerCount());
         JLinkDiscStop();
         return 0;
      }
      /* Pace the loop (same 5 ms budget as test/tools/netlink_pair.c):
         the beacon side only broadcasts once per second, so an
         unpaced 600-iteration busy-spin exhausts itself in a few
         milliseconds and never lives long enough to see one. */
      pace();
   }
   JLinkDiscStop();
   return expect ? 1 : 0;
}
