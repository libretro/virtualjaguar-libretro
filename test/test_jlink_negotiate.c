/* test_jlink_negotiate.c -- integration test for the #552 wire-speedup
   negotiation state machine (src/jerry/jlink.c: JLinkNegTick/JLinkNegOnRaw).

   jlink.c is single-instance (file-scope statics, like production), so
   this test cannot run two REAL jlink.c cores against each other in one
   process.  Instead it drives ONE real client-mode jlink.c/uart.c pair
   against a hand-crafted fake peer built from plain sockets -- exactly
   what a real peer looks like from jlink.c's point of view, whether that
   peer runs #552 or not.  This is what proves the interop property #552
   depends on: a peer that only accepts the TCP connection and never
   answers on the discovery port (an old build, or one not in "auto")
   must NEVER see the emulated timing accelerate. */
#define _DEFAULT_SOURCE 1   /* usleep/select under -std=c99 on glibc */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "src/core/event.h"
#include "src/core/settings.h"
#include "src/jerry/jerry.h"
#include "src/jerry/uart.h"
#include "src/jerry/jlink.h"
#include "src/jerry/jlink_discover.h"

/* ---- stubs required by event.c's savestate callback registry ---- */
void HalflineCallback(void) {}
void TOMPITCallback(void) {}
void JERRYPIT1Callback(void) {}
void JERRYPIT2Callback(void) {}
void JERRYI2SCallback(void) {}
void DSPSampleCallback(void) {}
void GPUCPUINTCallback(void) {}

/* ---- stubs required by uart.c ---- */
struct VJSettings vjs;
bool JERRYIRQEnabled(int irq) { (void)irq; return false; }
void JERRYSetPendingIRQ(int irq) { (void)irq; }
void m68k_set_irq(unsigned int level) { (void)level; }
int TOMIRQEnabled(int irq) { (void)irq; return 0; }

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

/* Port bands, PID-spread, same convention as test_jlink_tcp.c's own
   comment: below every OS's ephemeral floor so a stray outgoing
   connection on a CI runner cannot collide, and distinct from
   test_jlink_discover.c's own band (43000+) so the two test binaries
   never fight over a port if a runner happens to launch them together. */
static int tcp_port(void)  { return 45000 + (int)(getpid() % 1500); }
static int disc_port(void) { return 46500 + (int)(getpid() % 1500); }

/* ---- fake peer: plain sockets, deliberately NOT going through
   jlink.c/jlink_discover.c at all -- this models "whatever is on the
   other end of the wire", #552-aware or not. ---- */
static int fakeListen = -1;
static int fakeAccepted = -1;

/* FINDING, not just a test workaround: the real client's discSock binds
   INADDR_ANY:dport with SO_REUSEPORT (jlink_discover.c, so two cores on
   ONE machine can share the fixed discovery port). A first cut of this
   fake peer ALSO bound INADDR_ANY:dport (mirroring "two real instances
   sharing 127.0.0.1") and measured that UNICAST negotiation between two
   sockets sharing a port via SO_REUSEPORT is NOT reliable: REUSEPORT
   load-balances a given unicast datagram to exactly one member of the
   group by an internal hash, and both the client's hello and the fake
   peer's reply were observed hairpinning back to their own sender
   instead of crossing over -- every attempt, not intermittently.
   SO_REUSEPORT's model is interchangeable workers sharing a listen
   queue, not two distinct peers each expecting delivery; broadcast fans
   out to every group member correctly (why the existing beacon works),
   unicast does not.

   A distinct bind address (127.0.0.2, standard BSD priority: specific
   bind wins over wildcard, independent of REUSEPORT) would sidestep
   this cleanly, but macOS does not route an unconfigured 127.0.0.0/8
   address to loopback without `ifconfig lo0 alias` -- root, and not
   something `make test` should need. So this fake peer instead never
   binds the discovery port at all: it sends its confirm from a plain
   unbound (ephemeral-port) socket straight to the client's known
   127.0.0.1:dport, where the client's discSock is now the ONLY socket
   bound there, so delivery is unambiguous. That means this fake peer
   cannot *receive* the client's hello (nothing is listening on dport
   except the client itself) -- it doesn't need to: the confirming
   scenario only needs to prove a well-formed foreign packet arriving at
   the client causes JLinkNegOnRaw() to confirm, and the separate
   "silent peer" scenario below needs the fake peer to send nothing,
   which trivially requires no discovery socket either. */
static int fakeSend = -1;

