/* jlink.c — byte-transport seam for the JERRY UART.
   Loopback: bytes sent come back on the receive queue, modeling a
   console whose UARTO is wired to its own UARTI. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1   /* glibc: expose POSIX (nanosleep) under c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlink.h"
#include "jlink_tcp.h"
#include "jlink_netpacket.h"
#include "jlink_discover.h"
#include "voicemodem.h"
#include "voicechat.h"
#include "state.h"
#include "uart.h"   /* UARTWireSpeedupIntent/UARTSetWireSpeedupEffective (#552) */

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
   static LARGE_INTEGER f;   /* frequency is fixed for the process */
   LARGE_INTEGER c;
   if (f.QuadPart == 0)
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
#include <time.h>
#include <sys/time.h>
#define JLINK_HAVE_WAIT 1
static long long JLinkNowUsec(void)
{
   /* CLOCK_MONOTONIC, not gettimeofday: every caller measures a DURATION
      (the bounded reply wait, the 1 s discovery beacon, the 10 s peer
      expiry), and wall-clock time can step backwards on an NTP correction.
      A backward step made the unsigned elapsed-time subtraction in
      JLinkDiscPeerExpire() wrap to a huge value and expire every live peer
      at once.  Falls back to gettimeofday where the monotonic clock is
      unavailable. */
#if defined(CLOCK_MONOTONIC)
   struct timespec ts;
   if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
      return (long long)ts.tv_sec * 1000000LL + (long long)ts.tv_nsec / 1000LL;
#endif
   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
   }
}
static void JLinkSleepUsec(int usec)
{
   /* nanosleep, not usleep: glibc hides usleep/useconds_t under strict
      -std=c99 (the test builds), and nanosleep is plain POSIX.1-2001. */
   struct timespec ts;
   ts.tv_sec = 0;
   ts.tv_nsec = (long)usec * 1000L;
   nanosleep(&ts, NULL);
}
#endif

/* Wall-clock milliseconds for the discovery beacon/expiry cadence
   (JLinkDiscPoll).  Placed after both JLinkNowUsec definitions above --
   it is defined once, not per platform branch -- and just downconverts
   whichever one this platform compiled. */
uint32_t JLinkNowMs(void)
{
#ifdef JLINK_HAVE_WAIT
   return (uint32_t)(JLinkNowUsec() / 1000LL);
#else
   /* No wait helper on this platform means no sockets either, so
      discovery is inert here and a frozen clock is harmless. */
   return 0;
#endif
}

static int jlinkMode = JLINK_MODE_DISABLED;
static int jlinkDevice = JLINK_DEVICE_JAGLINK;
static uint8_t jlinkRing[JLINK_RING_SIZE];
static uint32_t jlinkHead = 0;   /* next byte to pop */
static uint32_t jlinkCount = 0;

static char jlinkTCPHost[128] = "127.0.0.1";
static int jlinkTCPPort = 42171;

static uint32_t jlinkTxTotal = 0;
static uint32_t jlinkRxTotal = 0;

/* Wire-speedup negotiation (#552) state -- see the big comment ahead of
   JLinkNegTick() below for the protocol.  Declared here (ahead of
   JLinkClose(), which also touches them) rather than down by that
   comment, purely for C89 file-scope ordering. */
#define JLINK_NEG_RETRY_MS      500u
#define JLINK_NEG_MAX_ATTEMPTS  8      /* ~4s ceiling before giving up */
static int      jlinkNegConfirmed     = 0;  /* peer proven to also be "auto" */
static int      jlinkNegGaveUp        = 0;  /* client: retries exhausted */
static int      jlinkNegAttempts      = 0;  /* client: hellos sent so far */
static uint32_t jlinkNegLastSendMs    = 0;
static int      jlinkNegWasConnected  = 0;  /* previous tick's JLinkConnected() */
static uint32_t jlinkNegSelfId        = 0;  /* self-receipt filter, see below */
static int      jlinkNegSelfIdReady   = 0;

/* Set by JLinkFrameTick when JLinkDiscPoll reports the peer set changed;
   consumed (read + cleared) by JLinkDiscConsumeChanged so a UI layer can
   poll cheaply without re-deriving the diff itself. */
static int jlinkDiscChanged = 0;

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

static void JLinkVMDrain(void);

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
   /* Voice modem: transport bytes are inter-modem frames, parsed as
    * they arrive; the ring never holds console-visible bytes.  (Bounded
    * reentrancy through loopback: a parsed frame may send a reply frame,
    * which lands here again.) */
   if (jlinkDevice == JLINK_DEVICE_VOICEMODEM)
      JLinkVMDrain();
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

