#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "jlink_discover.h"

/* Real-socket negotiation forwarding test (#552) needs plain POSIX
   sockets, mirroring test_jlink_tcp.c's own guard-free POSIX assumption
   for this project's test hosts. */
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#define JLINK_DISC_TEST_HAVE_NET 1
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

static int failures = 0;

static void check(int cond, const char *what)
{
   if (!cond) { printf("FAIL: %s\n", what); failures++; }
   else        printf("  ok: %s\n", what);
}

static void test_roundtrip(void)
{
   uint8_t buf[64];
   int dev = -1, port = -1;
   char name[JLINK_DISC_NAME_MAX];
   size_t n;

   n = JLinkDiscEncode(buf, sizeof(buf), JLINK_DISC_DEV_VOICEMODEM,
                       42171, "jaghub");
   check(n == JLINK_DISC_PKT_LEN, "encode writes exactly 40 bytes");
   check(JLinkDiscDecode(buf, n, &dev, &port, name, sizeof(name)) == 1,
         "decode accepts its own packet");
   check(dev == JLINK_DISC_DEV_VOICEMODEM, "device survives round-trip");
   check(port == 42171, "port survives round-trip");
   check(strcmp(name, "jaghub") == 0, "name survives round-trip");
}

static void test_rejects_bad(void)
{
   uint8_t buf[64];
   int dev, port;
   char name[JLINK_DISC_NAME_MAX];

   JLinkDiscEncode(buf, sizeof(buf), JLINK_DISC_DEV_JAGLINK, 42171, "x");

   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN - 1, &dev, &port,
                         name, sizeof(name)) == 0,
         "truncated packet rejected");
   buf[0] = 'X';
   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN, &dev, &port,
                         name, sizeof(name)) == 0,
         "wrong magic rejected");
   buf[0] = 'V'; buf[4] = 99;
   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN, &dev, &port,
                         name, sizeof(name)) == 0,
         "wrong version rejected");
}

static void test_name_never_unterminated(void)
{
   uint8_t buf[64];
   int dev, port;
   char name[JLINK_DISC_NAME_MAX];
   int i;

   JLinkDiscEncode(buf, sizeof(buf), JLINK_DISC_DEV_JAGLINK, 42171,
                   "0123456789012345678901234567890123456789");
   /* Stomp every name byte so nothing in the field is NUL. */
   for (i = 8; i < JLINK_DISC_PKT_LEN; i++)
      buf[i] = 'A';
   check(JLinkDiscDecode(buf, JLINK_DISC_PKT_LEN, &dev, &port,
                         name, sizeof(name)) == 1,
         "full-width name still decodes");
   check(name[JLINK_DISC_NAME_MAX - 1] == '\0',
         "decoded name is always NUL-terminated");
}

static void test_peer_table(void)
{
   JLinkDiscPeersReset();
   check(JLinkDiscPeerCount() == 0, "table starts empty");

   check(JLinkDiscPeerSeen("192.168.1.2", "a", 0, 42171, 1000) == 1,
         "first sighting reports a change");
   check(JLinkDiscPeerCount() == 1, "peer added");
   check(JLinkDiscPeerSeen("192.168.1.2", "a", 0, 42171, 2000) == 0,
         "refresh of a known peer reports no change");
   check(JLinkDiscPeerCount() == 1, "refresh does not duplicate");

   /* Seen at 5000, i.e. LATER than .2's last refresh at 2000.  Both at
      2000 would share a last_seen_ms, and then no expiry boundary can
      drop one without the other -- the scenario would be unsatisfiable. */
   check(JLinkDiscPeerSeen("192.168.1.3", "b", 1, 42172, 5000) == 1,
         "second peer reports a change");
   check(JLinkDiscPeerCount() == 2, "two peers tracked");

   check(JLinkDiscPeerExpire(2000 + JLINK_DISC_EXPIRE_MS) == 1,
         "expiry of the stale peer reports a change");
   check(JLinkDiscPeerCount() == 1, "stale peer dropped, fresh one kept");
   check(JLinkDiscPeerAt(0) != NULL, "a peer survived expiry");
   if (JLinkDiscPeerAt(0))
      check(strcmp(JLinkDiscPeerAt(0)->addr, "192.168.1.3") == 0,
            "surviving peer is the fresh one");
   check(JLinkDiscPeerAt(5) == NULL, "out-of-range index returns NULL");
}

static void test_capacity(void)
{
   char addr[JLINK_DISC_ADDR_MAX];
   int i;

   JLinkDiscPeersReset();
   for (i = 0; i < JLINK_DISC_MAX_PEERS + 4; i++)
   {
      sprintf(addr, "10.0.0.%d", i + 1);
      JLinkDiscPeerSeen(addr, "n", 0, 42171, 1000);
   }
   check(JLinkDiscPeerCount() == JLINK_DISC_MAX_PEERS,
         "table caps at JLINK_DISC_MAX_PEERS");
}

