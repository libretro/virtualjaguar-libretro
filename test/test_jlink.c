/* test_jlink.c — unit test for the JagLink byte-transport seam. */
#include <stdio.h>
#include <stdint.h>
#include "src/jerry/jlink.h"

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

static void test_disabled_mode(void)
{
    uint8_t b = 0xEE;
    JLinkClose();
    CHECK(JLinkMode() == JLINK_MODE_DISABLED, "closed -> mode disabled");
    CHECK(!JLinkConnected(), "disabled -> not connected");
    JLinkSendByte(0x41);
    CHECK(JLinkRxPending() == 0, "disabled send discards");
    CHECK(JLinkRecvByte(&b) == 0 && b == 0xEE, "disabled recv returns none");
}

static void test_loopback_roundtrip(void)
{
    uint8_t b = 0;
    CHECK(JLinkOpen(JLINK_MODE_LOOPBACK) == 1, "open loopback");
    CHECK(JLinkConnected(), "loopback -> connected");
    JLinkSendByte(0x11);
    JLinkSendByte(0x22);
    JLinkSendByte(0x33);
    CHECK(JLinkRxPending() == 3, "three bytes pending");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x11, "recv 1st in order");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x22, "recv 2nd in order");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x33, "recv 3rd in order");
    CHECK(JLinkRxPending() == 0, "drained");
    JLinkClose();
}

static void test_overflow_drops_newest(void)
{
    int i;
    uint8_t b = 0;
    JLinkOpen(JLINK_MODE_LOOPBACK);
    for (i = 0; i < 300; i++)
        JLinkSendByte((uint8_t)i);
    CHECK(JLinkRxPending() == 256, "ring capped at 256");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0, "oldest byte survives overflow");
    JLinkClose();
}

static void test_state_roundtrip(void)
{
    uint8_t buf[1024];
    uint8_t b = 0;
    size_t n;
    JLinkOpen(JLINK_MODE_LOOPBACK);
    JLinkSendByte(0xAB);
    JLinkSendByte(0xCD);
    n = JLinkStateSave(buf);
    CHECK(n > 0, "state save wrote bytes");
    (void)JLinkRecvByte(&b);
    (void)JLinkRecvByte(&b);
    CHECK(JLinkRxPending() == 0, "drained before load");
    CHECK(JLinkStateLoad(buf) == n, "state load consumed same size");
    CHECK(JLinkRxPending() == 2, "load restored 2 pending bytes");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0xAB, "restored byte order");
    JLinkClose();
}

/* jlink.h documents the traffic counters as per-session, reset by
   JLinkClose().  They used to survive a close and accumulate across
   sessions, which made the netlink diagnostics report traffic the current
   session never carried. */
static void test_counters_reset_on_close(void)
{
    JLinkOpen(JLINK_MODE_LOOPBACK);
    JLinkSendByte(0x5A);
    JLinkSendByte(0x5B);
    CHECK(JLinkTxTotal() == 2, "tx counted while open");
    JLinkClose();
    CHECK(JLinkTxTotal() == 0, "close resets tx total");
    CHECK(JLinkRxTotal() == 0, "close resets rx total");
}

/* RX is counted where the byte ARRIVES, not where the game drains the ring
   — otherwise a peer's traffic stays invisible in the counters until the
   game happens to read it, and tx/rx are never comparable. */
static void test_rx_counted_on_arrival(void)
{
    uint8_t b = 0;
    JLinkOpen(JLINK_MODE_LOOPBACK);
    JLinkSendByte(0x77);
    CHECK(JLinkRxTotal() == 1, "rx counted on arrival, before any read");
    CHECK(JLinkRxPending() == 1, "byte still queued");
    (void)JLinkRecvByte(&b);
    CHECK(JLinkRxTotal() == 1, "draining the ring does not re-count it");
    JLinkClose();
}

/* A dropped byte never entered the ring, so it must not be counted as
   received: rx total has to stay consistent with what a reader can see. */
static void test_dropped_bytes_not_counted(void)
{
    int i;
    JLinkOpen(JLINK_MODE_LOOPBACK);
    for (i = 0; i < 300; i++)
        JLinkSendByte((uint8_t)i);
    CHECK(JLinkTxTotal() == 300, "all 300 sends counted as tx");
    CHECK(JLinkRxTotal() == 256, "only the 256 that fit counted as rx");
    JLinkClose();
}

int main(void)
{
    test_disabled_mode();
    test_loopback_roundtrip();
    test_overflow_drops_newest();
    test_state_roundtrip();
    test_counters_reset_on_close();
    test_rx_counted_on_arrival();
    test_dropped_bytes_not_counted();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