const char *JLinkGetTCPHost(void)
{
   return jlinkTCPHost;
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

void JLinkTxBurstEnd(void)
{
   if (jlinkDevice == JLINK_DEVICE_VOICEMODEM)
      VMTxBurstEnd();
   JLinkPump();
}

void JLinkClose(void)
{
   JLinkTCPClose();
   VMReset();
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
   /* #552: drop any negotiated state along with the connection it was
    * negotiated for.  JLinkNegTick() would self-heal this on the very
    * next frame regardless (its "not connected" branch resets the same
    * fields), but doing it here too keeps no stale cross-session state
    * sitting in these statics between JLinkClose() and that next tick --
    * the same belt-and-suspenders the fields just above already apply. */
   jlinkNegConfirmed = 0;
   jlinkNegAttempts = 0;
   jlinkNegGaveUp = 0;
   jlinkNegWasConnected = 0;
   jlinkNegSelfIdReady = 0;
   UARTSetWireSpeedupEffective(1);
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

void JLinkSetDevice(int device)
{
   if (device != jlinkDevice)
   {
      jlinkDevice = device;
      VMReset();
   }
}

int JLinkDevice(void)
{
   return jlinkDevice;
}

/* Move transport-ring bytes into the voice modem's frame parser.  In
 * voice-modem mode the ring carries inter-modem frames, never bytes the
 * console may see directly. */
static void JLinkVMDrain(void)
{
   uint8_t b;
   while (jlinkCount > 0)
   {
      b = jlinkRing[jlinkHead];
      jlinkHead = (jlinkHead + 1) % JLINK_RING_SIZE;
      jlinkCount--;
      VMWireInput(b);
   }
}

/* Console-deliverable RX depth for the active device. */
static int JLinkDeliverable(void)
{
   if (jlinkDevice == JLINK_DEVICE_VOICEMODEM)
      return VMConsoleRxPending();
   return (int)jlinkCount;
}

void JLinkSendByte(uint8_t b)
{
   if (jlinkMode == JLINK_MODE_DISABLED)
      return;
   if (jlinkDevice == JLINK_DEVICE_VOICEMODEM)
   {
      /* The modem consumes the console's TX stream; anything bound for
       * the far side goes out through JLinkWireSendByte. */
      VMConsoleTx(b);
      return;
   }
   JLinkWireSendByte(b);
}

void JLinkWireSendByte(uint8_t b)
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

/* Returns whether the discovery peer set has changed since the last
   call, clearing the flag.  Set by JLinkFrameTick's JLinkDiscPoll call;
   a UI layer polls this once per frame to know when to re-read the peer
   list, instead of diffing it itself. */
int JLinkDiscConsumeChanged(void)
{
   int changed = jlinkDiscChanged;
   jlinkDiscChanged = 0;
   return changed;
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

/* Wire-speedup negotiation (#552).  Piggybacks on the LAN discovery
   socket (jlink_discover.c's JLinkDiscSetRawHandler/JLinkDiscSendTo) --
   see docs/netlink-design.md and the #552 issue body for why: it is the
   one channel that is both already bound (for tcp_server/tcp_client) and
   provably incapable of being mistaken for emulated UART bytes, on a
   peer that does not implement this included -- a build without #552
   still has jlink_discover.c from #467, so JLinkDiscDecode() on the far
   end rejects our differently-magicked packet and the datagram is
   silently dropped exactly like a hostile/corrupt one always was.  Never
   touches jlinkRing.

   Scope, deliberately conservative rather than forced (see the #552
   guide): only a single TCP peer (JLinkTCPPeerCount() == 1) negotiates.
   A CatNet-style multi-drop hub (JLINK_MODE_TCP_SERVER with >1 peer)
   would need every peer to agree, not just one, and there is no
   multi-party protocol here -- it simply stays stock.  netpacket
   (frontend netplay) also stays stock: the core only ever sees a
   client_id from RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, never a
   peer address, so there is no address to send a discovery-port
   negotiation packet to, and reusing the netpacket data channel itself
   was rejected (any bytes sent over it reach an old peer's UART ring
   directly -- jlink_netpacket.c has no framing at all).  Loopback never
   reaches here either: JLinkDiscActive() is only ever true for the two
   TCP modes (see netlink_apply()'s discovery gating in libretro.c).

   There is exactly one non-stock divisor now that #552 replaced the
   2x/4x value list with disabled/auto (UART_WIRE_SPEEDUP_MAX), so the
   wire only needs to prove "the peer exists and is also configured for
   auto" -- no magnitude is exchanged, so there is nothing to clamp
   beyond what UARTSetWireSpeedupEffective() already clamps to.

   Protocol: the CLIENT already knows the server's address (it dialed
   it), so it periodically sends a JLinkNegEncode() hello to the
   server's host at JLinkDiscPort() (the effective discovery port --
   never the raw JLINK_DISC_PORT macro, so a VJ_DISC_PORT-isolated test
   dials the same port jlink_discover.c actually bound) until it gets
   one back or gives up.  The SERVER is purely reactive -- JLinkNegOnRaw()
   below learns the client's address for free from the incoming
   datagram's source and replies with its own packet as an ack (each
   side tags its own outgoing packet with a random per-session id and
   ignores any incoming packet carrying that same id back -- see
   jlink_discover.h -- so a core can never receive its own hello and
   "confirm" against itself).  Neither side commits to the faster
   timing (UARTSetWireSpeedupEffective(UART_WIRE_SPEEDUP_MAX))
   until it has seen a decodable packet from the other -- proof the peer
   runs #552 and is also in "auto" -- which is exactly the coordination
   #552 exists to remove needing done by two people, not the safety
   property the option previously depended on for it.

   KNOWN LIMITATION, measured not assumed (see test/test_jlink_negotiate.c's
   commit history): two cores on ONE machine, both dialing 127.0.0.1 and
   both wildcard-binding the discovery port via SO_REUSEPORT (the
   existing dev/test topology jlink_discover.c documents), will usually
   never actually confirm -- SO_REUSEPORT load-balances a unicast
   datagram to exactly one member of the group by an internal hash, and
   it is entirely plausible for that hash to consistently pick the
   sender's OWN socket over the other one, so the hello/ack never
   crosses over. The self-id filter above stops that from being
   mistaken for a real confirmation; it does not make same-host
   negotiation succeed. This is safe (falls back to stock, exactly the
   documented "peer never answers" case) but is a real, current gap for
   anyone dev-testing two cores against each other on localhost with
   auto enabled -- it is not merely unproven, it was reproduced. Real
   play, where the two consoles are on different machines, is
   unaffected: there is no REUSEPORT ambiguity when the sending socket
   is not itself a member of the destination host's discovery-port
   group. VJ_NETLINK_AUTO_DEBUG=1 logs negotiation state transitions
   (confirmed / gave up / reconciled to stock) to help diagnose either
   case. */
static int JLinkNegEligible(void)
{
   return (jlinkMode == JLINK_MODE_TCP_SERVER
           || jlinkMode == JLINK_MODE_TCP_CLIENT)
          && JLinkDiscActive()
          && UARTWireSpeedupIntent()
          && JLinkTCPPeerCount() == 1;
}

/* Lazily generated once per process/session (cleared in JLinkClose() with
   the rest of the negotiation state, so a fresh session gets a fresh id
   rather than reusing a value a just-closed session's packets might still
   be in flight carrying).  See jlink_discover.h for why this exists: two
   cores on one machine legitimately share the discovery port via
   SO_REUSEPORT, and without this a core can receive its own outbound
   hello back and "confirm" against itself. */
static uint32_t JLinkNegSelfId(void)
{
   if (!jlinkNegSelfIdReady)
   {
      srand((unsigned)(JLinkNowMs() ^ (uint32_t)(size_t)&jlinkNegSelfId));
      jlinkNegSelfId = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
      jlinkNegSelfIdReady = 1;
   }
   return jlinkNegSelfId;
}

/* Registered with jlink_discover.c as its unrecognized-datagram handler;
   called synchronously from within JLinkDiscPoll(), for BOTH client and
   server -- either side may receive the other's hello first. */
static void JLinkNegOnRaw(const uint8_t *buf, size_t len,
                          const char *from_addr, int from_port)
{
   uint8_t out[JLINK_NEG_PKT_LEN];
   uint32_t peerId;
   size_t n;

   if (!JLinkNegDecode(buf, len, &peerId))
      return;               /* not ours -- see jlink_discover.h */
   if (peerId == JLinkNegSelfId())
      return;               /* our own hello, looped back via SO_REUSEPORT */
   if (!JLinkNegEligible())
      return;                /* we are not (or no longer) a candidate */
   if (jlinkNegConfirmed)
      return;                /* already done; bounds the reply ping-pong */

   jlinkNegConfirmed = 1;
   UARTSetWireSpeedupEffective(UART_WIRE_SPEEDUP_MAX);
   if (getenv("VJ_NETLINK_AUTO_DEBUG"))
      fprintf(stderr, "jlink-auto: confirmed with %s:%d (peerId=%08x)\n",
              from_addr, from_port, peerId);

   /* Reply with OUR OWN id (not an echo of the sender's) so the far side
      can apply the identical self-filter above.  Lost entirely on packet
      loss is fine -- the client's own retry loop below resends; a
      duplicate arriving here after jlinkNegConfirmed is already set is a
      no-op via the guard above. */
   n = JLinkNegEncode(out, sizeof(out), JLinkNegSelfId());
   if (n)
      JLinkDiscSendTo(out, n, from_addr, from_port);
}

/* Magic-keyed dispatcher for every non-beacon datagram on the discovery
   socket: VJNG -> wire-speed negotiation (#552), VJVC -> voice chat
   (#485).  Unknown magics are ignored (same as a hostile packet). */
static void JLinkDiscRawDispatch(const uint8_t *buf, size_t len,
                                 const char *from_addr, int from_port)
{
   if (!buf || len < 4)
      return;
   if (buf[0] == JLINK_NEG_MAGIC_0 && buf[1] == JLINK_NEG_MAGIC_1
       && buf[2] == JLINK_NEG_MAGIC_2 && buf[3] == JLINK_NEG_MAGIC_3)
   {
      JLinkNegOnRaw(buf, len, from_addr, from_port);
      return;
   }
   if (buf[0] == VC_MAGIC_0 && buf[1] == VC_MAGIC_1
       && buf[2] == VC_MAGIC_2 && buf[3] == VC_MAGIC_3)
   {
      VoiceChatOnRaw(buf, len, from_addr, from_port);
      return;
   }
}

/* Called every JLinkFrameTick -- reconciles negotiation state against
   the CURRENT connection every frame (not once at connect) so a state
   load, an option toggle, or a peer disconnect all take effect on the
   very next frame rather than needing a matching special case each.
   This is also what satisfies "loading a state must not silently
   re-apply a speedup that is no longer agreed" (#552): a restored
   Effective value survives only as long as this reconciliation keeps
   agreeing the session is still eligible and still the SAME connection
   that earned it (jlinkNegWasConnected forces a fresh handshake, not a
   trusted carry-over, across any disconnected->connected transition). */
static void JLinkNegTick(void)
{
   int connected = JLinkConnected();
   /* Deliberately uninitialised: assigned right before the retry-throttle
    * check below, the first point that needs a timestamp.  That point is
    * only reached during an ACTIVE client-side negotiation (eligible,
    * connected, not yet confirmed/given-up), so the common every-frame
    * pass through this function -- netlink disabled entirely included --
    * costs no clock_gettime (#569/P8). */
   uint32_t nowMs;

   /* Test-only mechanism escape hatch, same spirit as jlink.c's own
      VJ_NETLINK_HOST/VJ_NETLINK_PORT-style overrides: real negotiation
      needs a real second instance to confirm with, and a harness that
      runs two real processes on ONE machine (test/tools/netlink_latency.c)
      hits the SAME-HOST SO_REUSEPORT hazard documented above
      JLinkNegEligible() -- unicast negotiation between two sockets
      sharing 127.0.0.1's discovery port cannot be relied on to cross
      over. VJ_FORCE_WIRE_SPEEDUP=N lets such a harness drive
      UARTFrameUsec()'s divisor mechanism directly (overrun back-
      pressure, exchange-rate scaling under REAL event-driven timing)
      without depending on that negotiation succeeding, and bypasses the
      rest of this function entirely so the normal reconcile-to-stock
      logic below can never fight it. Never read by, or reachable from,
      normal play -- no core option maps to it. */
   {
      const char *forced = getenv("VJ_FORCE_WIRE_SPEEDUP");
      if (forced && forced[0] && atoi(forced) > 1)
      {
         UARTSetWireSpeedupEffective((unsigned)atoi(forced));
         return;
      }
   }

   JLinkDiscSetRawHandler(JLinkDiscRawDispatch);

   if (!JLinkNegEligible() || !connected)
   {
      if (jlinkNegConfirmed || jlinkNegAttempts || jlinkNegGaveUp)
      {
         if (getenv("VJ_NETLINK_AUTO_DEBUG"))
            fprintf(stderr, "jlink-auto: reconciled to stock (eligible=%d "
                    "connected=%d) -- was confirmed=%d attempts=%d gaveup=%d\n",
                    JLinkNegEligible(), connected, jlinkNegConfirmed,
                    jlinkNegAttempts, jlinkNegGaveUp);
         jlinkNegConfirmed = 0;
         jlinkNegAttempts = 0;
         jlinkNegGaveUp = 0;
         UARTSetWireSpeedupEffective(1);
      }
      jlinkNegWasConnected = connected;
      return;
   }

   if (!jlinkNegWasConnected)
   {
      /* Fresh connection (including a reconnect to a different peer on
         the same slot): start the handshake clean rather than trusting
         whatever a prior connection -- or a loaded savestate -- left
         behind. */
      jlinkNegConfirmed = 0;
      jlinkNegAttempts = 0;
      jlinkNegGaveUp = 0;
      UARTSetWireSpeedupEffective(1);
   }
   jlinkNegWasConnected = connected;

   if (jlinkNegConfirmed || jlinkNegGaveUp)
      return;

   /* Only the client reaches out proactively -- it is the side that
      already has the peer's address (it dialed it).  The server is
      entirely reactive, handled by JLinkNegOnRaw above, which learns
      the client's address for free from the incoming datagram. */
   if (jlinkMode != JLINK_MODE_TCP_CLIENT)
      return;

   nowMs = JLinkNowMs();
   if (jlinkNegAttempts > 0
       && (uint32_t)(nowMs - jlinkNegLastSendMs) < JLINK_NEG_RETRY_MS)
      return;

   if (jlinkNegAttempts >= JLINK_NEG_MAX_ATTEMPTS)
   {
      jlinkNegGaveUp = 1;   /* peer never answered (#552): stay stock */
      if (getenv("VJ_NETLINK_AUTO_DEBUG"))
         fprintf(stderr, "jlink-auto: gave up after %d hellos, no reply -- "
                 "staying at stock timing\n", jlinkNegAttempts);
      return;
   }
   {
      uint8_t out[JLINK_NEG_PKT_LEN];
      size_t n = JLinkNegEncode(out, sizeof(out), JLinkNegSelfId());
      if (n)
         JLinkDiscSendTo(out, n, jlinkTCPHost, JLinkDiscPort());
   }
   jlinkNegAttempts++;
   jlinkNegLastSendMs = nowMs;
}

/* Called once per video frame from retro_run: refill the wait budget
   from the measured reply latency, and service LAN discovery.

   Discovery is driven from here rather than JLinkPoll(): JLinkPoll() is
   also reached via JLinkPump(), which games call thousands of times per
   frame while spinning on a reply (see JLinkAwaitReply's comment on the
   fast-path bail needing to stay cheap) -- a recvfrom() drain loop on
   every one of those calls would be wasteful and, unlike TCP polling,
   has no gate on jlinkMode to keep it rare.  JLinkFrameTick has no mode
   gate either, so discovery still runs while the link itself is
   JLINK_MODE_DISABLED (exactly the state it exists to help escape), but
   it runs at guaranteed once-per-video-frame cadence, which matches the
   1 Hz beacon and 10 s peer expiry this module works on. */
void JLinkFrameTick(void)
{
   long long budget;
   /* The clock query lives inside the discovery gate (and inside
      JLinkNegTick's own active-negotiation tail) rather than up front:
      with netlink off this function still runs every video frame, and a
      per-frame clock_gettime for a timestamp nobody reads was measurable
      noise on slow hosts (#569/P8).  Discovery and negotiation now take
      independent JLinkNowMs() samples a few microseconds apart in ticks
      where both run; both consumers work on 1 s / 10 s cadences, so the
      skew is immaterial. */
   if (JLinkDiscActive())
      jlinkDiscChanged |= JLinkDiscPoll(JLinkNowMs());
   /* #552 wire-speedup negotiation.  Runs unconditionally, ahead of the
      jlinkWaitEnabled early-return below -- negotiation must not depend
      on the unrelated reply-wait option. */
   JLinkNegTick();
   if (jlinkDevice == JLINK_DEVICE_VOICEMODEM)
      VMFrameTick();
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
   if (!jlinkAwaitingReply || jlinkWaitBudgetUsec <= 0
       || JLinkDeliverable() > 0)
      return;
   if (!JLinkConnected())
      return;
   start = JLinkNowUsec();
   for (;;)
   {
      JLinkPump();
      if (JLinkDeliverable() > 0)
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
   if (jlinkDevice == JLINK_DEVICE_VOICEMODEM)
      return VMConsoleRecv(b);
   if (jlinkCount == 0)
      return 0;
   *b = jlinkRing[jlinkHead];
   jlinkHead = (jlinkHead + 1) % JLINK_RING_SIZE;
   jlinkCount--;
   return 1;
}

int JLinkRxPending(void)
{
   return JLinkDeliverable();
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
