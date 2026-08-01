/* jlink.c — byte-transport seam for the JERRY UART.
   Loopback: bytes sent come back on the receive queue, modeling a
   console whose UARTO is wired to its own UARTI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlink.h"
#include "jlink_tcp.h"
#include "jlink_netpacket.h"
#include "state.h"

#define JLINK_RING_SIZE 256

/* Wall-clock wait support for JLinkAwaitReply (netlink reply wait).
   Platforms without it (bare console targets) compile a no-op. */
#if defined(_WIN32)
#include <windows.h>
#define JLINK_HAVE_WAIT 1
static long long JLinkNowUsec(void)
{
   /* QueryPerformanceCounter: GetTickCount64's ~15 ms granularity would
      let the bounded wait overshoot its whole budget in one tick. */
   LARGE_INTEGER f, c;
   QueryPerformanceFrequency(&f);
   QueryPerformanceCounter(&c);
   return (c.QuadPart / f.QuadPart) * 1000000LL
        + (c.QuadPart % f.QuadPart) * 1000000LL / f.QuadPart;
}
static void JLinkSleepUsec(int usec)
{
   Sleep((DWORD)((usec + 999) / 1000));
}
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__) || \
      defined(__ANDROID__)
#include <unistd.h>
#include <sys/time.h>
#define JLINK_HAVE_WAIT 1
static long long JLinkNowUsec(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}
static void JLinkSleepUsec(int usec)
{
   usleep((useconds_t)usec);
}
#endif

static int jlinkMode = JLINK_MODE_DISABLED;
static uint8_t jlinkRing[JLINK_RING_SIZE];
static uint32_t jlinkHead = 0;   /* next byte to pop */
static uint32_t jlinkCount = 0;

static char jlinkTCPHost[128] = "127.0.0.1";
static int jlinkTCPPort = 42171;

static uint32_t jlinkTxTotal = 0;
static uint32_t jlinkRxTotal = 0;

/* Reply wait: after a TX burst goes out over a real transport, the games
   spin on ASISTAT for the partner's answer.  The spin burns its emulated
   frame budget in ~1 ms of wall clock, so ANY transport latency slips
   the reply past retro_run and delivery quantizes to whole video frames
   (measured: 445 exchanges/s direct -> 35/s with 6 ms RTT).  While a
   reply is outstanding, JLinkAwaitReply blocks in wall-clock time so the
   reply lands in the SAME frame.  Wall-clock only; no emulated state is
   touched.

   The per-frame wait budget is ADAPTIVE — no user tuning.  Reply latency
   is sampled at the transport boundary (burst armed -> first byte back)
   into an EWMA, and each frame may wait up to twice that (plus margin),
   clamped to [4, 15] ms.  Localhost converges to the floor (sub-ms cost);
   a real network converges to what it actually needs.  Same approach as
   melonDS's local-wireless sync: the emulator stalls for the peer
   automatically, bounded so A/V pacing survives. */
static int jlinkAwaitingReply = 0;
static int jlinkWaitEnabled = 1;         /* option; 0 = disabled */
static int jlinkWaitBudgetUsec = 0;      /* per-frame remainder */
/* Latency sampling is decoupled from jlinkAwaitingReply: the wait can
   time out and disarm, but the late reply must STILL be measured or the
   adaptive budget can never grow past its floor. */
static int jlinkSamplePending = 0;
static long long jlinkLastTxUsec = 0;
static long long jlinkReplyEwmaUsec = 0; /* smoothed reply latency */

#define JLINK_WAIT_FLOOR_USEC   4000
#define JLINK_WAIT_CEIL_USEC   15000
#define JLINK_WAIT_MARGIN_USEC  2000
#define JLINK_SAMPLE_MAX_USEC  50000   /* slower = not a reply, ignore */

static void JLinkRingPush(uint8_t b)
{
   uint32_t tail;
   if (jlinkCount >= JLINK_RING_SIZE)
      return;   /* full: drop newest, and don't count it as received */
   tail = (jlinkHead + jlinkCount) % JLINK_RING_SIZE;
   jlinkRing[tail] = b;
   jlinkCount++;
   if (jlinkSamplePending)
   {
      /* First byte back after a TX: sample the reply latency for the
         adaptive wait budget.  Arrival is timestamped here no matter
         which path pumped it — and regardless of whether the wait
         already timed out — so a too-small budget measures the
         quantized latency, grows, and self-corrects within a few
         exchanges. */
#ifdef JLINK_HAVE_WAIT
      long long sample = JLinkNowUsec() - jlinkLastTxUsec;
      if (sample >= 0 && sample <= JLINK_SAMPLE_MAX_USEC)
         jlinkReplyEwmaUsec += (sample - jlinkReplyEwmaUsec) / 8;
#endif
      jlinkSamplePending = 0;
   }
   jlinkAwaitingReply = 0;   /* the partner spoke */
   /* Counted on ARRIVAL, not on the game draining the ring.  jlinkTxTotal is
    * incremented in JLinkSendByte(), i.e. at the transport boundary; counting
    * RX at JLinkRecvByte() instead measured the other side of the ring, so a
    * peer's traffic stayed invisible until the game got round to reading it
    * and the two counters were never comparable.  (In loopback the same byte
    * legitimately increments both: it really does go out and come back.) */
   jlinkRxTotal++;
}