/* #552 negotiation codec -- pure, no sockets, same style as the beacon
   codec tests above. */
static void test_neg_roundtrip(void)
{
   uint8_t buf[64];
   size_t n;
   uint32_t id = 0;

   n = JLinkNegEncode(buf, sizeof(buf), 0xDEADBEEFu);
   check(n == JLINK_NEG_PKT_LEN, "encode writes exactly 12 bytes");
   check(JLinkNegDecode(buf, n, &id) == 1, "decode accepts its own packet");
   check(id == 0xDEADBEEFu, "senderId survives round-trip");
}

static void test_neg_rejects_bad(void)
{
   uint8_t buf[64];

   JLinkNegEncode(buf, sizeof(buf), 1);
   check(JLinkNegDecode(buf, JLINK_NEG_PKT_LEN - 1, NULL) == 0,
         "truncated negotiate packet rejected");
   buf[0] = 'X';
   check(JLinkNegDecode(buf, JLINK_NEG_PKT_LEN, NULL) == 0,
         "wrong magic rejected");
   buf[0] = JLINK_NEG_MAGIC_0; buf[4] = 99;
   check(JLinkNegDecode(buf, JLINK_NEG_PKT_LEN, NULL) == 0,
         "wrong version rejected");
}

/* THE requirement #552 exists to satisfy: a negotiation packet must never
   be mistakable for a beacon (or vice versa) by the OTHER protocol's
   decoder -- this is what lets an old peer's JLinkDiscDecode() silently
   drop a negotiate packet instead of corrupting the peer table, and lets
   a new peer's JLinkNegDecode() ignore a beacon instead of falsely
   "confirming" wire-speedup from an unrelated LAN discovery packet. */
static void test_neg_and_beacon_never_collide(void)
{
   uint8_t negBuf[64];
   uint8_t discBuf[64];
   int dev, port;
   char name[JLINK_DISC_NAME_MAX];
   size_t negLen = JLinkNegEncode(negBuf, sizeof(negBuf), 1);
   size_t discLen = JLinkDiscEncode(discBuf, sizeof(discBuf),
                                    JLINK_DISC_DEV_JAGLINK, 42171, "x");

   check(JLinkDiscDecode(negBuf, negLen, &dev, &port, name, sizeof(name)) == 0,
         "a negotiate packet is never accepted as a beacon");
   check(JLinkNegDecode(discBuf, discLen, NULL) == 0,
         "a beacon packet is never accepted as a negotiate packet");
}

#ifdef JLINK_DISC_TEST_HAVE_NET

/* Real-socket test: a #552 negotiate packet arriving on the discovery
   port must reach the registered raw handler and must NOT be treated as
   a beacon (peer table unchanged); a beacon must still work normally and
   must NOT reach the raw handler.  This is the actual interop property
   the design relies on, exercised over a real loopback socket rather
   than asserted from the pure codec alone.

   Port is PID-spread via VJ_DISC_PORT (jlink_discover.c already honors
   this override for exactly this reason -- see its own comment) so a
   concurrent `make test` run cannot collide on the fixed 42170 protocol
   port. */
static uint8_t   rawSeenBuf[64];
static size_t    rawSeenLen  = 0;
static int       rawSeenCount = 0;
static char      rawSeenAddr[JLINK_DISC_ADDR_MAX];

static void on_raw(const uint8_t *buf, size_t len, const char *from_addr,
                   int from_port)
{
   (void)from_port;
   rawSeenCount++;
   rawSeenLen = len < sizeof(rawSeenBuf) ? len : sizeof(rawSeenBuf);
   memcpy(rawSeenBuf, buf, rawSeenLen);
   strncpy(rawSeenAddr, from_addr, sizeof(rawSeenAddr) - 1);
   rawSeenAddr[sizeof(rawSeenAddr) - 1] = '\0';
}

static int disc_test_port(void)
{
   return 43000 + (int)(getpid() % 4000);
}

/* Send one UDP datagram to 127.0.0.1:port from a throwaway socket,
   modeling "a peer on the wire" without going through jlink_discover.c
   at all -- exactly what a hostile or version-skewed packet looks like
   from the receiver's point of view. */
static void send_raw(int port, const uint8_t *buf, size_t len)
{
   int s = (int)socket(AF_INET, SOCK_DGRAM, 0);
   struct sockaddr_in to;
   if (s < 0)
      return;
   memset(&to, 0, sizeof(to));
   to.sin_family = AF_INET;
   to.sin_addr.s_addr = inet_addr("127.0.0.1");
   to.sin_port = htons((unsigned short)port);
   sendto(s, buf, len, 0, (struct sockaddr *)&to, sizeof(to));
   close(s);
}

