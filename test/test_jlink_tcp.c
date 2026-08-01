/* test_jlink_tcp.c — unit test for the TCP link backend.  The test acts
   as the remote peer with its own plain sockets, so both endpoints live
   in one process without sharing any jlink state. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "src/jerry/jlink.h"

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

#define TEST_PORT_BASE 42371

/* Poll the jlink side + give the kernel a moment, up to ~2 s. */
static int wait_for(int (*pred)(void), int iters)
{
    int i;
    for (i = 0; i < iters; i++)
    {
        JLinkPoll();
        if (pred())
            return 1;
        usleep(10000);
    }
    return 0;
}

static int pred_connected(void) { return JLinkConnected(); }
static int pred_rx_pending(void) { return JLinkRxPending() > 0; }

/* ---- server-mode: jlink listens, test connects ---- */
static void test_server_mode(void)
{
    int port = TEST_PORT_BASE;
    int peer;
    struct sockaddr_in sa;
    uint8_t b = 0;
    ssize_t n;
    char buf[4];

    JLinkSetTCPEndpoint(NULL, port);
    CHECK(JLinkOpen(JLINK_MODE_TCP_SERVER) == 1, "server: open listens");
    CHECK(!JLinkConnected(), "server: not connected before peer");

    peer = socket(AF_INET, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    CHECK(connect(peer, (struct sockaddr *)&sa, sizeof(sa)) == 0,
          "server: test peer connects");

    CHECK(wait_for(pred_connected, 200), "server: accept completes");

    /* jlink -> peer */
    JLinkSendByte(0xAA);
    JLinkPoll();
    n = recv(peer, buf, 1, 0);
    CHECK(n == 1 && (uint8_t)buf[0] == 0xAA, "server: TX byte reaches peer");

    /* peer -> jlink */
    buf[0] = (char)0x55;
    send(peer, buf, 1, 0);
    CHECK(wait_for(pred_rx_pending, 200), "server: RX byte arrives");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x55, "server: RX byte value");

    /* disconnect detection */
    close(peer);
    {
        int i, gone = 0;
        for (i = 0; i < 200; i++)
        {
            JLinkPoll();
            if (!JLinkConnected()) { gone = 1; break; }
            usleep(10000);
        }
        CHECK(gone, "server: peer close detected");
    }
    JLinkClose();
}

/* ---- client-mode: test listens, jlink connects ---- */
static void test_client_mode(void)
{
    int port = TEST_PORT_BASE + 1;
    int lsock, peer = -1;
    struct sockaddr_in sa;
    uint8_t b = 0;
    ssize_t n;
    char buf[4];
    int i;

    lsock = socket(AF_INET, SOCK_STREAM, 0);
    {
        int one = 1;
        setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(lsock, (struct sockaddr *)&sa, sizeof(sa)) == 0,
          "client: test listener binds");
    listen(lsock, 1);
    fcntl(lsock, F_SETFL, O_NONBLOCK);

    JLinkSetTCPEndpoint("127.0.0.1", port);
    CHECK(JLinkOpen(JLINK_MODE_TCP_CLIENT) == 1, "client: open starts connect");

    for (i = 0; i < 200 && peer < 0; i++)
    {
        JLinkPoll();
        peer = accept(lsock, NULL, NULL);
        if (peer < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            usleep(10000);
    }
    CHECK(peer >= 0, "client: jlink connected to test listener");
    CHECK(wait_for(pred_connected, 200), "client: JLinkConnected reports up");

    /* jlink -> peer */
    JLinkSendByte(0x42);
    JLinkPoll();
    n = recv(peer, buf, 1, 0);
    if (n < 0) { usleep(50000); n = recv(peer, buf, 1, 0); }
    CHECK(n == 1 && (uint8_t)buf[0] == 0x42, "client: TX byte reaches peer");

    /* peer -> jlink */
    buf[0] = (char)0x24;
    send(peer, buf, 1, 0);
    CHECK(wait_for(pred_rx_pending, 200), "client: RX byte arrives");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x24, "client: RX byte value");

    close(peer);
    close(lsock);
    JLinkClose();
}

static void test_unconnected_send_harmless(void)
{
    JLinkSetTCPEndpoint(NULL, TEST_PORT_BASE + 2);
    CHECK(JLinkOpen(JLINK_MODE_TCP_SERVER) == 1, "orphan: open ok");
    JLinkSendByte(0x77);           /* no peer: must not crash or queue */
    JLinkPoll();
    CHECK(JLinkRxPending() == 0, "orphan: nothing echoes");
    JLinkClose();
    CHECK(JLinkMode() == JLINK_MODE_DISABLED, "orphan: close resets mode");
}

int main(void)
{
    test_server_mode();
    test_client_mode();
    test_unconnected_send_harmless();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
