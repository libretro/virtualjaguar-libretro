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

int main(void)
{
    test_disabled_mode();
    test_loopback_roundtrip();
    test_overflow_drops_newest();
    test_state_roundtrip();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