/* Give a nonblocking UDP round trip a moment to actually arrive before
   polling -- loopback is fast but not synchronous. */
static void settle(void)
{
   struct timeval tv;
   tv.tv_sec = 0;
   tv.tv_usec = 20000;
   select(0, NULL, NULL, NULL, &tv);
}

static void test_neg_forwarded_not_confused_with_beacon(void)
{
   char portStr[16];
   int port = disc_test_port();
   uint8_t negOut[JLINK_NEG_PKT_LEN];
   uint8_t discOut[JLINK_DISC_PKT_LEN];
   size_t n;

   sprintf(portStr, "%d", port);
#if defined(_WIN32)
   _putenv_s("VJ_DISC_PORT", portStr);
#else
   setenv("VJ_DISC_PORT", portStr, 1);
#endif

   JLinkDiscSetRawHandler(on_raw);
   check(JLinkDiscStart(1 /* listen_only */, JLINK_DISC_DEV_JAGLINK, 42171) == 1,
         "discovery socket binds on the isolated test port");

   /* A negotiate packet: must reach the raw handler, must NOT become a
      peer-table entry (it is not a valid beacon). */
   JLinkDiscPeersReset();
   rawSeenCount = 0;
   n = JLinkNegEncode(negOut, sizeof(negOut), 0x12345678u);
   send_raw(port, negOut, n);
   settle();
   JLinkDiscPoll(1000);
   check(rawSeenCount == 1, "negotiate packet reached the raw handler");
   {
      uint32_t id = 0;
      check(rawSeenLen == JLINK_NEG_PKT_LEN
            && JLinkNegDecode(rawSeenBuf, rawSeenLen, &id) == 1
            && id == 0x12345678u,
            "raw handler received the negotiate packet intact");
   }
   check(JLinkDiscPeerCount() == 0,
         "negotiate packet did NOT create a peer-table entry");

   /* A real beacon: must populate the peer table as before, must NOT
      reach the raw handler (proves the dispatch only catches what
      JLinkDiscDecode() itself rejects, never intercepting valid
      traffic). */
   JLinkDiscPeersReset();
   rawSeenCount = 0;
   n = JLinkDiscEncode(discOut, sizeof(discOut), JLINK_DISC_DEV_JAGLINK,
                       42171, "peer");
   send_raw(port, discOut, n);
   settle();
   JLinkDiscPoll(2000);
   check(JLinkDiscPeerCount() == 1, "a real beacon still populates the peer table");
   check(rawSeenCount == 0, "a real beacon never reaches the raw handler");

   /* JLinkDiscSendTo: the unicast send half of the protocol, echoed back
      to our own throwaway listener as a peer would see it. */
   {
      int echoSock = (int)socket(AF_INET, SOCK_DGRAM, 0);
      struct sockaddr_in sa;
      uint8_t rbuf[64];
      int got = -1;

      if (echoSock >= 0)
      {
         memset(&sa, 0, sizeof(sa));
         sa.sin_family = AF_INET;
         sa.sin_addr.s_addr = htonl(INADDR_ANY);
         sa.sin_port = 0;
         if (bind(echoSock, (struct sockaddr *)&sa, sizeof(sa)) == 0)
         {
            socklen_t slen = sizeof(sa);
            int echoPort;
            getsockname(echoSock, (struct sockaddr *)&sa, &slen);
            echoPort = ntohs(sa.sin_port);
            fcntl(echoSock, F_SETFL, fcntl(echoSock, F_GETFL, 0) | O_NONBLOCK);

            check(JLinkDiscSendTo(negOut, JLINK_NEG_PKT_LEN, "127.0.0.1",
                                  echoPort) == 1,
                  "JLinkDiscSendTo reports success");
            settle();
            got = (int)recv(echoSock, rbuf, sizeof(rbuf), 0);
         }
         close(echoSock);
      }
      check(got == (int)JLINK_NEG_PKT_LEN,
            "JLinkDiscSendTo actually delivers the packet");
   }

   JLinkDiscStop();
   JLinkDiscSetRawHandler(NULL);
}

#endif /* JLINK_DISC_TEST_HAVE_NET */

int main(void)
{
   test_roundtrip();
   test_rejects_bad();
   test_name_never_unterminated();
   test_peer_table();
   test_neg_roundtrip();
   test_neg_rejects_bad();
   test_neg_and_beacon_never_collide();
   test_capacity();
#ifdef JLINK_DISC_TEST_HAVE_NET
   test_neg_forwarded_not_confused_with_beacon();
#endif
   if (failures) { printf("test_jlink_discover: %d FAILURE(S)\n", failures); return 1; }
   printf("test_jlink_discover: all passed\n");
   return 0;
}