void JLinkSetTCPEndpoint(const char *host, int port)
{
   if (host && host[0])
   {
      strncpy(jlinkTCPHost, host, sizeof(jlinkTCPHost) - 1);
      jlinkTCPHost[sizeof(jlinkTCPHost) - 1] = '\0';
   }
   if (port > 0 && port < 65536)
      jlinkTCPPort = port;
}

int JLinkOpen(int mode)
{
   JLinkClose();
   if (mode == JLINK_MODE_LOOPBACK)
   {
      jlinkMode = mode;
      return 1;
   }
   if (mode == JLINK_MODE_TCP_SERVER || mode == JLINK_MODE_TCP_CLIENT)
   {
      if (!JLinkTCPOpen(mode == JLINK_MODE_TCP_SERVER,
                        jlinkTCPHost, jlinkTCPPort))
         return 0;
      jlinkMode = mode;
      return 1;
   }
   if (mode == JLINK_MODE_NETPACKET)
   {
      jlinkMode = mode;
      return 1;
   }
   return 0;
}

void JLinkClose(void)
{
   JLinkTCPClose();
   jlinkMode = JLINK_MODE_DISABLED;
   jlinkHead = 0;
   jlinkCount = 0;
   /* jlink.h documents these as lifetime-per-session and reset here.  Left
    * running, they accumulate across independent sessions and across tests
    * in one process, which makes the diagnostics read as traffic that this
    * session never carried. */
   jlinkTxTotal = 0;
   jlinkRxTotal = 0;
   jlinkAwaitingReply = 0;
   jlinkWaitBudgetUsec = 0;
   jlinkSamplePending = 0;
   jlinkLastTxUsec = 0;
   jlinkReplyEwmaUsec = 0;
}

int JLinkMode(void)
{
   return jlinkMode;
}

int JLinkConnected(void)
{
   if (jlinkMode == JLINK_MODE_TCP_SERVER || jlinkMode == JLINK_MODE_TCP_CLIENT)
      return JLinkTCPConnected();
   if (jlinkMode == JLINK_MODE_NETPACKET)
      return JLinkNPActive();
   return jlinkMode != JLINK_MODE_DISABLED;
}

void JLinkNPDeliver(const uint8_t *buf, size_t len)
{
   size_t i;
   for (i = 0; i < len; i++)
      JLinkRingPush(buf[i]);
}

void JLinkSendByte(uint8_t b)
{
   if (jlinkMode == JLINK_MODE_DISABLED)
      return;
   jlinkTxTotal++;
   if (jlinkMode == JLINK_MODE_LOOPBACK)
      JLinkRingPush(b);
   else if (jlinkMode == JLINK_MODE_TCP_SERVER
            || jlinkMode == JLINK_MODE_TCP_CLIENT
            || jlinkMode == JLINK_MODE_NETPACKET)
   {
      if (jlinkMode == JLINK_MODE_NETPACKET)
         JLinkNPQueueByte(b);
      else
         JLinkTCPSend(b);
      jlinkAwaitingReply = 1;
      jlinkSamplePending = 1;
#ifdef JLINK_HAVE_WAIT
      /* Per TX byte, so the sample measures last-byte-out -> first-byte
         back rather than including our own burst length. */
      jlinkLastTxUsec = JLinkNowUsec();
#endif
   }
}

uint32_t JLinkTxTotal(void)
{
   return jlinkTxTotal;
}

uint32_t JLinkRxTotal(void)
{
   return jlinkRxTotal;
}