static void fake_peer_start(int tport, int dport)
{
   struct sockaddr_in sa;
   int one = 1;
   (void)dport;

   fakeListen = (int)socket(AF_INET, SOCK_STREAM, 0);
   setsockopt(fakeListen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
   memset(&sa, 0, sizeof(sa));
   sa.sin_family = AF_INET;
   sa.sin_addr.s_addr = htonl(INADDR_ANY);
   sa.sin_port = htons((unsigned short)tport);
   bind(fakeListen, (struct sockaddr *)&sa, sizeof(sa));
   listen(fakeListen, 1);
   fcntl(fakeListen, F_SETFL, fcntl(fakeListen, F_GETFL, 0) | O_NONBLOCK);

   fakeSend = (int)socket(AF_INET, SOCK_DGRAM, 0);
   fcntl(fakeSend, F_SETFL, fcntl(fakeSend, F_GETFL, 0) | O_NONBLOCK);

   fakeAccepted = -1;
}

static void fake_peer_stop(void)
{
   if (fakeAccepted >= 0) close(fakeAccepted);
   if (fakeListen >= 0)   close(fakeListen);
   if (fakeSend >= 0)     close(fakeSend);
   fakeAccepted = fakeListen = fakeSend = -1;
}

/* Accept the real jlink.c client's TCP connect, if it has arrived yet.
   Never sends or receives UART bytes -- this test only needs the
   connection UP, not carrying traffic. */
static void fake_peer_accept_if_pending(void)
{
   if (fakeAccepted < 0 && fakeListen >= 0)
   {
      int s = (int)accept(fakeListen, NULL, NULL);
      if (s >= 0)
         fakeAccepted = s;
   }
}

/* Fixed, arbitrary id for the fake peer -- guaranteed distinct from
   whatever random id the real client generates for itself, so the
   client's self-receipt filter (JLinkNegOnRaw's peerId == self check,
   #552) never mistakes the fake peer's reply for its own hello. */
#define FAKE_PEER_NEG_ID 0xF4CE9EE5u

/* Send one well-formed #552 negotiate packet straight to the client's
   known discovery socket -- see the design note above for why this does
   not first receive the client's own hello. Returns 1 if the send
   syscall accepted it. */
static int fake_peer_confirm_if_asked(int dport)
{
   uint8_t buf[64];
   struct sockaddr_in to;
   size_t n = JLinkNegEncode(buf, sizeof(buf), FAKE_PEER_NEG_ID);
   int rc;
   if (!n)
      return 0;
   memset(&to, 0, sizeof(to));
   to.sin_family = AF_INET;
   to.sin_addr.s_addr = inet_addr("127.0.0.1");
   to.sin_port = htons((unsigned short)dport);
   rc = (int)sendto(fakeSend, buf, n, 0, (struct sockaddr *)&to, sizeof(to));
   if (getenv("VJ_NEG_DEBUG"))
      fprintf(stderr, "[negdbg] fake_peer sendto dport=%d rc=%d\n", dport, rc);
   return rc > 0;
}

/* The "silent peer" scenario needs no socket action at all -- kept as a
   named no-op so the call sites read the same as the confirming case. */
static void fake_peer_ignore_neg_traffic(void)
{
}

/* Drive the real client instance's per-frame service loop, mirroring
   retro_run's JLinkFrameTick(); JLinkPoll(); UARTPoll(); ordering
   (libretro.c). */
static void tick(void)
{
   JLinkFrameTick();
   JLinkPoll();
}

static void settle_usec(int usec)
{
   struct timeval tv;
   tv.tv_sec = 0;
   tv.tv_usec = usec;
   select(0, NULL, NULL, NULL, &tv);
}

static void start_real_client(int tport, int dport)
{
   char portStr[16];
   sprintf(portStr, "%d", dport);
   setenv("VJ_DISC_PORT", portStr, 1);

   InitializeEventList();
   UARTReset();
   UARTSetWireSpeedupIntent(0);
   UARTSetWireSpeedupEffective(1);
   JLinkClose();

   JLinkSetTCPEndpoint("127.0.0.1", tport);
   UARTSetWireSpeedupIntent(1);         /* option = "auto" */
   UARTSetLinkMode(JLINK_MODE_TCP_CLIENT);
   JLinkDiscStart(1 /* listen_only */, JLINK_DISC_DEV_JAGLINK, tport);
}

static void stop_real_client(void)
{
   JLinkDiscStop();
   JLinkClose();
   unsetenv("VJ_DISC_PORT");
}

/* Pump both the real client and the fake peer together until either the
   client confirms (UARTWireSpeedup() > 1) or max_ms elapses.  responder
   decides whether the fake peer answers negotiate packets at all. */
static void pump_until_confirmed_or_timeout(int dport, int max_ms, int responder)
{
   int elapsed = 0;
   while (elapsed < max_ms)
   {
      tick();
      fake_peer_accept_if_pending();
      if (responder)
         fake_peer_confirm_if_asked(dport);
      else
         fake_peer_ignore_neg_traffic();
      if (UARTWireSpeedup() > 1)
         return;
      settle_usec(10000);
      elapsed += 10;
   }
}

/* ---- scenarios ---- */

/* The core positive case: a real peer (our fake, playing the confirming
   role) is on the other end -- the client's negotiated Effective value
   must reach UART_WIRE_SPEEDUP_MAX, and quickly (well under the 4s
   give-up ceiling; the fake peer answers on the very first hello). */
static void test_confirms_with_answering_peer(void)
{
   int tport = tcp_port(), dport = disc_port();
   fake_peer_start(tport, dport);
   start_real_client(tport, dport);

   pump_until_confirmed_or_timeout(dport, 2000, 1 /* responder answers */);
   CHECK(UARTWireSpeedup() == UART_WIRE_SPEEDUP_MAX,
         "client negotiates full speedup with a confirming peer");

   stop_real_client();
   fake_peer_stop();
}

/* THE interop safety property: TCP is UP (so the link "works" in every
   sense a pre-#552 build would recognize) but the peer never answers on
   the discovery port -- exactly what an old core, or a peer with the
   option off, looks like.  The client must NEVER apply the speedup
   unilaterally, for as long as it keeps trying. */
static void test_stays_stock_with_silent_peer(void)
{
   int tport = tcp_port() + 1, dport = disc_port() + 1;
   fake_peer_start(tport, dport);
   start_real_client(tport, dport);

   /* Give it several retry cycles' worth of wall time (JLINK_NEG_RETRY_MS
      is 500ms in jlink.c) without ever answering -- short of the ~4s
      give-up ceiling so this also proves nothing jumps the gun before
      giving up, not just that giving up eventually lands on stock. */
   pump_until_confirmed_or_timeout(dport, 1500, 0 /* responder never answers */);
   CHECK(UARTWireSpeedup() == 1,
         "client stays at stock timing the whole time a peer never answers");
   CHECK(fakeAccepted >= 0,
         "sanity: the TCP link itself really did come up during this window");

   stop_real_client();
   fake_peer_stop();
}

/* Reconciliation: once confirmed, dropping the connection must revert
   Effective to stock within one frame -- pinned directly rather than
   inferred, since this is the property that keeps a stale negotiated
   state from lingering across a peer loss (#552's savestate-adjacent
   safety requirement, exercised here without needing an actual
   save/load -- test_uart_loopback.c covers the save/load half). */
static void test_disconnect_reverts_to_stock(void)
{
   int tport = tcp_port() + 2, dport = disc_port() + 2;
   fake_peer_start(tport, dport);
   start_real_client(tport, dport);

   pump_until_confirmed_or_timeout(dport, 2000, 1);
   CHECK(UARTWireSpeedup() == UART_WIRE_SPEEDUP_MAX,
         "setup: confirmed before testing the drop");

   /* Pull the fake peer's end of the TCP connection. */
   close(fakeAccepted);
   fakeAccepted = -1;

   /* Give the real client's TCP poll a moment to notice the drop, then
      one more tick to reconcile. */
   {
      int i;
      for (i = 0; i < 20 && UARTWireSpeedup() != 1; i++)
      {
         tick();
         settle_usec(20000);
      }
   }
   CHECK(UARTWireSpeedup() == 1,
         "peer disconnect reverts effective wire speed to stock");

   stop_real_client();
   fake_peer_stop();
}

/* Config layer: turning the option off mid-negotiation must win
   immediately, even over an already-confirmed value -- pinned here
   against the REAL negotiation state machine (test_uart_loopback.c
   pins the same property against the setter API directly). */
static void test_intent_off_overrides_confirmed(void)
{
   int tport = tcp_port() + 3, dport = disc_port() + 3;
   fake_peer_start(tport, dport);
   start_real_client(tport, dport);

   pump_until_confirmed_or_timeout(dport, 2000, 1);
   CHECK(UARTWireSpeedup() == UART_WIRE_SPEEDUP_MAX,
         "setup: confirmed before testing intent-off");

   UARTSetWireSpeedupIntent(0);   /* option layer: user turned it off */
   CHECK(UARTWireSpeedup() == 1,
         "intent off immediately drops an already-confirmed value");

   /* One more tick must not resurrect it behind the option's back. */
   tick();
   CHECK(UARTWireSpeedup() == 1,
         "reconciliation does not re-arm after intent is turned off");

   stop_real_client();
   fake_peer_stop();
}

int main(void)
{
   vjs.hardwareTypeNTSC = true;
   test_confirms_with_answering_peer();
   test_stays_stock_with_silent_peer();
   test_disconnect_reverts_to_stock();
   test_intent_off_overrides_confirmed();
   printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
   return failures ? 1 : 0;
}
