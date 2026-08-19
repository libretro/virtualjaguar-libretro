#define _DEFAULT_SOURCE 1   /* glibc/macOS: expose usleep() under -std=c99 */
#include <stdio.h>
#include <stdlib.h>      /* atoi */
#include <string.h>
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

int main(int argc, char **argv)
{
   int listen_only = 1, expect = 0, i, spins;
   for (i = 1; i < argc; i++)
   {
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
