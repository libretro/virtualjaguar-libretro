/* test_jlink_tcp.c — unit test for the TCP link backend.  The test acts
   as the remote peer with its own plain sockets, so both endpoints live
   in one process without sharing any jlink state.

   Ports are NOT hard-coded.  The old fixed base (42371) sat inside the
   Linux ephemeral range (32768-60999), and on CI runners a transient
   outgoing connection (runner agent, pip, codecov) would occasionally
   hold that exact port — bind() then fails EADDRINUSE, JLinkOpen()
   returns 0, and every downstream hub check cascades into FAIL (the
   coverage-job "hub" flake).  Server-mode ports are now picked from a
   PID-spread band BELOW every OS's ephemeral floor (Linux 32768, macOS
   49152) with a retry walk on bind failure; the client-mode listener
   just binds port 0 and asks the kernel what it got. */
#define _DEFAULT_SOURCE 1   /* usleep/MSG_DONTWAIT under -std=c99 on glibc */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "src/jerry/jlink.h"
#include "src/jerry/jlink_tcp.h"

/* #552: jlink.c's negotiation state machine calls into uart.c to read the
   config intent and write the negotiated effective divisor.  This test
   deliberately does not link uart.c (or the event.c/JERRY-stub chain it
   drags in) -- it tests the TCP backend in isolation -- so stub the two
   entry points minimally.  Real behavior is exercised end-to-end by
   test/test_jlink_negotiate.c and unit-pinned by test/test_uart_loopback.c. */
unsigned UARTWireSpeedupIntent(void) { return 0; }
void UARTSetWireSpeedupEffective(unsigned divisor) { (void)divisor; }

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (cond) printf("PASS %s\n", msg); \
         else { printf("FAIL %s\n", msg); failures++; } } while (0)

/* 17000-20999: above the well-known/registered services that actually
   run anywhere, below the ephemeral floor of every OS we test on.
   VJ_TEST_PORT_BASE pins the base for deterministic reproduction; it
   must leave room for the retry walk (offset 200 + 31 attempts), so
   out-of-range values fall back to the PID-spread default instead of
   silently leaving JLinkSetTCPEndpoint on a stale port. */
static int test_port_base(void)
{
    const char *e = getenv("VJ_TEST_PORT_BASE");
    if (e)
    {
        int p = atoi(e);
        if (p >= 1024 && p <= 65535 - 200 - 32)
            return p;
        if (p != 0)
            fprintf(stderr, "note: VJ_TEST_PORT_BASE '%s' out of range "
                            "(1024..%d), using PID default\n",
                    e, 65535 - 200 - 32);
    }
    return 17000 + (int)(getpid() % 4000);
}

/* Open a jlink TCP server on the first free port at-or-after
   base + off.  Returns the port, or -1 with diagnostics.  A busy slot
   is expected occasionally (another service, a concurrent suite);
   walking forward keeps the test independent of any one port. */
static int open_jlink_server(int off)
{
    int attempt;
    for (attempt = 0; attempt < 32; attempt++)
    {
        int port = test_port_base() + off + attempt;
        errno = 0;
        JLinkSetTCPEndpoint(NULL, port);
        if (JLinkOpen(JLINK_MODE_TCP_SERVER) == 1)
            return port;
        fprintf(stderr, "note: JLinkOpen(server) port %d failed (%s), "
                        "trying next\n", port, strerror(errno));
    }
    fprintf(stderr, "error: no bindable port in %d..%d\n",
            test_port_base() + off, test_port_base() + off + 31);
    return -1;
}

/* Poll the jlink side + give the kernel a moment.  Deadline-bounded
   (iters x 10 ms), returns early on success — generous iters cost
   nothing on the passing path, so waits use ~5 s not ~2 s. */
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

#define WAIT_ITERS 500   /* x 10 ms = 5 s deadline */

static int pred_connected(void) { return JLinkConnected(); }
static int pred_not_connected(void) { return !JLinkConnected(); }
static int pred_rx_pending(void) { return JLinkRxPending() > 0; }
static int pred_two_peers(void) { return JLinkTCPPeerCount() >= 2; }
static int pred_one_peer(void) { return JLinkTCPPeerCount() == 1; }

static int connect_peer(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    if (s < 0)
        return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0)
    {
        close(s);
        return -1;
    }
    return s;
}

/* Pump jlink and read one byte from a plain test socket; deadline-
   bounded like wait_for. */
static int peer_recv_byte(int s, uint8_t *b, int iters)
{
    int i;
    for (i = 0; i < iters; i++)
    {
        ssize_t n;
        JLinkPoll();
        n = recv(s, (char *)b, 1, MSG_DONTWAIT);
        if (n == 1) return 1;
        usleep(10000);
    }
    return 0;
}