void JLinkPoll(void)
{
   if (jlinkMode == JLINK_MODE_NETPACKET)
   {
      /* Flush the per-frame TX batch; RX arrives via the frontend's
         receive callback into the ring. */
      JLinkNPFlush();
      return;
   }
   if (jlinkMode != JLINK_MODE_TCP_SERVER && jlinkMode != JLINK_MODE_TCP_CLIENT)
      return;
   JLinkTCPPoll();
   /* Drain socket bytes into the RX ring while space remains; the
      kernel socket buffer holds anything beyond that (backpressure). */
   while (jlinkCount < JLINK_RING_SIZE)
   {
      uint8_t b;
      if (!JLinkTCPRecv(&b))
         break;
      JLinkRingPush(b);
   }
}

void JLinkSetWaitEnabled(int enabled)
{
   jlinkWaitEnabled = enabled ? 1 : 0;
}

/* Called once per video frame from retro_run: refill the wait budget
   from the measured reply latency. */
void JLinkFrameTick(void)
{
   long long budget;
   if (!jlinkWaitEnabled)
   {
      jlinkWaitBudgetUsec = 0;
      return;
   }
   budget = jlinkReplyEwmaUsec * 2 + JLINK_WAIT_MARGIN_USEC;
   if (budget < JLINK_WAIT_FLOOR_USEC)
      budget = JLINK_WAIT_FLOOR_USEC;
   if (budget > JLINK_WAIT_CEIL_USEC)
      budget = JLINK_WAIT_CEIL_USEC;
   jlinkWaitBudgetUsec = (int)budget;
#ifdef JLINK_HAVE_WAIT
   {
      /* Headless diagnostic: VJ_NETLINK_WAIT_DEBUG=1 logs the adaptive
         state once a second. */
      static int dbg = -1;
      static unsigned dbgFrames = 0;
      if (dbg < 0)
      {
         const char *e = getenv("VJ_NETLINK_WAIT_DEBUG");
         dbg = (e && e[0] == '1') ? 1 : 0;
      }
      if (dbg && (++dbgFrames % 60u) == 0)
         fprintf(stderr, "jlink-wait: ewma=%dus budget=%dus\n",
                 (int)jlinkReplyEwmaUsec, jlinkWaitBudgetUsec);
   }
#endif
}

/* Block (bounded) until the partner's reply reaches the RX ring.  Only
   engages while a TX burst is unanswered on a real transport; the games
   call this path thousands of times per frame while spinning, so the
   fast-path bail must stay cheap. */
void JLinkAwaitReply(void)
{
#ifdef JLINK_HAVE_WAIT
   long long start, now;
   if (!jlinkAwaitingReply || jlinkWaitBudgetUsec <= 0 || jlinkCount > 0)
      return;
   if (!JLinkConnected())
      return;
   start = JLinkNowUsec();
   for (;;)
   {
      JLinkPump();
      if (jlinkCount > 0)
         break;
      if (!JLinkConnected())
         break;             /* peer went away mid-wait */
      now = JLinkNowUsec();
      if (now - start >= (long long)jlinkWaitBudgetUsec)
      {
         /* Peer silent for the whole budget: disarm until the next TX
            burst so idle ASISTAT polling doesn't stall every frame. */
         jlinkAwaitingReply = 0;
         break;
      }
      JLinkSleepUsec(500);
   }
   now = JLinkNowUsec();
   jlinkWaitBudgetUsec -= (int)(now - start);
#endif
}

void JLinkPump(void)
{
   if (jlinkMode == JLINK_MODE_NETPACKET)
   {
      JLinkNPFlush();
      JLinkNPPumpReceive();   /* receive() feeds the ring reentrantly */
      return;
   }
   /* TCP: same servicing as the per-frame poll — cheap when idle. */
   JLinkPoll();
}

int JLinkRecvByte(uint8_t *b)
{
   if (jlinkCount == 0)
      return 0;
   *b = jlinkRing[jlinkHead];
   jlinkHead = (jlinkHead + 1) % JLINK_RING_SIZE;
   jlinkCount--;
   return 1;
}

int JLinkRxPending(void)
{
   return (int)jlinkCount;
}

size_t JLinkStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   STATE_SAVE_VAR(buf, jlinkHead);
   STATE_SAVE_VAR(buf, jlinkCount);
   STATE_SAVE_BUF(buf, jlinkRing, JLINK_RING_SIZE);
   return (size_t)(buf - start);
}

size_t JLinkStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   STATE_LOAD_VAR(buf, jlinkHead);
   STATE_LOAD_VAR(buf, jlinkCount);
   STATE_LOAD_BUF(buf, jlinkRing, JLINK_RING_SIZE);
   if (jlinkHead >= JLINK_RING_SIZE)
      jlinkHead = 0;
   if (jlinkCount > JLINK_RING_SIZE)
      jlinkCount = JLINK_RING_SIZE;
   return (size_t)(buf - start);
}
