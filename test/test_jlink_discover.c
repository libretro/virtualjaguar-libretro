#include <stdio.h>
#include <string.h>
#include "jlink_discover.h"

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

int main(void)
{
   test_roundtrip();
   test_rejects_bad();
   test_name_never_unterminated();
   test_peer_table();
   test_capacity();
   if (failures) { printf("test_jlink_discover: %d FAILURE(S)\n", failures); return 1; }
   printf("test_jlink_discover: all passed\n");
   return 0;
}