/* ---- server-mode: jlink listens, test connects ---- */
static void test_server_mode(void)
{
    int port;
    int peer;
    uint8_t b = 0;
    ssize_t n;
    char buf[4];

    port = open_jlink_server(0);
    CHECK(port > 0, "server: open listens");
    CHECK(!JLinkConnected(), "server: not connected before peer");

    peer = connect_peer(port);
    CHECK(peer >= 0, "server: test peer connects");

    CHECK(wait_for(pred_connected, WAIT_ITERS), "server: accept completes");

    /* jlink -> peer */
    JLinkSendByte(0xAA);
    JLinkPoll();
    n = recv(peer, buf, 1, 0);
    CHECK(n == 1 && (uint8_t)buf[0] == 0xAA, "server: TX byte reaches peer");

    /* peer -> jlink */
    buf[0] = (char)0x55;
    send(peer, buf, 1, 0);
    CHECK(wait_for(pred_rx_pending, WAIT_ITERS), "server: RX byte arrives");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x55, "server: RX byte value");

    /* disconnect detection */
    close(peer);
    CHECK(wait_for(pred_not_connected, WAIT_ITERS),
          "server: peer close detected");
    JLinkClose();
}

/* ---- client-mode: test listens, jlink connects ---- */
static void test_client_mode(void)
{
    int port;
    int lsock, peer = -1;
    struct sockaddr_in sa;
    socklen_t salen;
    uint8_t b = 0;
    ssize_t n;
    char buf[4];
    int i;

    lsock = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(lsock >= 0, "client: test listener socket created");
    {
        int one = 1;
        setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = 0;              /* kernel picks: collision-proof */
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(lsock, (struct sockaddr *)&sa, sizeof(sa)) == 0,
          "client: test listener binds");
    salen = (socklen_t)sizeof(sa);
    CHECK(getsockname(lsock, (struct sockaddr *)&sa, &salen) == 0
          && ntohs(sa.sin_port) != 0, "client: kernel assigned a port");
    port = (int)ntohs(sa.sin_port);
    listen(lsock, 1);
    fcntl(lsock, F_SETFL, O_NONBLOCK);

    JLinkSetTCPEndpoint("127.0.0.1", port);
    CHECK(JLinkOpen(JLINK_MODE_TCP_CLIENT) == 1, "client: open starts connect");

    for (i = 0; i < WAIT_ITERS && peer < 0; i++)
    {
        JLinkPoll();
        peer = accept(lsock, NULL, NULL);
        if (peer < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            usleep(10000);
    }
    CHECK(peer >= 0, "client: jlink connected to test listener");
    CHECK(wait_for(pred_connected, WAIT_ITERS),
          "client: JLinkConnected reports up");

    /* jlink -> peer */
    JLinkSendByte(0x42);
    JLinkPoll();
    n = recv(peer, buf, 1, 0);
    if (n < 0) { usleep(50000); n = recv(peer, buf, 1, 0); }
    CHECK(n == 1 && (uint8_t)buf[0] == 0x42, "client: TX byte reaches peer");

    /* peer -> jlink */
    buf[0] = (char)0x24;
    send(peer, buf, 1, 0);
    CHECK(wait_for(pred_rx_pending, WAIT_ITERS), "client: RX byte arrives");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0x24, "client: RX byte value");

    close(peer);
    close(lsock);
    JLinkClose();
}

/* ---- hub mode: two peers on one jlink server, bus semantics ---- */
static void test_hub_mode(void)
{
    int port;
    int p1, p2;
    uint8_t b = 0;
    char c;

    port = open_jlink_server(100);
    CHECK(port > 0, "hub: open listens");

    p1 = connect_peer(port);
    CHECK(p1 >= 0, "hub: peer1 connects");
    CHECK(wait_for(pred_connected, WAIT_ITERS), "hub: peer1 accepted");
    p2 = connect_peer(port);
    CHECK(p2 >= 0, "hub: peer2 connects");
    /* The second accept happens on a later poll: wait for the observable
       condition (2 live peers), not a fixed number of sleeps. */
    CHECK(wait_for(pred_two_peers, WAIT_ITERS), "hub: peer2 accepted");

    /* local TX reaches BOTH peers */
    JLinkSendByte(0xA1);
    JLinkPoll();
    CHECK(peer_recv_byte(p1, &b, WAIT_ITERS) && b == 0xA1, "hub: TX to peer1");
    CHECK(peer_recv_byte(p2, &b, WAIT_ITERS) && b == 0xA1, "hub: TX to peer2");

    /* peer1 byte reaches the local ring AND peer2 (bus forward) */
    c = (char)0xB2;
    send(p1, &c, 1, 0);
    CHECK(wait_for(pred_rx_pending, WAIT_ITERS), "hub: peer1 byte in local ring");
    CHECK(JLinkRecvByte(&b) == 1 && b == 0xB2, "hub: local ring value");
    CHECK(peer_recv_byte(p2, &b, WAIT_ITERS) && b == 0xB2,
          "hub: forwarded to peer2");

    /* peer2 drop: session continues with peer1 */
    close(p2);
    CHECK(wait_for(pred_one_peer, WAIT_ITERS), "hub: peer2 drop detected");
    CHECK(JLinkConnected(), "hub: still connected after peer2 drop");
    JLinkSendByte(0xC3);
    JLinkPoll();
    CHECK(peer_recv_byte(p1, &b, WAIT_ITERS) && b == 0xC3,
          "hub: peer1 flows after drop");

    close(p1);
    JLinkClose();
}

static void test_unconnected_send_harmless(void)
{
    int port = open_jlink_server(200);
    CHECK(port > 0, "orphan: open ok");
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
    test_hub_mode();
    test_unconnected_send_harmless();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
